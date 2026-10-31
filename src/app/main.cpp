#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "csv.h"
#include "document.h"
#include "formula_store.h"
#include "text_encoding.h"
#include "view.h"
#include "app.h"

using namespace sheetpad;

namespace app {

App g;

bool KeyDown(int vk) {
    return (GetKeyState(vk) & 0x8000) != 0;
}

// ---------------------------------------------------------------- scrolling

void ScrollTo(HWND window, int row, int col) {
    CommitEdit(window);
    g.firstRow = row;
    g.firstCol = col;
    UpdateScrollbars(window);
    Redraw(window);
}

void ScrollBar(HWND window, int bar, int code) {
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_ALL;
    GetScrollInfo(window, bar, &info);
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
        ScrollTo(window, position, g.firstCol);
    } else {
        ScrollTo(window, g.firstRow, position);
    }
}

void Wheel(HWND window, int delta, bool horizontal) {
    int& remainder = horizontal ? g.hwheelRemainder : g.wheelRemainder;
    remainder += delta;
    const int steps = remainder / WHEEL_DELTA;
    remainder %= WHEEL_DELTA;
    if (steps == 0) {
        return;
    }
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    RECT client{};
    GetClientRect(window, &client);
    const int amount = lines == WHEEL_PAGESCROLL
        ? (horizontal ? VisibleCols(client) : VisibleRows(client))
        : static_cast<int>(lines);
    if (horizontal) {
        ScrollTo(window, g.firstRow, g.firstCol + steps * amount);
    } else {
        ScrollTo(window, g.firstRow - steps * amount, g.firstCol);
    }
}

// ---------------------------------------------------------------- mouse

Pos HitCell(HWND window, int x, int y) {
    (void)window;
    return {std::clamp(g.firstRow + FloorDiv(y - g.m.headerHeight, g.m.rowHeight), 0, GridRows() - 1),
            std::clamp(g.firstCol + FloorDiv(x - g.m.rowNumberWidth, g.m.columnWidth), 0, GridCols() - 1)};
}

void MouseDown(HWND window, int x, int y, bool doubleClick) {
    CommitEdit(window);
    SetFocus(window);
    const int lastRow = DataRows() - 1;
    const int lastCol = static_cast<int>(g.doc.ColumnCount()) - 1;
    const Pos hit = HitCell(window, x, y);
    // Whole row/column selections keep the first cell of the range active.
    if (x < g.m.rowNumberWidth && y < g.m.headerHeight) {
        SetSelection(window, {lastRow, lastCol}, {0, 0});
    } else if (y < g.m.headerHeight) {
        if (hit.col <= lastCol) {
            const RECT button = FilterButtonRect(hit.col);
            if (x >= button.left && x < button.right) {
                ShowFilterPopup(window, hit.col);
                return;
            }
        }
        const int col = std::min(hit.col, lastCol);
        SetSelection(window, {lastRow, KeyDown(VK_SHIFT) ? g.anchor.col : col}, {0, col});
        g.dragMode = DragMode::Columns;
        SetCapture(window);
    } else if (x < g.m.rowNumberWidth) {
        const int row = std::min(hit.row, lastRow);
        SetSelection(window, {KeyDown(VK_SHIFT) ? g.anchor.row : row, lastCol}, {row, 0});
        g.dragMode = DragMode::Rows;
        SetCapture(window);
    } else if (doubleClick) {
        SetSelection(window, hit, hit);
        BeginEdit(window, nullptr);
    } else {
        SetSelection(window, KeyDown(VK_SHIFT) ? g.anchor : hit, hit);
        g.dragMode = DragMode::Cells;
        SetCapture(window);
    }
}

