#include "grid_window.h"

#include <algorithm>
#include <string>

#include "app.h"

using namespace sheetpad;

namespace app {

// ---------------------------------------------------------------- scrolling

void GridWindow::ScrollTo(int row, int col) {
    editor.Commit();
    firstRow_ = row;
    firstCol_ = col;
    UpdateScrollbars();
    Redraw();
}

void GridWindow::ScrollBar(int bar, int code) {
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_ALL;
    GetScrollInfo(*this, bar, &info);
    int position = info.nPos;
    switch (code) {
    case SB_LINEUP: --position; break;
    case SB_LINEDOWN: ++position; break;
    case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
    case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: position = info.nTrackPos; break;
    case SB_TOP: position = info.nMin; break;
    case SB_BOTTOM: position = info.nMax; break;
    default: return;
    }
    if (bar == SB_VERT) {
        ScrollTo(position, firstCol_);
    } else {
        ScrollTo(firstRow_, position);
    }
}

void GridWindow::Wheel(int delta, bool horizontal) {
    int& remainder = horizontal ? hwheelRemainder_ : wheelRemainder_;
    remainder += delta;
    const int steps = remainder / WHEEL_DELTA;
    remainder %= WHEEL_DELTA;
    if (steps == 0) {
        return;
    }
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    RECT client{};
    GetClientRect(*this, &client);
    const int amount = lines == WHEEL_PAGESCROLL ? (horizontal ? VisibleCols(client) : VisibleRows(client))
                                                 : static_cast<int>(lines);
    if (horizontal) {
        ScrollTo(firstRow_, firstCol_ + steps * amount);
    } else {
        ScrollTo(firstRow_ - steps * amount, firstCol_);
    }
}

// ---------------------------------------------------------------- mouse

int GridWindow::ColAtX(int x) const {
    const int last = app_.GridCols() - 1;
    int col = firstCol_;
    int left = app_.m.rowNumberWidth;
    if (x >= left) {
        while (col < last && x >= left + ColWidth(col)) {
            left += ColWidth(col++);
        }
    } else {
        while (col > 0 && x < left) {
            left -= ColWidth(--col);
        }
    }
    return col;
}

Pos GridWindow::HitCell(int x, int y) const {
    const Metrics& m = app_.m;
    return {std::clamp(firstRow_ + FloorDiv(y - m.headerHeight, m.rowHeight), 0, app_.GridRows() - 1), ColAtX(x)};
}

int GridWindow::ResizeHandleAt(int x, int y) const {
    const Metrics& m = app_.m;
    if (y < 0 || y >= m.headerHeight || x < m.rowNumberWidth) {
        return -1;
    }
    const int dataCols = static_cast<int>(app_.doc.ColumnCount());
    const int slop = app_.Scale(4);
    const int col = ColAtX(x);
    if (col < dataCols && ColLeft(col) + ColWidth(col) - x <= slop) {
        return col;
    }
    if (col > firstCol_ && col - 1 < dataCols && x - ColLeft(col) <= slop) {
        return col - 1;
    }
    return -1;
}

bool GridWindow::StoreColumnWidth(int col, int pixels) {
    const int clamped = std::clamp(pixels, app_.Scale(kMinColumnWidth), app_.Scale(kMaxColumnWidth));
    const int logical = MulDiv(clamped, 96, app_.m.dpi);
    auto& widths = app_.view.columnWidths;
    const std::size_t key = static_cast<std::size_t>(col);
    const auto it = widths.find(key);
    const int current = it == widths.end() ? kDefaultColumnWidth : it->second;
    if (logical == current) {
        return false;
    }
    if (logical == kDefaultColumnWidth) {
        widths.erase(key);
    } else {
        widths[key] = logical;
    }
    app_.viewDirty = true;
    return true;
}

void GridWindow::SetColumnWidth(int col, int pixels) {
    if (StoreColumnWidth(col, pixels)) {
        UpdateScrollbars();
        Redraw();
    }
}

void GridWindow::MouseDown(int x, int y, bool doubleClick) {
    const Metrics& m = app_.m;
    editor.Commit();
    SetFocus(*this);
    const int resizeCol = ResizeHandleAt(x, y);
    if (resizeCol >= 0) {
        if (doubleClick) {
            // Fit to content, or back to the default when already fitted. Inside a multi-column selection the
            // clicked column decides and the action applies to every selected column.
            const CellRange sel = app_.Selection();
            const bool multi = sel.left < sel.right && resizeCol >= sel.left && resizeCol <= sel.right;
            AutofitColumns(multi ? sel.left : resizeCol, multi ? sel.right : resizeCol, resizeCol);
            return;
        }
        resizeCol_ = resizeCol;
        resizeOriginX_ = x;
        resizeStartWidth_ = ColWidth(resizeCol);
        dragMode_ = DragMode::ResizeColumn;
        SetCapture(*this);
        return;
    }
    const int lastRow = app_.DataRows() - 1;
    const int lastCol = static_cast<int>(app_.doc.ColumnCount()) - 1;
    const Pos hit = HitCell(x, y);
    // Whole row/column selections keep the first cell of the range active.
    if (x < m.rowNumberWidth && y < m.headerHeight) {
        app_.SetSelection({lastRow, lastCol}, {0, 0});
    } else if (y < m.headerHeight) {
        if (hit.col <= lastCol) {
            const RECT button = FilterButtonRect(hit.col);
            if (x >= button.left && x < button.right) {
                ShowFilterPopup(hit.col);
                return;
            }
        }
        const int col = std::min(hit.col, lastCol);
        app_.SetSelection({lastRow, KeyDown(VK_SHIFT) ? app_.anchor.col : col}, {0, col});
        dragMode_ = DragMode::Columns;
        SetCapture(*this);
    } else if (x < m.rowNumberWidth) {
        const int row = std::min(hit.row, lastRow);
        app_.SetSelection({KeyDown(VK_SHIFT) ? app_.anchor.row : row, lastCol}, {row, 0});
        dragMode_ = DragMode::Rows;
        SetCapture(*this);
    } else if (doubleClick) {
        app_.SetSelection(hit, hit);
        editor.Begin(nullptr);
    } else {
        app_.SetSelection(KeyDown(VK_SHIFT) ? app_.anchor : hit, hit);
        dragMode_ = DragMode::Cells;
        SetCapture(*this);
    }
}

void GridWindow::MouseMoved(int x, int y) {
    if (dragMode_ == DragMode::None) {
        return;
    }
    if (dragMode_ == DragMode::ResizeColumn) {
        SetColumnWidth(resizeCol_, resizeStartWidth_ + x - resizeOriginX_);
        return;
    }
    const int lastRow = app_.DataRows() - 1;
    const int lastCol = static_cast<int>(app_.doc.ColumnCount()) - 1;
    const Pos hit = HitCell(x, y);
    switch (dragMode_) {
    case DragMode::Cells:
        if (!(hit == app_.active)) {
            app_.SetSelection(app_.anchor, hit);
        }
        break;
    case DragMode::Columns: {
        const int col = std::min(hit.col, lastCol);
        if (col != app_.active.col) {
            app_.SetSelection({lastRow, app_.anchor.col}, {0, col});
        }
        break;
    }
    case DragMode::Rows: {
        const int row = std::min(hit.row, lastRow);
        if (row != app_.active.row) {
            app_.SetSelection({app_.anchor.row, lastCol}, {row, 0});
        }
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------- keyboard

void GridWindow::KeyPressed(WPARAM key) {
    const bool shift = KeyDown(VK_SHIFT);
    const bool ctrl = KeyDown(VK_CONTROL);
    RECT client{};
    GetClientRect(*this, &client);
    const int page = std::max(1, VisibleRows(client) - 1);
    const int lastRow = app_.GridRows() - 1;
    const int lastCol = app_.GridCols() - 1;
    const Pos active = app_.active;

    switch (key) {
    case VK_LEFT: app_.MoveActive(0, ctrl ? -lastCol : -1, shift); break;
    case VK_RIGHT: app_.MoveActive(0, ctrl ? lastCol : 1, shift); break;
    case VK_UP: app_.MoveActive(ctrl ? -lastRow : -1, 0, shift); break;
    case VK_DOWN: app_.MoveActive(ctrl ? lastRow : 1, 0, shift); break;
    case VK_PRIOR: app_.MoveActive(-page, 0, shift); break;
    case VK_NEXT: app_.MoveActive(page, 0, shift); break;
    case VK_HOME:
        app_.SetSelection(shift ? app_.anchor : Pos{ctrl ? 0 : active.row, 0}, {ctrl ? 0 : active.row, 0});
        break;
    case VK_END: {
        const Pos target{ctrl ? lastRow : active.row, static_cast<int>(app_.doc.ColumnCount()) - 1};
        app_.SetSelection(shift ? app_.anchor : target, target);
        break;
    }
    case VK_TAB: app_.MoveActive(0, shift ? -1 : 1, false); break;
    case VK_RETURN: app_.MoveActive(shift ? -1 : 1, 0, false); break;
    case VK_ESCAPE: app_.SetSelection(active, active); break;
    case VK_DELETE: app_.ClearSelection(); break;
    case VK_F2: editor.Begin(nullptr); break;
    case VK_BACK: {
        const std::wstring empty;
        editor.Begin(&empty);
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------- context menu

namespace {

std::wstring CountLabel(const wchar_t* singular, const wchar_t* pluralUnit, int count, const wchar_t* before,
                        const wchar_t* after) {
    if (count == 1) {
        return std::wstring(before) + singular + after;
    }
    return std::wstring(before) + std::to_wstring(count) + L" " + pluralUnit + after;
}

}  // namespace

void GridWindow::ShowContextMenu(int screenX, int screenY) {
    const Metrics& m = app_.m;
    editor.Commit();

    POINT point{screenX, screenY};
    if (screenX == -1 && screenY == -1) {
        const RECT cell = CellRect(app_.active);
        point = {cell.left + m.padding, cell.bottom};
        ClientToScreen(*this, &point);
        screenX = point.x;
        screenY = point.y;
    } else {
        ScreenToClient(*this, &point);
        if (point.x < m.rowNumberWidth && point.y < m.headerHeight) {
            return;
        }
        const CellRange current = app_.Selection();
        const Pos hit = HitCell(point.x, point.y);
        const int lastRow = app_.DataRows() - 1;
        const int lastCol = static_cast<int>(app_.doc.ColumnCount()) - 1;
        if (point.y < m.headerHeight) {
            const int col = std::min(hit.col, lastCol);
            if (col < current.left || col > current.right) {
                app_.SetSelection({lastRow, col}, {0, col});
            }
        } else if (point.x < m.rowNumberWidth) {
            const int row = std::min(hit.row, lastRow);
            if (row < current.top || row > current.bottom) {
                app_.SetSelection({row, lastCol}, {row, 0});
            }
        } else if (hit.row < current.top || hit.row > current.bottom || hit.col < current.left ||
                   hit.col > current.right) {
            app_.SetSelection(hit, hit);
        }
    }

    const CellRange sel = app_.Selection();
    const int rows = sel.bottom - sel.top + 1;
    const int columns = sel.right - sel.left + 1;
    HMENU menu = CreatePopupMenu();
    const auto add = [&](UINT id, const std::wstring& label) {
        AppendMenuW(menu, MF_STRING, id, label.c_str());
    };
    add(kCmdInsertRowAbove, CountLabel(L"Row", L"Rows", rows, L"Insert ", L" Above\tCtrl+Plus"));
    add(kCmdInsertRowBelow, CountLabel(L"Row", L"Rows", rows, L"Insert ", L" Below"));
    add(kCmdDeleteRows, CountLabel(L"Row", L"Rows", rows, L"Delete ", L"\tCtrl+Minus"));
    add(kCmdDuplicateRow, L"Duplicate Row\tCtrl+Shift+D");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(kCmdInsertColumnLeft, CountLabel(L"Column", L"Columns", columns, L"Insert ", L" Left\tCtrl+Shift+Plus"));
    add(kCmdInsertColumnRight, CountLabel(L"Column", L"Columns", columns, L"Insert ", L" Right"));
    add(kCmdDeleteColumns, CountLabel(L"Column", L"Columns", columns, L"Delete ", L"\tCtrl+Shift+Minus"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(kCmdCut, L"Cu&t\tCtrl+X");
    add(kCmdCopy, L"&Copy\tCtrl+C");
    add(kCmdPaste, L"&Paste\tCtrl+V");
    add(kCmdPasteSpecial, L"Paste &Special...\tCtrl+Shift+V");
    add(kCmdPasteHtmlText, L"Paste HT&ML Text...");
    add(kCmdDelete, L"Clear Contents\tDel");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(kCmdFilterColumn, L"Filter Column...\tAlt+Down");
    add(kCmdSortAsc, L"Sort Ascending");
    add(kCmdSortDesc, L"Sort Descending");
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, screenX, screenY, 0, *this, nullptr);
    DestroyMenu(menu);
}

// ---------------------------------------------------------------- window procedure

LRESULT GridWindow::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        app_.CreateFonts(*this);
        editor.Create(*this);
        UpdateScrollbars();
        app_.UpdateTitle();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_SIZE:
        if (backBitmap_) {
            DeleteObject(backBitmap_);
            backBitmap_ = nullptr;
        }
        UpdateScrollbars();
        if (editor.Editing()) {
            editor.Position();
        }
        Redraw();
        return 0;
    case WM_SETFOCUS:
        if (editor.Editing()) {
            SetFocus(editor);
        }
        return 0;
    case kMsgEditFocusLost:
        if (editor.Editing() && GetFocus() != editor) {
            editor.Commit();
        }
        return 0;
    case kMsgEditKey:
        editor.HandleKey(wParam, lParam != 0);
        return 0;
    case WM_COMMAND:
        if (lParam == 0) {
            app_.RunCommand(LOWORD(wParam));
        }
        return 0;
    case WM_SYSKEYDOWN:
        if (wParam == VK_DOWN && !editor.Editing()) {
            ShowFilterPopup(app_.active.col);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        KeyPressed(wParam);
        return 0;
    case WM_CHAR:
        if (wParam >= 32 && wParam != 127 && !editor.Editing()) {
            const std::wstring text(1, static_cast<wchar_t>(wParam));
            editor.Begin(&text);
        }
        return 0;
    case WM_VSCROLL:
        ScrollBar(SB_VERT, LOWORD(wParam));
        return 0;
    case WM_HSCROLL:
        ScrollBar(SB_HORZ, LOWORD(wParam));
        return 0;
    case WM_MOUSEWHEEL:
        Wheel(GET_WHEEL_DELTA_WPARAM(wParam), (LOWORD(wParam) & MK_SHIFT) != 0);
        return 0;
    case WM_MOUSEHWHEEL:
        Wheel(GET_WHEEL_DELTA_WPARAM(wParam), true);
        return 0;
    case WM_LBUTTONDOWN:
        MouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), false);
        return 0;
    case WM_LBUTTONDBLCLK:
        MouseDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
        return 0;
    case WM_MOUSEMOVE:
        MouseMoved(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (dragMode_ == DragMode::ResizeColumn) {
            app_.UpdateTitle();
        }
        dragMode_ = DragMode::None;
        ReleaseCapture();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(*this, &point);
            if (dragMode_ == DragMode::ResizeColumn || ResizeHandleAt(point.x, point.y) >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    case WM_CAPTURECHANGED:
        dragMode_ = DragMode::None;
        return 0;
    case WM_CONTEXTMENU:
        ShowContextMenu(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_DESTROY:
        if (backBitmap_) {
            DeleteObject(backBitmap_);
            backBitmap_ = nullptr;
        }
        DeleteObject(app_.font);
        return 0;
    default:
        break;
    }
    return DefaultProc(message, wParam, lParam);
}

}  // namespace app
