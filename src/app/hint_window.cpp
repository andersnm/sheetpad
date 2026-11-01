#include "hint_window.h"

#include <algorithm>

#include "app.h"

namespace app {

namespace {

constexpr std::size_t kMaxVisibleRows = 8;

}  // namespace

HintWindow::~HintWindow() {
    if (boldFont_) {
        DeleteObject(boldFont_);
    }
}

bool HintWindow::EnsureCreated() {
    if (hwnd_) {
        return true;
    }
    LOGFONTW lf{};
    GetObjectW(app_.font, sizeof(lf), &lf);
    lf.lfWeight = FW_BOLD;
    boldFont_ = CreateFontIndirectW(&lf);
    return Create(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kHintClass, L"", WS_POPUP | WS_BORDER, 0, 0, 10, 10,
                  app_.frame, nullptr, GetModuleHandleW(nullptr));
}

void HintWindow::Place(int width, int height, POINT anchor, bool anchorIsBottom) {
    RECT bounds{0, 0, width, height};
    AdjustWindowRectEx(&bounds, WS_POPUP | WS_BORDER, FALSE, WS_EX_TOOLWINDOW);
    const int outerW = bounds.right - bounds.left;
    const int outerH = bounds.bottom - bounds.top;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    const int x = std::max<int>(monitor.rcWork.left, std::min<int>(anchor.x, monitor.rcWork.right - outerW));
    int y = anchorIsBottom ? anchor.y - outerH : anchor.y;
    y = std::max<int>(monitor.rcWork.top, std::min<int>(y, monitor.rcWork.bottom - outerH));
    SetWindowPos(*this, HWND_TOP, x, y, outerW, outerH, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(*this, nullptr, TRUE);
}

void HintWindow::ShowList(std::vector<Item> items, POINT anchor) {
    if (!EnsureCreated()) {
        return;
    }
    items_ = std::move(items);
    selected_ = 0;
    top_ = 0;
    const int rows = static_cast<int>(std::min(items_.size(), kMaxVisibleRows));
    Place(app_.Scale(420), rows * app_.m.rowHeight, anchor, false);
}

void HintWindow::ShowTip(const std::wstring& text, std::size_t boldStart, std::size_t boldLength, POINT anchor) {
    if (!EnsureCreated()) {
        return;
    }
    text_ = text;
    boldStart_ = boldStart;
    boldLength_ = boldLength;
    HDC dc = GetDC(*this);
    HGDIOBJ old = SelectObject(dc, boldFont_);
    SIZE bold{};
    GetTextExtentPoint32W(dc, text_.c_str(), static_cast<int>(text_.size()), &bold);
    SelectObject(dc, old);
    ReleaseDC(*this, dc);
    Place(bold.cx + 2 * app_.m.padding, app_.m.rowHeight, anchor, true);
}

void HintWindow::Hide() {
    if (hwnd_) {
        ShowWindow(*this, SW_HIDE);
    }
}

void HintWindow::EnsureVisible() {
    if (selected_ < top_) {
        top_ = selected_;
    } else if (selected_ >= top_ + kMaxVisibleRows) {
        top_ = selected_ + 1 - kMaxVisibleRows;
    }
}

void HintWindow::MoveSelection(int delta) {
    if (items_.empty()) {
        return;
    }
    const int count = static_cast<int>(items_.size());
    selected_ = static_cast<std::size_t>((static_cast<int>(selected_) + delta + count) % count);
    EnsureVisible();
    InvalidateRect(*this, nullptr, FALSE);
}

void HintWindow::Paint() {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(*this, &ps);
    RECT client{};
    GetClientRect(*this, &client);
    const int background = isList_ ? COLOR_WINDOW : COLOR_INFOBK;
    const int foreground = isList_ ? COLOR_WINDOWTEXT : COLOR_INFOTEXT;
    FillRect(dc, &client, GetSysColorBrush(background));
    SetBkMode(dc, TRANSPARENT);
    const int pad = app_.m.padding;
    const int rowH = app_.m.rowHeight;
    HGDIOBJ oldFont = SelectObject(dc, app_.font);

    if (isList_) {
        const int nameWidth = app_.Scale(120);
        const std::size_t last = std::min(items_.size(), top_ + kMaxVisibleRows);
        for (std::size_t i = top_; i < last; ++i) {
            RECT row{0, static_cast<LONG>(i - top_) * rowH, client.right, static_cast<LONG>(i - top_ + 1) * rowH};
            const bool selected = i == selected_;
            if (selected) {
                FillRect(dc, &row, GetSysColorBrush(COLOR_HIGHLIGHT));
            }
            SetTextColor(dc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
            RECT nameRect{row.left + pad, row.top, row.left + pad + nameWidth, row.bottom};
            DrawTextW(dc, items_[i].name.c_str(), -1, &nameRect,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
            SetTextColor(dc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_GRAYTEXT));
            RECT descRect{nameRect.right + pad, row.top, row.right - pad, row.bottom};
            DrawTextW(dc, items_[i].description.c_str(), -1, &descRect,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
    } else {
        SetTextColor(dc, GetSysColor(foreground));
        const std::size_t boldEnd = std::min(text_.size(), boldStart_ + boldLength_);
        const std::size_t boldBegin = std::min(boldStart_, boldEnd);
        int x = pad;
        const auto draw = [&](HFONT font, std::size_t from, std::size_t to) {
            if (from >= to) {
                return;
            }
            SelectObject(dc, font);
            SIZE size{};
            GetTextExtentPoint32W(dc, text_.c_str() + from, static_cast<int>(to - from), &size);
            RECT cell{x, 0, x + size.cx, client.bottom};
            DrawTextW(dc, text_.c_str() + from, static_cast<int>(to - from), &cell,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_NOCLIP);
            x += size.cx;
        };
        draw(app_.font, 0, boldBegin);
        draw(boldFont_, boldBegin, boldEnd);
        draw(app_.font, boldEnd, text_.size());
    }
    SelectObject(dc, oldFont);
    EndPaint(*this, &ps);
}

LRESULT HintWindow::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (isList_) {
            const std::size_t index = top_ + static_cast<std::size_t>(GET_Y_LPARAM(lParam) / app_.m.rowHeight);
            if (index < items_.size()) {
                selected_ = index;
                InvalidateRect(*this, nullptr, FALSE);
                if (message == WM_LBUTTONDBLCLK && acceptHandler_) {
                    acceptHandler_(acceptContext_, index);
                }
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (isList_) {
            MoveSelection(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1);
        }
        return 0;
    default:
        break;
    }
    return DefaultProc(message, wParam, lParam);
}

}  // namespace app
