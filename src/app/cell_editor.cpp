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
    HideHints();
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

// ---------------------------------------------------------------- formula Autocomplete

// The editor text as one char per UTF-16 unit so indices match the control. Empty unless the text is a formula.
std::string CellEditor::FormulaText(std::size_t& caret) const {
    const std::wstring wide = WindowText(*this);
    DWORD selStart = 0;
    DWORD selEnd = 0;
    SendMessageW(*this, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
    caret = selEnd;
    if (wide.empty() || wide.front() != L'=') {
        return {};
    }
    std::string text;
    text.reserve(wide.size());
    for (const wchar_t c : wide) {
        text.push_back(c < 128 ? static_cast<char>(c) : '\x80');
    }
    return text;
}

POINT CellEditor::ScreenPointAt(std::size_t charIndex, bool bottom) const {
    const LRESULT pos = SendMessageW(*this, EM_POSFROMCHAR, charIndex, 0);
    POINT point{static_cast<short>(LOWORD(pos)), 0};
    ClientToScreen(*this, &point);
    RECT window{};
    GetWindowRect(*this, &window);
    point.y = bottom ? window.bottom : window.top;
    return point;
}

void CellEditor::HideHints() {
    completionOpen_ = false;
    completionList_.Hide();
    signatureTip_.Hide();
}

void CellEditor::CloseCompletion() {
    completionOpen_ = false;
    completionList_.Hide();
}

void CellEditor::OpenCompletion() {
    std::size_t caret = 0;
    const std::string text = FormulaText(caret);
    if (text.empty() || InStringLiteral(text, caret)) {
        return;
    }
    const sheetpad::TokenRange token = TokenAt(text, caret);
    candidates_ = CompletionsFor(text.substr(token.start, token.end - token.start));
    if (candidates_.empty()) {
        CloseCompletion();
        return;
    }
    tokenStart_ = token.start;
    tokenLength_ = token.end - token.start;
    std::vector<HintWindow::Item> items;
    for (const FunctionInfo* info : candidates_) {
        items.push_back({Utf8ToWide(info->name), Utf8ToWide(info->description)});
    }
    completionList_.SetAcceptHandler(
        [](void* context, std::size_t index) { static_cast<CellEditor*>(context)->AcceptCompletion(index); }, this);
    completionList_.ShowList(std::move(items), ScreenPointAt(token.start, true));
    completionOpen_ = true;
}

void CellEditor::RefreshCompletion() {
    std::size_t caret = 0;
    const std::string text = FormulaText(caret);
    if (text.empty() || InStringLiteral(text, caret)) {
        CloseCompletion();
        return;
    }
    const sheetpad::TokenRange token = TokenAt(text, caret);
    const std::size_t length = token.end - token.start;
    if (token.start != tokenStart_ || (length == 0 && tokenLength_ > 0)) {
        CloseCompletion();
        return;
    }
    tokenLength_ = length;
    const auto matches = CompletionsFor(text.substr(token.start, length));
    if (matches.empty()) {
        CloseCompletion();
        return;
    }
    if (matches != candidates_) {
        candidates_ = matches;
        std::vector<HintWindow::Item> items;
        for (const FunctionInfo* info : candidates_) {
            items.push_back({Utf8ToWide(info->name), Utf8ToWide(info->description)});
        }
        completionList_.ShowList(std::move(items), ScreenPointAt(token.start, true));
    }
}

void CellEditor::AcceptCompletion(std::size_t index) {
    if (index >= candidates_.size()) {
        return;
    }
    std::size_t caret = 0;
    const std::string text = FormulaText(caret);
    const sheetpad::TokenRange token = TokenAt(text, caret);
    const std::wstring name = Utf8ToWide(candidates_[index]->name);
    const bool parentheses = NeedsParentheses(text, token.end);
    CloseCompletion();
    SendMessageW(*this, EM_SETSEL, token.start, token.end);
    SendMessageW(*this, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>((parentheses ? name + L"()" : name).c_str()));
    const std::size_t end = token.start + name.size() + (parentheses ? 1 : 0);
    SendMessageW(*this, EM_SETSEL, end, end);
    UpdateSignatureTip();
}

void CellEditor::UpdateSignatureTip() {
    std::size_t caret = 0;
    const std::string text = FormulaText(caret);
    if (!text.empty()) {
        if (const auto call = CallAt(text, caret)) {
            const auto range = SignatureArgRange(*call->info, call->argIndex);
            signatureTip_.ShowTip(Utf8ToWide(call->info->signature), range.first, range.second,
                                  ScreenPointAt(caret, false));
            return;
        }
    }
    signatureTip_.Hide();
}

void CellEditor::AfterEdit() {
    if (completionOpen_) {
        RefreshCompletion();
    }
    UpdateSignatureTip();
}

LRESULT CellEditor::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
    HWND parent = GetParent(*this);
    switch (message) {
    case WM_KEYDOWN:
        if (wParam == VK_SPACE && KeyDown(VK_CONTROL) && !KeyDown(VK_MENU)) {
            OpenCompletion();
            return 0;
        }
        if (completionOpen_) {
            switch (wParam) {
            case VK_UP:
                completionList_.MoveSelection(-1);
                return 0;
            case VK_DOWN:
                completionList_.MoveSelection(1);
                return 0;
            case VK_TAB:
                AcceptCompletion(completionList_.Selected());
                return 0;
            case VK_RETURN:
                if (!KeyDown(VK_CONTROL) && !KeyDown(VK_MENU)) {
                    AcceptCompletion(completionList_.Selected());
                    return 0;
                }
                break;
            case VK_ESCAPE:
                CloseCompletion();
                return 0;
            default:
                break;
            }
        }
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
        if ((wParam == L' ' || wParam == 0) && KeyDown(VK_CONTROL)) {
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        HideHints();
        PostMessageW(parent, kMsgEditFocusLost, 0, 0);
        break;
    default:
        break;
    }
    const LRESULT result = DefaultProc(message, wParam, lParam);
    switch (message) {
    case WM_CHAR:
    case WM_KEYDOWN:
    case WM_LBUTTONUP:
    case WM_PASTE:
    case WM_CUT:
    case WM_UNDO:
        if (editing_) {
            AfterEdit();
        }
        break;
    default:
        break;
    }
    return result;
}

}  // namespace app
