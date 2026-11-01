#include "cell_editor.h"

#include <algorithm>
#include <string>

#include "app.h"
#include "text_encoding.h"

using namespace sheetpad;

namespace app {

namespace {

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

}  // namespace

// ---------------------------------------------------------------- in-cell editor

void CellEditor::Create(HWND grid) {
    Attach(CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_AUTOHSCROLL | ES_AUTOVSCROLL, 0, 0, 10, 10,
                           grid, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditControlId)),
                           GetModuleHandleW(nullptr), nullptr));
    SendMessageW(*this, WM_SETFONT, reinterpret_cast<WPARAM>(app_.font), FALSE);
    Subclass(*this);
}

void CellEditor::Position() {
    const Metrics& m = app_.m;
    const RECT cell = app_.grid.CellRect(cell_);
    const int inset = 2;
    MoveWindow(*this, cell.left + inset, cell.top + inset, (cell.right - cell.left) - 2 * inset,
               m.rowHeight - 2 * inset, TRUE);

    RECT inner{};
    GetClientRect(*this, &inner);
    const int margin = std::max(0, m.padding - inset);
    const int top = std::max(0, (static_cast<int>(inner.bottom) - m.textHeight) / 2);
    RECT formatting{margin, top, std::max<int>(margin + 1, inner.right - margin), inner.bottom};
    SendMessageW(*this, EM_SETRECTNP, 0, reinterpret_cast<LPARAM>(&formatting));
}

void CellEditor::Begin(const std::wstring* initialText) {
    if (editing_) {
        return;
    }
    app_.grid.EnsureVisible(app_.active);
    app_.anchor = app_.active;
    cell_ = app_.active;
    const std::size_t row = app_.DocRowOf(cell_.row);
    const auto col = static_cast<std::size_t>(cell_.col);
    const std::string& formula = app_.doc.Formula(row, col);
    original_ = formula.empty() ? app_.doc.Cell(row, col) : formula;
    editing_ = true;

    const std::wstring text = initialText ? *initialText : ToEditText(original_);
    SetWindowTextW(*this, text.c_str());
    Position();
    ShowWindow(*this, SW_SHOW);
    SetFocus(*this);
    SendMessageW(*this, EM_SETSEL, text.size(), text.size());
    SendMessageW(*this, EM_EMPTYUNDOBUFFER, 0, 0);
    app_.grid.Redraw();
}

void CellEditor::End() {
    editing_ = false;
    ShowWindow(*this, SW_HIDE);
    SetFocus(app_.grid);
}

void CellEditor::Commit() {
    if (!editing_) {
        return;
    }
    const std::wstring text = WindowText(*this);
    const Pos cell = cell_;
    const std::string original = original_;
    End();

    std::string value = NormalizeNewlines(WideToUtf8(text));
    if (value != NormalizeNewlines(original)) {
        const bool isFormula = value.size() > 1 && value.front() == '=';
        app_.doc.ApplyEdits(
            {{app_.DocRowOf(cell.row), static_cast<std::size_t>(cell.col), std::move(value), isFormula}});
    }
    app_.AfterDocumentChanged();
}

void CellEditor::Cancel() {
    if (!editing_) {
        return;
    }
    End();
    app_.grid.Redraw();
}

void CellEditor::HandleKey(WPARAM key, bool shift) {
    if (!editing_) {
        return;
    }
    if (key == VK_ESCAPE) {
        Cancel();
        return;
    }
    Commit();
    switch (key) {
    case VK_RETURN: app_.MoveActive(shift ? -1 : 1, 0, false); break;
    case VK_TAB: app_.MoveActive(0, shift ? -1 : 1, false); break;
    case VK_UP: app_.MoveActive(-1, 0, false); break;
    case VK_DOWN: app_.MoveActive(1, 0, false); break;
    default: break;
    }
}

LRESULT CellEditor::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
    HWND parent = GetParent(*this);
    switch (message) {
    case WM_KEYDOWN:
        switch (wParam) {
        case VK_RETURN:
            if (KeyDown(VK_CONTROL) || KeyDown(VK_MENU)) {
                SendMessageW(*this, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\r\n"));
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
            if (SendMessageW(*this, EM_GETLINECOUNT, 0, 0) <= 1) {
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
            SendMessageW(*this, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\r\n"));
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
    return DefaultProc(message, wParam, lParam);
}

}  // namespace app
