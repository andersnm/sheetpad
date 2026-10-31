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

// ---------------------------------------------------------------- in-cell editor

std::wstring ToEditText(const std::string& utf8) {
    const std::wstring wide = Utf8ToWide(utf8);
    std::wstring out;
    out.reserve(wide.size());
    for (std::size_t i = 0; i < wide.size(); ++i) {
        if (wide[i] == L'\n' && (i == 0 || wide[i - 1] != L'\r')) {
            out.push_back(L'\r');
        }
        out.push_back(wide[i]);
    }
    return out;
}

std::string NormalizeNewlines(std::string text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

void PositionEdit() {
    const RECT cell = CellRect(g.editCell);
    const int inset = 2;
    MoveWindow(g.edit, cell.left + inset, cell.top + inset, g.m.columnWidth - 2 * inset,
               g.m.rowHeight - 2 * inset, TRUE);

    RECT inner{};
    GetClientRect(g.edit, &inner);
    const int margin = std::max(0, g.m.padding - inset);
    const int top = std::max(0, (static_cast<int>(inner.bottom) - g.m.textHeight) / 2);
    RECT formatting{margin, top, std::max<int>(margin + 1, inner.right - margin), inner.bottom};
    SendMessageW(g.edit, EM_SETRECTNP, 0, reinterpret_cast<LPARAM>(&formatting));
}

void BeginEdit(HWND window, const std::wstring* initialText) {
    if (g.editing) {
        return;
    }
    EnsureVisible(window, g.active);
    g.anchor = g.active;
    g.editCell = g.active;
    const std::size_t editRow = DocRowOf(g.editCell.row);
    const std::size_t editCol = static_cast<std::size_t>(g.editCell.col);
    const std::string& formula = g.doc.Formula(editRow, editCol);
    g.editOriginal = formula.empty() ? g.doc.Cell(editRow, editCol) : formula;
    g.editing = true;

    const std::wstring text = initialText ? *initialText : ToEditText(g.editOriginal);
    SetWindowTextW(g.edit, text.c_str());
    PositionEdit();
    ShowWindow(g.edit, SW_SHOW);
    SetFocus(g.edit);
    SendMessageW(g.edit, EM_SETSEL, text.size(), text.size());
    SendMessageW(g.edit, EM_EMPTYUNDOBUFFER, 0, 0);
    Redraw(window);
}

void EndEdit(HWND window) {
    g.editing = false;
    ShowWindow(g.edit, SW_HIDE);
    SetFocus(window);
}

void CommitEdit(HWND window) {
    if (!g.editing) {
        return;
    }
    const int length = GetWindowTextLengthW(g.edit);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(g.edit, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    const Pos cell = g.editCell;
    const std::string original = g.editOriginal;
    EndEdit(window);

    std::string value = NormalizeNewlines(WideToUtf8(text));
    if (value != NormalizeNewlines(original)) {
        const bool isFormula = value.size() > 1 && value.front() == '=';
        g.doc.ApplyEdits(
            {{DocRowOf(cell.row), static_cast<std::size_t>(cell.col), std::move(value), isFormula}});
    }
    AfterDocumentChanged(window);
}

void CancelEdit(HWND window) {
    if (!g.editing) {
        return;
    }
    EndEdit(window);
    Redraw(window);
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    HWND parent = GetParent(hwnd);
    switch (message) {
    case WM_KEYDOWN:
        switch (wParam) {
        case VK_RETURN:
            if (KeyDown(VK_CONTROL) || KeyDown(VK_MENU)) {
                SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\r\n"));
            } else {
                PostMessageW(parent, kMsgEditKey, VK_RETURN, KeyDown(VK_SHIFT));
            }
            return 0;
        case VK_TAB:
            PostMessageW(parent, kMsgEditKey, VK_TAB, KeyDown(VK_SHIFT));
            return 0;
        case VK_ESCAPE:
            PostMessageW(parent, kMsgEditKey, VK_ESCAPE, 0);
            return 0;
        case VK_UP:
        case VK_DOWN:
            if (SendMessageW(hwnd, EM_GETLINECOUNT, 0, 0) <= 1) {
                PostMessageW(parent, kMsgEditKey, wParam, 0);
                return 0;
            }
            break;
        default:
            break;
        }
        break;
    case WM_SYSKEYDOWN:
        if (wParam == VK_RETURN) {
            SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\r\n"));
            return 0;
        }
        break;
    case WM_CHAR:
    case WM_SYSCHAR:
        if (wParam == L'\r' || wParam == L'\n' || wParam == L'\t' || wParam == 27) {
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        PostMessageW(parent, kMsgEditFocusLost, 0, 0);
        break;
    default:
        break;
    }
    return CallWindowProcW(g.editDefaultProc, hwnd, message, wParam, lParam);
}

void HandleEditKey(HWND window, WPARAM key, bool shift) {
    if (!g.editing) {
        return;
    }
    if (key == VK_ESCAPE) {
        CancelEdit(window);
        return;
    }
    CommitEdit(window);
    switch (key) {
    case VK_RETURN: MoveActive(window, shift ? -1 : 1, 0, false); break;
    case VK_TAB: MoveActive(window, 0, shift ? -1 : 1, false); break;
    case VK_UP: MoveActive(window, -1, 0, false); break;
    case VK_DOWN: MoveActive(window, 1, 0, false); break;
    default: break;
    }
}

void CreateEditControl(HWND window) {
    g.edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_AUTOHSCROLL | ES_AUTOVSCROLL,
                             0, 0, 10, 10, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditControlId)),
                             GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g.edit, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);
    g.editDefaultProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g.edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditProc)));
}

}
