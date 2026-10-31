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

// ---------------------------------------------------------------- layout

void CreateFonts(HWND window) {
    HDC dc = GetDC(window);
    g.m.dpi = GetDeviceCaps(dc, LOGPIXELSY);

    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    LOGFONTW logFont{};
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        logFont = metrics.lfMessageFont;
    } else {
        logFont.lfHeight = -MulDiv(9, g.m.dpi, 72);
        wcscpy(logFont.lfFaceName, L"Segoe UI");
    }
    g.font = CreateFontIndirectW(&logFont);

    HGDIOBJ old = SelectObject(dc, g.font);
    TEXTMETRICW text{};
    GetTextMetricsW(dc, &text);
    SelectObject(dc, old);
    ReleaseDC(window, dc);

    g.m.textHeight = text.tmHeight;
    g.m.padding = Scale(6);
    g.m.rowHeight = text.tmHeight + Scale(10);
    g.m.headerHeight = g.m.rowHeight;
    g.m.rowNumberWidth = Scale(56);
    g.m.columnWidth = Scale(140);
}

int VisibleRows(const RECT& client) {
    return std::max(1, (static_cast<int>(client.bottom) - g.m.headerHeight) / g.m.rowHeight);
}

int VisibleCols(const RECT& client) {
    return std::max(1, (static_cast<int>(client.right) - g.m.rowNumberWidth) / g.m.columnWidth);
}

RECT CellRect(Pos p) {
    const int x = g.m.rowNumberWidth + (p.col - g.firstCol) * g.m.columnWidth;
    const int y = g.m.headerHeight + (p.row - g.firstRow) * g.m.rowHeight;
    return {x, y, x + g.m.columnWidth, y + g.m.rowHeight};
}

void UpdateScrollbars(HWND window) {
    RECT client{};
    GetClientRect(window, &client);

    const int visibleRows = VisibleRows(client);
    g.firstRow = std::clamp(g.firstRow, 0, std::max(0, GridRows() - visibleRows));
    SCROLLINFO vertical{};
    vertical.cbSize = sizeof(vertical);
    vertical.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    vertical.nMin = 0;
    vertical.nMax = GridRows() - 1;
    vertical.nPage = static_cast<UINT>(visibleRows);
    vertical.nPos = g.firstRow;
    SetScrollInfo(window, SB_VERT, &vertical, TRUE);

    const int visibleCols = VisibleCols(client);
    g.firstCol = std::clamp(g.firstCol, 0, std::max(0, GridCols() - visibleCols));
    SCROLLINFO horizontal{};
    horizontal.cbSize = sizeof(horizontal);
    horizontal.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    horizontal.nMin = 0;
    horizontal.nMax = GridCols() - 1;
    horizontal.nPage = static_cast<UINT>(visibleCols);
    horizontal.nPos = g.firstCol;
    SetScrollInfo(window, SB_HORZ, &horizontal, TRUE);
}

void EnsureVisible(HWND window, Pos p) {
    RECT client{};
    GetClientRect(window, &client);
    const int visibleRows = VisibleRows(client);
    const int visibleCols = VisibleCols(client);
    if (p.row < g.firstRow) {
        g.firstRow = p.row;
    } else if (p.row >= g.firstRow + visibleRows) {
        g.firstRow = p.row - visibleRows + 1;
    }
    if (p.col < g.firstCol) {
        g.firstCol = p.col;
    } else if (p.col >= g.firstCol + visibleCols) {
        g.firstCol = p.col - visibleCols + 1;
    }
    UpdateScrollbars(window);
}

void Redraw(HWND window) {
    InvalidateRect(window, nullptr, FALSE);
    UpdateStatus();
}
}