void MouseMoved(HWND window, int x, int y) {
    if (g.dragMode == DragMode::None) {
        return;
    }
    const int lastRow = DataRows() - 1;
    const int lastCol = static_cast<int>(g.doc.ColumnCount()) - 1;
    const Pos hit = HitCell(window, x, y);
    switch (g.dragMode) {
    case DragMode::Cells:
        if (!(hit == g.active)) {
            SetSelection(window, g.anchor, hit);
        }
        break;
    case DragMode::Columns: {
        const int col = std::min(hit.col, lastCol);
        if (col != g.active.col) {
            SetSelection(window, {lastRow, g.anchor.col}, {0, col});
        }
        break;
    }
    case DragMode::Rows: {
        const int row = std::min(hit.row, lastRow);
        if (row != g.active.row) {
            SetSelection(window, {g.anchor.row, lastCol}, {row, 0});
        }
        break;
    }
    default:
        break;
    }
}


// ---------------------------------------------------------------- sort and filter

void SortByColumn(HWND window, int col, bool descending, bool add) {
    if (col < 0 || col >= static_cast<int>(g.doc.ColumnCount())) {
        return;
    }
    ChangeView(window, [&] {
        if (!add) {
            g.view.sorts.clear();
        }
        std::erase_if(g.view.sorts, [&](const SortLevel& level) { return level.column == static_cast<std::size_t>(col); });
        g.view.sorts.push_back({static_cast<std::size_t>(col), descending});
    });
}

void ApplySortToData(HWND window) {
    if (g.view.sorts.empty()) {
        return;
    }
    ChangeView(window, [] {
        g.view.Rebuild(g.doc);
        const std::vector<std::size_t> source = g.view.SortedPermutation(g.doc);
        g.doc.PermuteRows(source);
        g.view.sorts.clear();
    });
}

// ---------------------------------------------------------------- rows and columns

void RunStructuralCommand(HWND window, UINT command) {
    const CellRange sel = Selection();
    const std::size_t rows = static_cast<std::size_t>(sel.bottom - sel.top + 1);
    const std::size_t columns = static_cast<std::size_t>(sel.right - sel.left + 1);
    const int dataRows = DataRows();
    const int dataCols = static_cast<int>(g.doc.ColumnCount());

    switch (command) {
    case kCmdInsertRowAbove: {
        const std::size_t docAt = DocRowOf(sel.top);
        if (g.doc.InsertRows(docAt, rows)) {
            if (g.view.Active()) {
                g.view.RowsInserted(docAt, rows, static_cast<std::size_t>(sel.top));
            }
            AfterDocumentChanged(window);
        }
        break;
    }
    case kCmdInsertRowBelow: {
        const int at = std::min(sel.bottom + 1, dataRows);
        const std::size_t docAt = DocRowOf(at);
        if (g.doc.InsertRows(docAt, rows)) {
            if (g.view.Active()) {
                g.view.RowsInserted(docAt, rows, static_cast<std::size_t>(at));
            }
            AfterDocumentChanged(window);
            SetSelection(window, {at, sel.left}, {at + static_cast<int>(rows) - 1, sel.right});
        }
        break;
    }
    case kCmdDeleteRows: {
        std::vector<std::size_t> docRows;
        for (int row = sel.top; row <= std::min(sel.bottom, dataRows - 1); ++row) {
            docRows.push_back(DocRowOf(row));
        }
        std::sort(docRows.begin(), docRows.end());
        docRows.erase(std::unique(docRows.begin(), docRows.end()), docRows.end());
        if (g.doc.DeleteRowSet(docRows)) {
            if (g.view.Active()) {
                g.view.RowsDeleted(docRows);
            }
            AfterDocumentChanged(window);
            const int row = std::min(sel.top, DataRows() - 1);
            SetSelection(window, {row, sel.left}, {row, sel.left});
        }
        break;
    }
    case kCmdDuplicateRow: {
        const std::size_t docRow = DocRowOf(g.active.row);
        if (g.doc.DuplicateRow(docRow)) {
            if (g.view.Active()) {
                g.view.RowsInserted(docRow + 1, 1, static_cast<std::size_t>(g.active.row) + 1);
            }
            AfterDocumentChanged(window);
            SetSelection(window, {g.active.row + 1, g.active.col}, {g.active.row + 1, g.active.col});
        }
        break;
    }
    case kCmdInsertColumnLeft:
        if (g.doc.InsertColumns(static_cast<std::size_t>(sel.left), columns)) {
            g.view.ColumnsInserted(static_cast<std::size_t>(sel.left), columns);
            AfterDocumentChanged(window);
        }
        break;
    case kCmdInsertColumnRight: {
        const int at = std::min(sel.right + 1, dataCols);
        if (g.doc.InsertColumns(static_cast<std::size_t>(at), columns)) {
            g.view.ColumnsInserted(static_cast<std::size_t>(at), columns);
            AfterDocumentChanged(window);
            SetSelection(window, {sel.top, at}, {sel.bottom, at + static_cast<int>(columns) - 1});
        }
        break;
    }
    case kCmdDeleteColumns:
        if (g.doc.DeleteColumns(static_cast<std::size_t>(sel.left), columns)) {
            g.view.ColumnsDeleted(static_cast<std::size_t>(sel.left), columns);
            AfterDocumentChanged(window);
            const int col = std::min(sel.left, static_cast<int>(g.doc.ColumnCount()) - 1);
            SetSelection(window, {sel.top, col}, {sel.top, col});
        }
        break;
    default:
        break;
    }
}

