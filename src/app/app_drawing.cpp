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

constexpr std::size_t kMaxDisplayBytes = 1024;

// ---------------------------------------------------------------- painting

void DrawUtf8(HDC dc, std::string_view text, RECT rect, UINT flags) {
    if (text.size() > kMaxDisplayBytes) {
        std::size_t cut = kMaxDisplayBytes;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        text = text.substr(0, cut);
    }
    std::wstring wide = Utf8ToWide(text);
    std::wstring shown;
    shown.reserve(wide.size());
    for (std::size_t i = 0; i < wide.size(); ++i) {
        if (wide[i] == L'\r' && i + 1 < wide.size() && wide[i + 1] == L'\n') {
            continue;
        }
        shown.push_back(wide[i] == L'\r' || wide[i] == L'\n' ? L'\u00B6' : wide[i]);
    }
    rect.left += g.m.padding;
    rect.right -= g.m.padding;
    DrawTextW(dc, shown.c_str(), static_cast<int>(shown.size()), &rect,
              flags | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
}

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

RECT FilterButtonRect(int col) {
    const int x = g.m.rowNumberWidth + (col - g.firstCol) * g.m.columnWidth;
    return {x + g.m.columnWidth - Scale(20) - 1, 1, x + g.m.columnWidth - 1, g.m.headerHeight - 1};
}

void DrawGrid(HDC dc, const RECT& client) {
    const Metrics& m = g.m;
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
    SelectObject(dc, g.font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, textColor);

    const int right = static_cast<int>(client.right);
    const int gridBottom = static_cast<int>(client.bottom);
    const CellRange sel = Selection();
    const int lastCol = std::min(GridCols() - 1, g.firstCol + (right - m.rowNumberWidth) / m.columnWidth + 1);
    const int lastRow = std::min(GridRows() - 1, g.firstRow + (gridBottom - m.headerHeight) / m.rowHeight + 1);
    const int dataRows = DataRows();
    const int dataCols = static_cast<int>(g.doc.ColumnCount());
    const bool highlightFinds = g.findVisible && g.matcher && g.matcher->Valid() && !g.matcher->Empty();

    SaveDC(dc);
    IntersectClipRect(dc, m.rowNumberWidth, m.headerHeight, right, gridBottom);
    for (int row = g.firstRow; row <= lastRow; ++row) {
        const std::size_t docRow = DocRowOf(row);
        const bool titleRow = row < dataRows && docRow < g.view.headerRows;
        for (int col = g.firstCol; col <= lastCol; ++col) {
            const RECT cell = CellRect({row, col});
            const bool inRange = row >= sel.top && row <= sel.bottom && col >= sel.left && col <= sel.right;
            const bool isActive = row == g.active.row && col == g.active.col;
            const bool spare = row >= dataRows || col >= dataCols;
            const std::string& value = g.doc.Cell(docRow, static_cast<std::size_t>(col));
            const bool found = highlightFinds && !spare && g.matcher->Matches(value);
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

            const bool isFormula = !g.doc.Formula(docRow, static_cast<std::size_t>(col)).empty();
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
                const int size = Scale(6);
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

    for (int col = g.firstCol; col <= lastCol; ++col) {
        const int x = m.rowNumberWidth + (col - g.firstCol) * m.columnWidth;
        const RECT header{x, 0, x + m.columnWidth, m.headerHeight};
        FillRect(dc, &header, col >= sel.left && col <= sel.right ? headerSelectedBrush : headerBrush);
        const RECT edge{header.right - 1, 0, header.right, header.bottom};
        FillRect(dc, &edge, gridBrush);
        const std::wstring label = ColumnName(col);
        RECT labelRect = header;
        labelRect.right -= col < dataCols ? Scale(20) : 0;
        DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &labelRect,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);

        if (col < dataCols) {
            const RECT button = FilterButtonRect(col);
            const bool filtered = g.view.filters.count(static_cast<std::size_t>(col)) != 0;
            if (filtered) {
                FillRect(dc, &button, filterBrush);
            }
            DrawTriangle(dc, (button.left + button.right) / 2, (button.top + button.bottom) / 2, Scale(4), false,
                         filtered ? accent : glyphColor);
            for (const SortLevel& level : g.view.sorts) {
                if (level.column == static_cast<std::size_t>(col)) {
                    DrawTriangle(dc, button.left - Scale(9), (button.top + button.bottom) / 2, Scale(4),
                                 !level.descending, accent);
                }
            }
        }
    }
    const RECT headerLine{m.rowNumberWidth, m.headerHeight - 1, right, m.headerHeight};
    FillRect(dc, &headerLine, gridBrush);

    SaveDC(dc);
    IntersectClipRect(dc, 0, m.headerHeight, m.rowNumberWidth, gridBottom);
    for (int row = g.firstRow; row <= lastRow; ++row) {
        const int y = m.headerHeight + (row - g.firstRow) * m.rowHeight;
        const RECT header{0, y, m.rowNumberWidth, y + m.rowHeight};
        FillRect(dc, &header, row >= sel.top && row <= sel.bottom ? headerSelectedBrush : headerBrush);
        const RECT edge{0, header.bottom - 1, header.right, header.bottom};
        FillRect(dc, &edge, gridBrush);
        const std::wstring label = std::to_wstring(DocRowOf(row) + 1);
        RECT labelRect = header;
        if (g.view.Active() && row < dataRows) {
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

void PaintWindow(HWND window) {
    PAINTSTRUCT paint{};
    HDC screen = BeginPaint(window, &paint);
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right;
    const int height = client.bottom;

    if (width > 0 && height > 0) {
        if (!g.backBitmap || g.backSize.cx != width || g.backSize.cy != height) {
            if (g.backBitmap) {
                DeleteObject(g.backBitmap);
            }
            g.backBitmap = CreateCompatibleBitmap(screen, width, height);
            g.backSize = {width, height};
        }
        HDC memory = CreateCompatibleDC(screen);
        HGDIOBJ old = SelectObject(memory, g.backBitmap);
        DrawGrid(memory, client);
        BitBlt(screen, paint.rcPaint.left, paint.rcPaint.top, paint.rcPaint.right - paint.rcPaint.left,
               paint.rcPaint.bottom - paint.rcPaint.top, memory, paint.rcPaint.left, paint.rcPaint.top, SRCCOPY);
        SelectObject(memory, old);
        DeleteDC(memory);
    }
    EndPaint(window, &paint);
}

}
