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

void SetFindResult(const wchar_t* text) {
    SetWindowTextW(g.findResult, text);
}

bool FindNext(HWND window, bool forward, bool includeCurrent) {
    if (!g.matcher || g.matcher->Empty()) {
        SetFindResult(L"");
        return false;
    }
    if (!g.matcher->Valid()) {
        SetFindResult(L"Invalid pattern");
        return false;
    }
    const int rows = DataRows();
    const int cols = static_cast<int>(g.doc.ColumnCount());
    if (rows == 0 || cols == 0) {
        return false;
    }
    const int activeRow = std::min(g.active.row, rows - 1);
    const int activeCol = std::min(g.active.col, cols - 1);
    const bool columnOnly = g.findColumnOnly;
    const long long count = columnOnly ? rows : static_cast<long long>(rows) * cols;
    const long long start = columnOnly ? activeRow : static_cast<long long>(activeRow) * cols + activeCol;
    for (long long step = includeCurrent ? 0 : 1; step <= count; ++step) {
        const long long index = forward ? (start + step) % count : ((start - step) % count + count) % count;
        const Pos p = columnOnly ? Pos{static_cast<int>(index), activeCol}
                                 : Pos{static_cast<int>(index / cols), static_cast<int>(index % cols)};
        if (g.matcher->Matches(g.doc.Cell(DocRowOf(p.row), static_cast<std::size_t>(p.col)))) {
            SetFindResult(L"");
            SetSelection(window, p, p);
            return true;
        }
    }
    SetFindResult(L"Not found");
    return false;
}

bool Checked(int id) {
    return SendMessageW(GetDlgItem(g.frame, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void UpdateMatcher() {
    FindOptions options;
    options.text = WideToUtf8(WindowText(g.findEdit));
    options.matchCase = Checked(kFindCaseId);
    options.wholeCell = Checked(kFindWholeId);
    options.regex = Checked(kFindRegexId);
    g.findColumnOnly = Checked(kFindColumnId);
    g.matcher.emplace(options);
    InvalidateRect(g.grid, nullptr, FALSE);
}

void ShowFindBar(bool show) {
    if (show == g.findVisible && !show) {
        return;
    }
    g.findVisible = show;
    for (int id = kFindLabelId; id <= kFindResultId; ++id) {
        ShowWindow(GetDlgItem(g.frame, id), show ? SW_SHOW : SW_HIDE);
    }
    LayoutFrame();
    if (show) {
        SetFocus(g.findEdit);
        SendMessageW(g.findEdit, EM_SETSEL, 0, -1);
        UpdateMatcher();
    } else {
        g.matcher.reset();
        SetFocus(g.grid);
    }
    InvalidateRect(g.grid, nullptr, FALSE);
}

LRESULT CALLBACK FindEditProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_KEYDOWN: {
        const bool shift = KeyDown(VK_SHIFT);
        if (wParam == VK_RETURN || wParam == VK_F3) {
            FindNext(g.grid, !shift, false);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            ShowFindBar(false);
            return 0;
        }
        if (KeyDown(VK_CONTROL) && wParam == 'A') {
            SendMessageW(window, EM_SETSEL, 0, -1);
            return 0;
        }
        if (KeyDown(VK_CONTROL) && wParam == 'F') {
            SendMessageW(window, EM_SETSEL, 0, -1);
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
    return CallWindowProcW(g.findEditDefaultProc, window, message, wParam, lParam);
}

void CreateFindBar(HWND frame, HINSTANCE instance) {
    const auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0) {
        HWND control = CreateWindowExW(exStyle, cls, text, WS_CHILD | style, 0, 0, 10, 10, frame,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
        return control;
    };
    g.findLabel = make(L"STATIC", L"Find:", SS_CENTERIMAGE, kFindLabelId);
    g.findEdit = make(L"EDIT", L"", ES_AUTOHSCROLL, kFindEditId, WS_EX_CLIENTEDGE);
    g.findEditDefaultProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g.findEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(FindEditProc)));
    make(L"BUTTON", L"Previous", BS_PUSHBUTTON, kFindPrevId);
    make(L"BUTTON", L"Next", BS_PUSHBUTTON, kFindNextId);
    make(L"BUTTON", L"Match case", BS_AUTOCHECKBOX, kFindCaseId);
    make(L"BUTTON", L"Whole cell", BS_AUTOCHECKBOX, kFindWholeId);
    make(L"BUTTON", L"Regex", BS_AUTOCHECKBOX, kFindRegexId);
    make(L"BUTTON", L"This column", BS_AUTOCHECKBOX, kFindColumnId);
    g.findResult = make(L"STATIC", L"", SS_CENTERIMAGE | SS_ENDELLIPSIS, kFindResultId);
    make(L"BUTTON", L"Close", BS_PUSHBUTTON, kFindCloseId);
}

void HandleFindCommand(int id, int code) {
    switch (id) {
    case kFindEditId:
        if (code == EN_CHANGE) {
            UpdateMatcher();
            FindNext(g.grid, true, true);
        }
        break;
    case kFindNextId:
    case kFindPrevId:
        FindNext(g.grid, id == kFindNextId, false);
        SetFocus(g.findEdit);
        break;
    case kFindCaseId:
    case kFindWholeId:
    case kFindRegexId:
    case kFindColumnId:
        UpdateMatcher();
        FindNext(g.grid, true, true);
        SetFocus(g.findEdit);
        break;
    case kFindCloseId:
        ShowFindBar(false);
        break;
    default:
        break;
    }
}

}