std::wstring CountLabel(const wchar_t* singular, const wchar_t* pluralUnit, int count, const wchar_t* before,
                        const wchar_t* after) {
    if (count == 1) {
        return std::wstring(before) + singular + after;
    }
    return std::wstring(before) + std::to_wstring(count) + L" " + pluralUnit + after;
}

void ShowContextMenu(HWND window, int screenX, int screenY) {
    CommitEdit(window);
    RECT client{};
    GetClientRect(window, &client);

    POINT point{screenX, screenY};
    if (screenX == -1 && screenY == -1) {
        const RECT cell = CellRect(g.active);
        point = {cell.left + g.m.padding, cell.bottom};
        ClientToScreen(window, &point);
        screenX = point.x;
        screenY = point.y;
    } else {
        ScreenToClient(window, &point);
        if (point.x < g.m.rowNumberWidth && point.y < g.m.headerHeight) {
            return;
        }
        const CellRange current = Selection();
        const Pos hit = HitCell(window, point.x, point.y);
        const int lastRow = DataRows() - 1;
        const int lastCol = static_cast<int>(g.doc.ColumnCount()) - 1;
        if (point.y < g.m.headerHeight) {
            const int col = std::min(hit.col, lastCol);
            if (col < current.left || col > current.right) {
                SetSelection(window, {lastRow, col}, {0, col});
            }
        } else if (point.x < g.m.rowNumberWidth) {
            const int row = std::min(hit.row, lastRow);
            if (row < current.top || row > current.bottom) {
                SetSelection(window, {row, lastCol}, {row, 0});
            }
        } else if (hit.row < current.top || hit.row > current.bottom || hit.col < current.left ||
                   hit.col > current.right) {
            SetSelection(window, hit, hit);
        }
    }

    const CellRange sel = Selection();
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
    add(kCmdDelete, L"Clear Contents\tDel");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    add(kCmdFilterColumn, L"Filter Column...\tAlt+Down");
    add(kCmdSortAsc, L"Sort Ascending");
    add(kCmdSortDesc, L"Sort Descending");
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, screenX, screenY, 0, window, nullptr);
    DestroyMenu(menu);
}

