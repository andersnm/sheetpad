#include "find_bar.h"

#include <algorithm>
#include <string>

#include "app.h"
#include "text_encoding.h"

using namespace sheetpad;

namespace app {

// ---------------------------------------------------------------- find bar

LRESULT FindEdit::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_KEYDOWN: {
        const bool shift = KeyDown(VK_SHIFT);
        if (wParam == VK_RETURN || wParam == VK_F3) {
            app_.findBar.FindNext(!shift, false);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            app_.findBar.Show(false);
            return 0;
        }
        if (KeyDown(VK_CONTROL) && wParam == 'A') {
            SendMessageW(*this, EM_SETSEL, 0, -1);
            return 0;
        }
        if (KeyDown(VK_CONTROL) && wParam == 'F') {
            SendMessageW(*this, EM_SETSEL, 0, -1);
            return 0;
        }
        break;
    }
    case WM_CHAR:
        if (wParam == VK_RETURN || wParam == VK_ESCAPE || (wParam == 1 && KeyDown(VK_CONTROL))) {
            return 0;
        }
        break;
    default:
        break;
    }
    return DefaultProc(message, wParam, lParam);
}

void FindBar::Create(HWND frame, HINSTANCE instance) {
    frame_ = frame;
    const auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0) {
        HWND control = CreateWindowExW(exStyle, cls, text, WS_CHILD | style, 0, 0, 10, 10, frame,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app_.font), TRUE);
        return control;
    };
    make(L"STATIC", L"Find:", SS_CENTERIMAGE, kFindLabelId);
    edit_.Subclass(make(L"EDIT", L"", ES_AUTOHSCROLL, kFindEditId, WS_EX_CLIENTEDGE));
    make(L"BUTTON", L"Previous", BS_PUSHBUTTON, kFindPrevId);
    make(L"BUTTON", L"Next", BS_PUSHBUTTON, kFindNextId);
    make(L"BUTTON", L"Match case", BS_AUTOCHECKBOX, kFindCaseId);
    make(L"BUTTON", L"Whole cell", BS_AUTOCHECKBOX, kFindWholeId);
    make(L"BUTTON", L"Regex", BS_AUTOCHECKBOX, kFindRegexId);
    make(L"BUTTON", L"This column", BS_AUTOCHECKBOX, kFindColumnId);
    result_.Attach(make(L"STATIC", L"", SS_CENTERIMAGE | SS_ENDELLIPSIS, kFindResultId));
    make(L"BUTTON", L"Close", BS_PUSHBUTTON, kFindCloseId);
}

void FindBar::SetResult(const wchar_t* text) {
    SetWindowTextW(result_, text);
}

bool FindBar::Checked(int id) const {
    return SendMessageW(GetDlgItem(frame_, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void FindBar::UpdateMatcher() {
    FindOptions options;
    options.text = WideToUtf8(WindowText(edit_));
    options.matchCase = Checked(kFindCaseId);
    options.wholeCell = Checked(kFindWholeId);
    options.regex = Checked(kFindRegexId);
    columnOnly_ = Checked(kFindColumnId);
    matcher_.emplace(options);
    InvalidateRect(app_.grid, nullptr, FALSE);
}

void FindBar::Show(bool show) {
    if (show == visible_ && !show) {
        return;
    }
    visible_ = show;
    for (int id = kFindLabelId; id <= kFindResultId; ++id) {
        ShowWindow(GetDlgItem(frame_, id), show ? SW_SHOW : SW_HIDE);
    }
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

// Places the controls in a row starting at `top`.
void FindBar::Layout(int top) {
    const int h = app_.m.rowHeight;
    int x = app_.Scale(6);
    const auto place = [&](int id, int width) {
        MoveWindow(GetDlgItem(frame_, id), x, top, width, h, TRUE);
        x += width + app_.Scale(6);
    };
    place(kFindLabelId, app_.Scale(40));
    place(kFindEditId, app_.Scale(220));
    place(kFindPrevId, app_.Scale(56));
    place(kFindNextId, app_.Scale(56));
    place(kFindCaseId, app_.Scale(96));
    place(kFindWholeId, app_.Scale(92));
    place(kFindRegexId, app_.Scale(64));
    place(kFindColumnId, app_.Scale(104));
    place(kFindResultId, app_.Scale(110));
    place(kFindCloseId, app_.Scale(60));
}

bool FindBar::FindNext(bool forward, bool includeCurrent) {
    if (!matcher_ || matcher_->Empty()) {
        SetResult(L"");
        return false;
    }
    if (!matcher_->Valid()) {
        SetResult(L"Invalid pattern");
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
    SetResult(L"Not found");
    return false;
}

void FindBar::HandleCommand(int id, int code) {
    switch (id) {
    case kFindEditId:
        if (code == EN_CHANGE) {
            UpdateMatcher();
            FindNext(true, true);
        }
        break;
    case kFindNextId:
    case kFindPrevId:
        FindNext(id == kFindNextId, false);
        SetFocus(edit_);
        break;
    case kFindCaseId:
    case kFindWholeId:
    case kFindRegexId:
    case kFindColumnId:
        UpdateMatcher();
        FindNext(true, true);
        SetFocus(edit_);
        break;
    case kFindCloseId:
        Show(false);
        break;
    default:
        break;
    }
}

}  // namespace app
