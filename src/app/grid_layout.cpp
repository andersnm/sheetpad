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

int GridWindow::ColWidth(int col) const {
    const auto it = app_.view.columnWidths.find(static_cast<std::size_t>(col));
    return it == app_.view.columnWidths.end() ? app_.m.columnWidth : app_.Scale(it->second);
}

int GridWindow::ColLeft(int col) const {
    int x = app_.m.rowNumberWidth;
    for (int c = firstCol_; c < col; ++c) {
        x += ColWidth(c);
    }
    for (int c = col; c < firstCol_; ++c) {
        x -= ColWidth(c);
    }
    return x;
}

int GridWindow::VisibleCols(const RECT& client) const {
    const int available = static_cast<int>(client.right) - app_.m.rowNumberWidth;
    int used = 0;
    int count = 0;
    for (int col = firstCol_; col < app_.GridCols(); ++col) {
        used += ColWidth(col);
        if (used > available) {
            break;
        }
        ++count;
    }
    return std::max(1, count);
}

// The largest first column that still leaves no unused space to the right of the last column.
int GridWindow::MaxFirstCol(const RECT& client) const {
    const int available = static_cast<int>(client.right) - app_.m.rowNumberWidth;
    int first = app_.GridCols() - 1;
    int used = ColWidth(first);
    while (first > 0 && used + ColWidth(first - 1) <= available) {
        used += ColWidth(--first);
    }
    return first;
}

RECT GridWindow::CellRect(Pos p) const {
    const Metrics& m = app_.m;
    const int x = ColLeft(p.col);
    const int y = m.headerHeight + (p.row - firstRow_) * m.rowHeight;
    return {x, y, x + ColWidth(p.col), y + m.rowHeight};
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

    const int maxFirstCol = MaxFirstCol(client);
    firstCol_ = std::clamp(firstCol_, 0, maxFirstCol);
    SCROLLINFO horizontal{};
    horizontal.cbSize = sizeof(horizontal);
    horizontal.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    horizontal.nMin = 0;
    horizontal.nMax = app_.GridCols() - 1;
    horizontal.nPage = static_cast<UINT>(app_.GridCols() - maxFirstCol);
    horizontal.nPos = firstCol_;
    SetScrollInfo(*this, SB_HORZ, &horizontal, TRUE);
}

void GridWindow::EnsureVisible(Pos p) {
    RECT client{};
    GetClientRect(*this, &client);
    const int visibleRows = VisibleRows(client);
    if (p.row < firstRow_) {
        firstRow_ = p.row;
    } else if (p.row >= firstRow_ + visibleRows) {
        firstRow_ = p.row - visibleRows + 1;
    }
    if (p.col < firstCol_) {
        firstCol_ = p.col;
    } else {
        while (firstCol_ < p.col && ColLeft(p.col) + ColWidth(p.col) > client.right) {
            ++firstCol_;
        }
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
