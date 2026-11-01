#include "grid_window.h"

#include <algorithm>
#include <string>

#include "app.h"
#include "text_encoding.h"

using namespace sheetpad;

namespace app {

namespace {

constexpr std::size_t kMaxDisplayBytes = 1024;
// Autofit measures at most this many rows, so that it stays instant on very large files.
constexpr int kAutofitMaxRows = 5000;

COLORREF Blend(COLORREF foreground, COLORREF background, int foregroundPercent) {
    const auto mix = [&](int f, int b) { return (f * foregroundPercent + b * (100 - foregroundPercent)) / 100; };
    return RGB(mix(GetRValue(foreground), GetRValue(background)),
               mix(GetGValue(foreground), GetGValue(background)),
               mix(GetBValue(foreground), GetBValue(background)));
}

void DrawTriangle(HDC dc, int cx, int cy, int half, bool up, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    const int h = std::max(1, half / 2 + 1);
    const POINT points[3] = {{cx - half, up ? cy + h : cy - h}, {cx + half + 1, up ? cy + h : cy - h},
                             {cx, up ? cy - h : cy + h}};
    Polygon(dc, points, 3);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(brush);
}

// The text as painted in a cell: capped in length, with line breaks shown as pilcrows.
std::wstring DisplayText(std::string_view text) {
    if (text.size() > kMaxDisplayBytes) {
        std::size_t cut = kMaxDisplayBytes;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        text = text.substr(0, cut);
    }
    const std::wstring wide = Utf8ToWide(text);
    std::wstring shown;
    shown.reserve(wide.size());
    for (std::size_t i = 0; i < wide.size(); ++i) {
        if (wide[i] == L'\r' && i + 1 < wide.size() && wide[i + 1] == L'\n') {
            continue;
        }
        shown.push_back(wide[i] == L'\r' || wide[i] == L'\n' ? L'\u00B6' : wide[i]);
    }
    return shown;
}

}  // namespace

// ---------------------------------------------------------------- painting

void GridWindow::DrawUtf8(HDC dc, std::string_view text, RECT rect, UINT flags) {
    const std::wstring shown = DisplayText(text);
    rect.left += app_.m.padding;
    rect.right -= app_.m.padding;
    DrawTextW(dc, shown.c_str(), static_cast<int>(shown.size()), &rect,
              flags | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

int GridWindow::FitWidth(HDC dc, int col, int rows) const {
    const Metrics& m = app_.m;
    // The header must fit the label, the filter button and the sort arrow beside it.
    const std::wstring label = ColumnName(col);
    SIZE size{};
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &size);
    int width = size.cx + app_.Scale(20) + app_.Scale(12) + 2 * m.padding;
    for (int row = 0; row < rows; ++row) {
        const std::string& value = app_.doc.Cell(app_.DocRowOf(row), static_cast<std::size_t>(col));
        if (value.empty()) {
            continue;
        }
        const std::wstring shown = DisplayText(value);
        GetTextExtentPoint32W(dc, shown.c_str(), static_cast<int>(shown.size()), &size);
        width = std::max(width, static_cast<int>(size.cx) + 2 * m.padding + 1);
    }
    return width;
}

void GridWindow::AutofitColumns(int left, int right, int toggleCol) {
    const int lastData = static_cast<int>(app_.doc.ColumnCount()) - 1;
    left = std::max(left, 0);
    right = std::min(right, lastData);
    if (left > right) {
        return;
    }
    editor.Commit();

    HDC dc = GetDC(*this);
    HGDIOBJ oldFont = SelectObject(dc, app_.font);
    const int rows = std::min(app_.DataRows(), kAutofitMaxRows);

    // Toggle: a column that is not at its fit width is fitted; one that already is goes back to the default.
    bool reset = false;
    if (toggleCol >= 0 && toggleCol <= lastData) {
        const int fit = std::clamp(FitWidth(dc, toggleCol, rows), app_.Scale(kMinColumnWidth),
                                   app_.Scale(kMaxColumnWidth));
        reset = MulDiv(fit, 96, app_.m.dpi) == MulDiv(ColWidth(toggleCol), 96, app_.m.dpi);
    }

    bool changed = false;
    for (int col = left; col <= right; ++col) {
        changed |= StoreColumnWidth(col, reset ? app_.m.columnWidth : FitWidth(dc, col, rows));
    }
    SelectObject(dc, oldFont);
    ReleaseDC(*this, dc);

    if (changed) {
        UpdateScrollbars();
        Redraw();
        app_.UpdateTitle();
    }
}

RECT GridWindow::FilterButtonRect(int col) const {
    const Metrics& m = app_.m;
    const int x = ColLeft(col);
    const int width = ColWidth(col);
    return {x + width - app_.Scale(20) - 1, 1, x + width - 1, m.headerHeight - 1};
}

void GridWindow::DrawGrid(HDC dc, const RECT& client) {
    const Metrics& m = app_.m;
    const Document& doc = app_.doc;
    const RowView& view = app_.view;
    const auto& matcher = app_.findBar.Matcher();
    const COLORREF windowColor = GetSysColor(COLOR_WINDOW);
    const COLORREF textColor = GetSysColor(COLOR_WINDOWTEXT);
    const COLORREF accent = GetSysColor(COLOR_HIGHLIGHT);
    const COLORREF errorColor = RGB(192, 0, 0);

    HBRUSH windowBrush = CreateSolidBrush(windowColor);
    HBRUSH gridBrush = CreateSolidBrush(Blend(textColor, windowColor, 16));
    HBRUSH headerBrush = CreateSolidBrush(Blend(textColor, windowColor, 5));
    HBRUSH headerSelectedBrush = CreateSolidBrush(Blend(accent, windowColor, 22));
    HBRUSH spareBrush = CreateSolidBrush(Blend(textColor, windowColor, 3));
    HBRUSH rangeBrush = CreateSolidBrush(Blend(accent, windowColor, 14));
    HBRUSH accentBrush = CreateSolidBrush(accent);
    HBRUSH findBrush = CreateSolidBrush(Blend(RGB(255, 200, 0), windowColor, 45));
    HBRUSH filterBrush = CreateSolidBrush(Blend(accent, windowColor, 30));
    HBRUSH titleRowBrush = CreateSolidBrush(Blend(textColor, windowColor, 9));
    const COLORREF glyphColor = Blend(textColor, windowColor, 55);

    FillRect(dc, &client, windowBrush);
    SelectObject(dc, app_.font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, textColor);

    const int right = static_cast<int>(client.right);
    const int gridBottom = static_cast<int>(client.bottom);
    const CellRange sel = app_.Selection();
    const int lastCol = std::min(app_.GridCols() - 1, firstCol_ + VisibleCols(client));
    const int lastRow = std::min(app_.GridRows() - 1, firstRow_ + (gridBottom - m.headerHeight) / m.rowHeight + 1);
    const int dataRows = app_.DataRows();
    const int dataCols = static_cast<int>(doc.ColumnCount());
    const bool highlightFinds = app_.findBar.Visible() && matcher && matcher->Valid() && !matcher->Empty();

    SaveDC(dc);
    IntersectClipRect(dc, m.rowNumberWidth, m.headerHeight, right, gridBottom);
    for (int row = firstRow_; row <= lastRow; ++row) {
        const std::size_t docRow = app_.DocRowOf(row);
        const bool titleRow = row < dataRows && docRow < view.headerRows;
        for (int col = firstCol_; col <= lastCol; ++col) {
            const RECT cell = CellRect({row, col});
            const bool inRange = row >= sel.top && row <= sel.bottom && col >= sel.left && col <= sel.right;
            const bool isActive = row == app_.active.row && col == app_.active.col;
            const bool spare = row >= dataRows || col >= dataCols;
            const std::string& value = doc.Cell(docRow, static_cast<std::size_t>(col));
            const bool found = highlightFinds && !spare && matcher->Matches(value);
            HBRUSH background = windowBrush;
            if (inRange && !isActive) {
                background = rangeBrush;
            } else if (found) {
                background = findBrush;
            } else if (spare) {
                background = spareBrush;
            } else if (titleRow) {
                background = titleRowBrush;
            }
            FillRect(dc, &cell, background);

            const RECT vertical{cell.right - 1, cell.top, cell.right, cell.bottom};
            const RECT horizontal{cell.left, cell.bottom - 1, cell.right, cell.bottom};
            FillRect(dc, &vertical, gridBrush);
            FillRect(dc, &horizontal, gridBrush);

            const bool isFormula = !doc.Formula(docRow, static_cast<std::size_t>(col)).empty();
            if (!value.empty()) {
                const bool isError = isFormula && value.front() == '#';
                if (isError) {
                    SetTextColor(dc, errorColor);
                }
                DrawUtf8(dc, value, cell, DT_LEFT);
                if (isError) {
                    SetTextColor(dc, textColor);
                }
            }
            if (isFormula) {
                const int size = app_.Scale(6);
                for (int i = 0; i < size; ++i) {
                    const RECT bar{cell.right - 1 - (size - i), cell.top + i, cell.right - 1, cell.top + i + 1};
                    FillRect(dc, &bar, accentBrush);
                }
            }
        }
    }

    RECT outline = CellRect({sel.top, sel.left});
    const RECT last = CellRect({sel.bottom, sel.right});
    outline.right = last.right;
    outline.bottom = last.bottom;
    FrameRect(dc, &outline, accentBrush);
    InflateRect(&outline, -1, -1);
    FrameRect(dc, &outline, accentBrush);
    RestoreDC(dc, -1);

    for (int col = firstCol_; col <= lastCol; ++col) {
        const int x = ColLeft(col);
        const RECT header{x, 0, x + ColWidth(col), m.headerHeight};
        FillRect(dc, &header, col >= sel.left && col <= sel.right ? headerSelectedBrush : headerBrush);
        const RECT edge{header.right - 1, 0, header.right, header.bottom};
        FillRect(dc, &edge, gridBrush);
        const std::wstring label = ColumnName(col);
        RECT labelRect = header;
        labelRect.right -= col < dataCols ? app_.Scale(20) : 0;
        DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &labelRect,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

        if (col < dataCols) {
            const RECT button = FilterButtonRect(col);
            const bool filtered = view.filters.count(static_cast<std::size_t>(col)) != 0;
            if (filtered) {
                FillRect(dc, &button, filterBrush);
            }
            DrawTriangle(dc, (button.left + button.right) / 2, (button.top + button.bottom) / 2, app_.Scale(4), false,
                         filtered ? accent : glyphColor);
            for (const SortLevel& level : view.sorts) {
                if (level.column == static_cast<std::size_t>(col)) {
                    DrawTriangle(dc, button.left - app_.Scale(9), (button.top + button.bottom) / 2, app_.Scale(4),
                                 !level.descending, accent);
                }
            }
        }
    }
    const RECT headerLine{m.rowNumberWidth, m.headerHeight - 1, right, m.headerHeight};
    FillRect(dc, &headerLine, gridBrush);

    SaveDC(dc);
    IntersectClipRect(dc, 0, m.headerHeight, m.rowNumberWidth, gridBottom);
    for (int row = firstRow_; row <= lastRow; ++row) {
        const int y = m.headerHeight + (row - firstRow_) * m.rowHeight;
        const RECT header{0, y, m.rowNumberWidth, y + m.rowHeight};
        FillRect(dc, &header, row >= sel.top && row <= sel.bottom ? headerSelectedBrush : headerBrush);
        const RECT edge{0, header.bottom - 1, header.right, header.bottom};
        FillRect(dc, &edge, gridBrush);
        const std::wstring label = std::to_wstring(app_.DocRowOf(row) + 1);
        RECT labelRect = header;
        if (view.Active() && row < dataRows) {
            SetTextColor(dc, accent);
        }
        DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &labelRect,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        SetTextColor(dc, textColor);
    }
    RestoreDC(dc, -1);
    const RECT rowLine{m.rowNumberWidth - 1, m.headerHeight, m.rowNumberWidth, gridBottom};
    FillRect(dc, &rowLine, gridBrush);

    const RECT corner{0, 0, m.rowNumberWidth, m.headerHeight};
    FillRect(dc, &corner, headerBrush);
    const RECT cornerBottom{0, m.headerHeight - 1, m.rowNumberWidth, m.headerHeight};
    const RECT cornerRight{m.rowNumberWidth - 1, 0, m.rowNumberWidth, m.headerHeight};
    FillRect(dc, &cornerBottom, gridBrush);
    FillRect(dc, &cornerRight, gridBrush);

    for (HBRUSH brush : {windowBrush, gridBrush, headerBrush, headerSelectedBrush, spareBrush, rangeBrush, accentBrush,
                         findBrush, filterBrush, titleRowBrush}) {
        DeleteObject(brush);
    }
}

void GridWindow::Paint() {
    PAINTSTRUCT paint{};
    HDC screen = BeginPaint(*this, &paint);
    RECT client{};
    GetClientRect(*this, &client);
    const int width = client.right;
    const int height = client.bottom;

    if (width > 0 && height > 0) {
        if (!backBitmap_ || backSize_.cx != width || backSize_.cy != height) {
            if (backBitmap_) {
                DeleteObject(backBitmap_);
            }
            backBitmap_ = CreateCompatibleBitmap(screen, width, height);
            backSize_ = {width, height};
        }
        HDC memory = CreateCompatibleDC(screen);
        HGDIOBJ old = SelectObject(memory, backBitmap_);
        DrawGrid(memory, client);
        BitBlt(screen, paint.rcPaint.left, paint.rcPaint.top, paint.rcPaint.right - paint.rcPaint.left,
               paint.rcPaint.bottom - paint.rcPaint.top, memory, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
        SelectObject(memory, old);
        DeleteDC(memory);
    }
    EndPaint(*this, &paint);
}

}  // namespace app