void RunCommand(HWND window, UINT command) {
    if (g.editing) {
        switch (command) {
        case kCmdUndo: SendMessageW(g.edit, EM_UNDO, 0, 0); return;
        case kCmdCut: SendMessageW(g.edit, WM_CUT, 0, 0); return;
        case kCmdCopy: SendMessageW(g.edit, WM_COPY, 0, 0); return;
        case kCmdPaste: SendMessageW(g.edit, WM_PASTE, 0, 0); return;
        case kCmdDelete: SendMessageW(g.edit, WM_CLEAR, 0, 0); return;
        case kCmdSelectAll: SendMessageW(g.edit, EM_SETSEL, 0, -1); return;
        case kCmdRedo: return;
        case kCmdInsertRowAbove:
        case kCmdInsertRowBelow:
        case kCmdDeleteRows:
        case kCmdDuplicateRow:
        case kCmdInsertColumnLeft:
        case kCmdInsertColumnRight:
        case kCmdDeleteColumns:
        case kCmdFillDown:
        case kCmdFillRight: return;
        default: CommitEdit(window); break;
        }
    }

    switch (command) {
    case kCmdFillDown: FillSelection(window, true); return;
    case kCmdFillRight: FillSelection(window, false); return;
    case kCmdInsertRowAbove:
    case kCmdInsertRowBelow:
    case kCmdDeleteRows:
    case kCmdDuplicateRow:
    case kCmdInsertColumnLeft:
    case kCmdInsertColumnRight:
    case kCmdDeleteColumns: RunStructuralCommand(window, command); return;
    default: break;
    }
    switch (command) {
    case kCmdNew: NewDocument(window); break;
    case kCmdOpen: OpenDocument(window); break;
    case kCmdSave: Save(window); break;
    case kCmdSaveAs: SaveAs(window); break;
    case kCmdExit: SendMessageW(g.frame, WM_CLOSE, 0, 0); break;
    case kCmdUndo:
        if (g.doc.Undo()) {
            g.viewStale = true;
            AfterDocumentChanged(window);
        }
        break;
    case kCmdRedo:
        if (g.doc.Redo()) {
            g.viewStale = true;
            AfterDocumentChanged(window);
        }
        break;
    case kCmdCut:
        if (CopySelection(window, true)) {
            ClearSelection(window);
        }
        break;
    case kCmdCopy: CopySelection(window); break;
    case kCmdPaste: PasteClipboard(window); break;
    case kCmdDelete: ClearSelection(window); break;
    case kCmdSelectAll: SetSelection(window, {0, 0}, {DataRows() - 1, static_cast<int>(g.doc.ColumnCount()) - 1}); break;
    case kCmdFind: ShowFindBar(true); break;
    case kCmdFindNext:
    case kCmdFindPrev:
        if (!g.findVisible) {
            ShowFindBar(true);
        } else {
            FindNext(window, command == kCmdFindNext, false);
        }
        break;
    case kCmdFindClose: ShowFindBar(false); break;
    case kCmdHeaderRow:
        ChangeView(window, [] {
            g.view.headerRows = g.view.headerRows ? 0 : 1;
            g.viewDirty = true;
        });
        break;
    case kCmdFilterColumn: ShowFilterPopup(window, g.active.col); break;
    case kCmdClearFilters:
        if (!g.view.filters.empty()) {
            ChangeView(window, [] {
                g.view.filters.clear();
                g.viewDirty = true;
            });
        }
        break;
    case kCmdSortAsc: SortByColumn(window, g.active.col, false, false); break;
    case kCmdSortDesc: SortByColumn(window, g.active.col, true, false); break;
    case kCmdSortAddAsc: SortByColumn(window, g.active.col, false, true); break;
    case kCmdSortAddDesc: SortByColumn(window, g.active.col, true, true); break;
    case kCmdClearSort:
        if (!g.view.sorts.empty()) {
            ChangeView(window, [] { g.view.sorts.clear(); });
        }
        break;
    case kCmdApplySort: ApplySortToData(window); break;
    case kCmdReapply: ChangeView(window, [] {}); break;
    case kCmdInspector: ShowInspector(!g.inspectorVisible); break;
    default: break;
    }
}

