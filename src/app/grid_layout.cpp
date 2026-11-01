#include "grid_window.h"

#include <algorithm>

#include "app.h"

using namespace sheetpad;

namespace app {

// ---------------------------------------------------------------- layout

bool GridWindow::Create(HWND parent, HINSTANCE instance) {
    return WindowHandle::Create(0, kWindowClass, L"", WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL, 0, 0, 0, 0,
                                parent, nullptr, instance);
}

int GridWindow::VisibleRows(const RECT& client) const {
    const Metrics& m = app_.m;
    return std::max(1, (static_cast<int>(client.bottom) - m.headerHeight) / m.rowHeight);
}

int GridWindow::VisibleCols(const RECT& client) const {
    const Metrics& m = app_.m;
    return std::max(1, (static_cast<int>(client.right) - m.rowNumberWidth) / m.columnWidth);
}

RECT GridWindow::CellRect(Pos p) const {
    const Metrics& m = app_.m;
    const int x = m.rowNumberWidth + (p.col - firstCol_) * m.columnWidth;
    const int y = m.headerHeight + (p.row - firstRow_) * m.rowHeight;
    return {x, y, x + m.columnWidth, y + m.rowHeight};
}

void GridWindow::UpdateScrollbars() {
    RECT client{};
    GetClientRect(*this, &client);

    const int visibleRows = VisibleRows(client);
    firstRow_ = std::clamp(firstRow_, 0, std::max(0, app_.GridRows() - visibleRows));
    SCROLLINFO vertical{};
    vertical.cbSize = sizeof(vertical);
    vertical.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    vertical.nMin = 0;
    vertical.nMax = app_.GridRows() - 1;
    vertical.nPage = static_cast<UINT>(visibleRows);
    vertical.nPos = firstRow_;
    SetScrollInfo(*this, SB_VERT, &vertical, TRUE);

    const int visibleCols = VisibleCols(client);
    firstCol_ = std::clamp(firstCol_, 0, std::max(0, app_.GridCols() - visibleCols));
    SCROLLINFO horizontal{};
    horizontal.cbSize = sizeof(horizontal);
    horizontal.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    horizontal.nMin = 0;
    horizontal.nMax = app_.GridCols() - 1;
    horizontal.nPage = static_cast<UINT>(visibleCols);
    horizontal.nPos = firstCol_;
    SetScrollInfo(*this, SB_HORZ, &horizontal, TRUE);
}

void GridWindow::EnsureVisible(Pos p) {
    RECT client{};
    GetClientRect(*this, &client);
    const int visibleRows = VisibleRows(client);
    const int visibleCols = VisibleCols(client);
    if (p.row < firstRow_) {
        firstRow_ = p.row;
    } else if (p.row >= firstRow_ + visibleRows) {
        firstRow_ = p.row - visibleRows + 1;
    }
    if (p.col < firstCol_) {
        firstCol_ = p.col;
    } else if (p.col >= firstCol_ + visibleCols) {
        firstCol_ = p.col - visibleCols + 1;
    }
    UpdateScrollbars();
}

void GridWindow::Redraw() {
    InvalidateRect(*this, nullptr, FALSE);
    app_.frame.UpdateStatus();
}

void GridWindow::ResetScroll() {
    firstRow_ = 0;
    firstCol_ = 0;
}

}  // namespace app
