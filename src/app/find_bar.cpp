#include "find_bar.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include "app.h"
#include "text_encoding.h"

using namespace sheetpad;

namespace app {

// ---------------------------------------------------------------- find bar

LRESULT FindEdit::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_KEYDOWN && wParam == 'A' && KeyDown(VK_CONTROL)) {
        SendMessageW(*this, EM_SETSEL, 0, -1);
        return 0;
    }
    // Ctrl+A would otherwise beep.
    if (message == WM_CHAR && wParam == 1) {
        return 0;
    }
    return DefaultProc(message, wParam, lParam);
}

void FindBar::Create(HWND frame, HINSTANCE instance) {
    dialog_.Attach(CreateDialogParamW(instance, MAKEINTRESOURCEW(IDD_FIND), frame, DialogProc,
                                      reinterpret_cast<LPARAM>(this)));
    const auto bind = [&](WindowHandle& control, int id) {
        control.Attach(GetDlgItem(dialog_, id));
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app_.font), TRUE);
    };
    bind(label_, IDC_FIND_LABEL);
    edit_.Subclass(GetDlgItem(dialog_, IDC_FIND_EDIT));
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(app_.font), TRUE);
    bind(previous_, IDC_FIND_PREV);
    bind(next_, IDC_FIND_NEXT);
    bind(matchCase_, IDC_FIND_CASE);
    bind(wholeCell_, IDC_FIND_WHOLE);
    bind(regex_, IDC_FIND_REGEX);
    bind(thisColumn_, IDC_FIND_COLUMN);
    bind(result_, IDC_FIND_RESULT);
    bind(close_, IDC_FIND_CLOSE);
}

INT_PTR CALLBACK FindBar::DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);
        return FALSE;  // the find bar is created hidden and must not take the focus
    }
    auto* self = reinterpret_cast<FindBar*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (self && message == WM_COMMAND) {
        self->HandleCommand(LOWORD(wParam), HIWORD(wParam));
        return TRUE;
    }
    return FALSE;
}

bool FindBar::PreTranslate(MSG& message) {
    if (!visible_) {
        return false;
    }
    if (IsChild(dialog_, message.hwnd)) {
        return IsDialogMessageW(dialog_, &message) != FALSE;
    }
    if (message.message == WM_SYSCHAR) {
        // Lets a mnemonic work from the grid; IsDialogMessage only looks at messages for its own controls.
        MSG routed = message;
        routed.hwnd = edit_;
        return IsDialogMessageW(dialog_, &routed) != FALSE;
    }
    return false;
}

void FindBar::SetResult(const std::wstring& text) {
    SetWindowTextW(result_, text.c_str());
}

bool FindBar::Checked(const WindowHandle& checkbox) {
    return SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void FindBar::UpdateMatcher() {
    FindOptions options;
    options.text = WideToUtf8(WindowText(edit_));
    options.matchCase = Checked(matchCase_);
    options.wholeCell = Checked(wholeCell_);
    options.regex = Checked(regex_);
    columnOnly_ = Checked(thisColumn_);
    matcher_.emplace(options);
    InvalidateRect(app_.grid, nullptr, FALSE);
}

void FindBar::Show(bool show) {
    if (show == visible_ && !show) {
        return;
    }
    visible_ = show;
    ShowWindow(dialog_, show ? SW_SHOW : SW_HIDE);
    app_.frame.Layout();
    if (show) {
        SetFocus(edit_);
        SendMessageW(edit_, EM_SETSEL, 0, -1);
        UpdateMatcher();
    } else {
        matcher_.reset();
        SetFocus(app_.grid);
    }
    InvalidateRect(app_.grid, nullptr, FALSE);
}

int FindBar::Height() const {
    return app_.m.rowHeight + app_.Scale(8);
}

// Places the dialog in the strip starting at `top` and the controls in a row inside it.
void FindBar::Layout(int top, int width) {
    MoveWindow(dialog_, 0, top, width, Height(), TRUE);
    const int y = app_.Scale(4);
    const int h = app_.m.rowHeight;
    int x = app_.Scale(6);
    const std::array<std::pair<const WindowHandle*, int>, 10> row{{
        {&label_, 40},
        {&edit_, 220},
        {&previous_, 56},
        {&next_, 56},
        {&matchCase_, 96},
        {&wholeCell_, 92},
        {&regex_, 64},
        {&thisColumn_, 104},
        {&result_, 110},
        {&close_, 60},
    }};
    for (const auto& [control, pixels] : row) {
        const int w = app_.Scale(pixels);
        MoveWindow(*control, x, y, w, h, TRUE);
        x += w + app_.Scale(6);
    }
}

bool FindBar::FindNext(bool forward, bool includeCurrent) {
    if (!matcher_ || matcher_->Empty()) {
        SetResult(L"");
        return false;
    }
    if (!matcher_->Valid()) {
        SetResult(Res(IDS_FIND_INVALID));
        return false;
    }
    const int rows = app_.DataRows();
    const int cols = static_cast<int>(app_.doc.ColumnCount());
    if (rows == 0 || cols == 0) {
        return false;
    }
    const int activeRow = std::min(app_.active.row, rows - 1);
    const int activeCol = std::min(app_.active.col, cols - 1);
    const long long count = columnOnly_ ? rows : static_cast<long long>(rows) * cols;
    const long long start = columnOnly_ ? activeRow : static_cast<long long>(activeRow) * cols + activeCol;
    for (long long step = includeCurrent ? 0 : 1; step <= count; ++step) {
        const long long index = forward ? (start + step) % count : ((start - step) % count + count) % count;
        const Pos p = columnOnly_ ? Pos{static_cast<int>(index), activeCol}
                                  : Pos{static_cast<int>(index / cols), static_cast<int>(index % cols)};
        if (matcher_->Matches(app_.doc.Cell(app_.DocRowOf(p.row), static_cast<std::size_t>(p.col)))) {
            SetResult(L"");
            app_.SetSelection(p, p);
            return true;
        }
    }
    SetResult(Res(IDS_FIND_NOT_FOUND));
    return false;
}

void FindBar::HandleCommand(int id, int code) {
    switch (id) {
    case IDC_FIND_EDIT:
        if (code == EN_CHANGE) {
            UpdateMatcher();
            FindNext(true, true);
        }
        break;
    case IDOK:  // Enter, as sent by the dialog manager
        FindNext(!KeyDown(VK_SHIFT), false);
        break;
    case IDC_FIND_NEXT:
    case IDC_FIND_PREV:
        FindNext(id == IDC_FIND_NEXT, false);
        SetFocus(edit_);
        break;
    case IDC_FIND_CASE:
    case IDC_FIND_WHOLE:
    case IDC_FIND_REGEX:
    case IDC_FIND_COLUMN:
        UpdateMatcher();
        FindNext(true, true);
        SetFocus(edit_);
        break;
    case IDC_FIND_CLOSE:
    case IDCANCEL:  // Escape
        Show(false);
        break;
    default:
        break;
    }
}

}  // namespace app