void KeyPressed(HWND window, WPARAM key) {
    const bool shift = KeyDown(VK_SHIFT);
    const bool ctrl = KeyDown(VK_CONTROL);
    RECT client{};
    GetClientRect(window, &client);
    const int page = std::max(1, VisibleRows(client) - 1);
    const int lastRow = GridRows() - 1;
    const int lastCol = GridCols() - 1;

    switch (key) {
    case VK_LEFT: MoveActive(window, 0, ctrl ? -lastCol : -1, shift); break;
    case VK_RIGHT: MoveActive(window, 0, ctrl ? lastCol : 1, shift); break;
    case VK_UP: MoveActive(window, ctrl ? -lastRow : -1, 0, shift); break;
    case VK_DOWN: MoveActive(window, ctrl ? lastRow : 1, 0, shift); break;
    case VK_PRIOR: MoveActive(window, -page, 0, shift); break;
    case VK_NEXT: MoveActive(window, page, 0, shift); break;
    case VK_HOME:
        SetSelection(window, shift ? g.anchor : Pos{ctrl ? 0 : g.active.row, 0},
                     {ctrl ? 0 : g.active.row, 0});
        break;
    case VK_END: {
        const Pos target{ctrl ? lastRow : g.active.row, static_cast<int>(g.doc.ColumnCount()) - 1};
        SetSelection(window, shift ? g.anchor : target, target);
        break;
    }
    case VK_TAB: MoveActive(window, 0, shift ? -1 : 1, false); break;
    case VK_RETURN: MoveActive(window, shift ? -1 : 1, 0, false); break;
    case VK_ESCAPE: SetSelection(window, g.active, g.active); break;
    case VK_DELETE: ClearSelection(window); break;
    case VK_F2: BeginEdit(window, nullptr); break;
    case VK_BACK: {
        const std::wstring empty;
        BeginEdit(window, &empty);
        break;
    }
    default: break;
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateFonts(window);
        CreateEditControl(window);
        UpdateScrollbars(window);
        UpdateTitle(window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintWindow(window);
        return 0;
    case WM_SIZE:
        if (g.backBitmap) {
            DeleteObject(g.backBitmap);
            g.backBitmap = nullptr;
        }
        UpdateScrollbars(window);
        if (g.editing) {
            PositionEdit();
        }
        Redraw(window);
        return 0;
    case WM_SETFOCUS:
        if (g.editing) {
            SetFocus(g.edit);
        }
        return 0;
    case kMsgEditFocusLost:
        if (g.editing && GetFocus() != g.edit) {
            CommitEdit(window);
        }
        return 0;
    case kMsgEditKey:
        HandleEditKey(window, wParam, lParam != 0);
        return 0;
    case WM_COMMAND:
        if (lParam == 0) {
            RunCommand(window, LOWORD(wParam));
        }
        return 0;
    case WM_INITMENUPOPUP: {
        const HMENU menu = reinterpret_cast<HMENU>(wParam);
        EnableMenuItem(menu, kCmdUndo, MF_BYCOMMAND | (g.editing || g.doc.CanUndo() ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(menu, kCmdRedo, MF_BYCOMMAND | (!g.editing && g.doc.CanRedo() ? MF_ENABLED : MF_GRAYED));
        CheckMenuItem(menu, kCmdHeaderRow, MF_BYCOMMAND | (g.view.headerRows ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(menu, kCmdInspector, MF_BYCOMMAND | (g.inspectorVisible ? MF_CHECKED : MF_UNCHECKED));
        EnableMenuItem(menu, kCmdClearFilters, MF_BYCOMMAND | (g.view.filters.empty() ? MF_GRAYED : MF_ENABLED));
        EnableMenuItem(menu, kCmdClearSort, MF_BYCOMMAND | (g.view.sorts.empty() ? MF_GRAYED : MF_ENABLED));
        EnableMenuItem(menu, kCmdApplySort, MF_BYCOMMAND | (g.view.sorts.empty() ? MF_GRAYED : MF_ENABLED));
        EnableMenuItem(menu, kCmdReapply, MF_BYCOMMAND | (g.view.Active() ? MF_ENABLED : MF_GRAYED));
        return 0;
    }
    case WM_SYSKEYDOWN:
        if (wParam == VK_DOWN && !g.editing) {
            ShowFilterPopup(window, g.active.col);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        KeyPressed(window, wParam);
        return 0;
    case WM_CHAR:
        if (wParam >= 32 && wParam != 127 && !g.editing) {
            const std::wstring text(1, static_cast<wchar_t>(wParam));
            BeginEdit(window, &text);
        }
        return 0;
    case WM_VSCROLL:
        ScrollBar(window, SB_VERT, LOWORD(wParam));
        return 0;
    case WM_HSCROLL:
        ScrollBar(window, SB_HORZ, LOWORD(wParam));
        return 0;
    case WM_MOUSEWHEEL:
        Wheel(window, GET_WHEEL_DELTA_WPARAM(wParam), (LOWORD(wParam) & MK_SHIFT) != 0);
        return 0;
    case WM_MOUSEHWHEEL:
        Wheel(window, GET_WHEEL_DELTA_WPARAM(wParam), true);
        return 0;
    case WM_LBUTTONDOWN:
        MouseDown(window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), false);
        return 0;
    case WM_LBUTTONDBLCLK:
        MouseDown(window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), true);
        return 0;
    case WM_MOUSEMOVE:
        MouseMoved(window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        g.dragMode = DragMode::None;
        ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        g.dragMode = DragMode::None;
        return 0;
    case WM_CONTEXTMENU:
        ShowContextMenu(window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_DESTROY:
        if (g.backBitmap) {
            DeleteObject(g.backBitmap);
        }
        DeleteObject(g.font);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

using namespace app;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    HDC screen = GetDC(nullptr);
    g.m.dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) {
        MessageBoxW(nullptr, L"The application window could not be registered.", L"Sheetpad", MB_OK | MB_ICONERROR);
        return 1;
    }
    WNDCLASSEXW frameClass = windowClass;
    frameClass.style = 0;
    frameClass.lpfnWndProc = FrameProc;
    frameClass.lpszClassName = kFrameClass;
    frameClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    frameClass.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                                      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    frameClass.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                                        GetSystemMetrics(SM_CXSMICON),
                                                        GetSystemMetrics(SM_CYSMICON), 0));
    if (!RegisterClassExW(&frameClass)) {
        MessageBoxW(nullptr, L"The application window could not be registered.", L"Sheetpad", MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW filterClass = windowClass;
    filterClass.style = CS_DBLCLKS;
    filterClass.lpfnWndProc = FilterProc;
    filterClass.lpszClassName = kFilterClass;
    filterClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&filterClass)) {
        MessageBoxW(nullptr, L"The application window could not be registered.", L"Sheetpad", MB_OK | MB_ICONERROR);
        return 1;
    }

    TextHooks& hooks = GetTextHooks();
    hooks.sortKey = [](const std::string& text) -> std::string {
        const std::wstring wide = Utf8ToWide(text);
        if (wide.empty()) {
            return {};
        }
        const int size = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_SORTKEY | LINGUISTIC_IGNORECASE, wide.c_str(),
                                       static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr, 0);
        if (size <= 0) {
            return text;
        }
        std::string key(static_cast<std::size_t>(size), '\0');
        const int written = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_SORTKEY | LINGUISTIC_IGNORECASE,
                                          wide.c_str(), static_cast<int>(wide.size()),
                                          reinterpret_cast<LPWSTR>(key.data()), size, nullptr, nullptr, 0);
        if (written <= 0) {
            return text;
        }
        key.resize(static_cast<std::size_t>(written));
        return key;
    };
    hooks.lower = [](const std::string& text) -> std::string {
        std::wstring wide = Utf8ToWide(text);
        if (wide.empty()) {
            return text;
        }
        std::wstring lowered(wide.size(), L'\0');
        const int written = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING,
                                          wide.c_str(), static_cast<int>(wide.size()), lowered.data(),
                                          static_cast<int>(lowered.size()), nullptr, nullptr, 0);
        if (written <= 0) {
            return text;
        }
        lowered.resize(static_cast<std::size_t>(written));
        return WideToUtf8(lowered);
    };

    HWND frame = CreateWindowExW(0, kFrameClass, L"Sheetpad", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, Scale(1000), Scale(680), nullptr, nullptr,
                                 instance, nullptr);
    if (!frame) {
        MessageBoxW(nullptr, L"The application window could not be created.", L"Sheetpad", MB_OK | MB_ICONERROR);
        return 1;
    }
    HWND window = g.grid;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1) {
            LoadFile(window, argv[1]);
        }
        LocalFree(argv);
    }

    ShowWindow(frame, showCommand);
    UpdateWindow(frame);

    const std::array<ACCEL, 23> accelerators{{
        {FVIRTKEY | FCONTROL | FSHIFT, 'I', kCmdInspector},
        {FVIRTKEY | FCONTROL, 'F', kCmdFind},
        {FVIRTKEY, VK_F3, kCmdFindNext},
        {FVIRTKEY | FSHIFT, VK_F3, kCmdFindPrev},
        {FVIRTKEY | FCONTROL, 'N', kCmdNew},
        {FVIRTKEY | FCONTROL, 'O', kCmdOpen},
        {FVIRTKEY | FCONTROL, 'S', kCmdSave},
        {FVIRTKEY | FCONTROL | FSHIFT, 'S', kCmdSaveAs},
        {FVIRTKEY | FCONTROL, 'Z', kCmdUndo},
        {FVIRTKEY | FCONTROL, 'Y', kCmdRedo},
        {FVIRTKEY | FCONTROL | FSHIFT, 'Z', kCmdRedo},
        {FVIRTKEY | FCONTROL, 'X', kCmdCut},
        {FVIRTKEY | FCONTROL, 'C', kCmdCopy},
        {FVIRTKEY | FCONTROL, 'V', kCmdPaste},
        {FVIRTKEY | FCONTROL, 'A', kCmdSelectAll},
        {FVIRTKEY | FCONTROL, 'D', kCmdFillDown},
        {FVIRTKEY | FCONTROL, 'R', kCmdFillRight},
        {FVIRTKEY | FCONTROL | FSHIFT, 'D', kCmdDuplicateRow},
        {FVIRTKEY | FCONTROL, VK_OEM_PLUS, kCmdInsertRowAbove},
        {FVIRTKEY | FCONTROL, VK_OEM_MINUS, kCmdDeleteRows},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_OEM_PLUS, kCmdInsertColumnLeft},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_OEM_MINUS, kCmdDeleteColumns},
        {FVIRTKEY | FCONTROL, VK_ADD, kCmdInsertRowAbove},
    }};
    HACCEL table = CreateAcceleratorTableW(const_cast<ACCEL*>(accelerators.data()),
                                           static_cast<int>(accelerators.size()));

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        const HWND focus = GetFocus();
        const bool textFocus = (g.findVisible && focus == g.findEdit) || (g.inspectorVisible && focus == g.inspector);
        const bool editShortcut = textFocus && message.message == WM_KEYDOWN &&
                                  KeyDown(VK_CONTROL) &&
                                  (message.wParam == 'A' || message.wParam == 'C' || message.wParam == 'V' ||
                                   message.wParam == 'X' || message.wParam == 'Z' || message.wParam == 'Y');
        if (!editShortcut && TranslateAcceleratorW(frame, table, &message)) {
            continue;
        }
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    DestroyAcceleratorTable(table);
    return static_cast<int>(message.wParam);
}
