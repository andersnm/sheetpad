#include "inspector_pane.h"

#include <algorithm>
#include <string>

#include "app.h"
#include "text_encoding.h"

using namespace sheetpad;

namespace app {

namespace {

constexpr std::size_t kMaxShownBytes = 1000;

// Evaluates nothing: formula cells already hold their calculated text.
class DocSource : public CellSource {
public:
    explicit DocSource(Document& doc) : doc_(doc) {}

    Value GetCell(std::size_t row, std::size_t col) override { return ValueFromCell(doc_.Cell(row, col)); }
    std::size_t RowCount() const override { return doc_.RowCount(); }
    std::size_t ColumnCount() const override { return doc_.ColumnCount(); }

private:
    Document& doc_;
};

std::wstring Lines(const std::string& utf8) {
    std::string shown = utf8.size() > kMaxShownBytes ? utf8.substr(0, kMaxShownBytes) + "..." : utf8;
    std::wstring wide = Utf8ToWide(shown);
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

}  // namespace

std::wstring InspectorPane::Describe() {
    const Document& doc = app_.doc;
    const std::size_t row = app_.DocRowOf(app_.active.row);
    const auto col = static_cast<std::size_t>(app_.active.col);
    const std::string& value = doc.Cell(row, col);
    std::wstring text = L"Cell " + app_.CellName(app_.active) + L"\r\n\r\n";
    const FormulaInspection* inspection = ActiveInspection();
    if (!inspection) {
        text += L"Value:\r\n" + (value.empty() ? std::wstring(L"(empty)") : Lines(value));
        text += L"\r\n\r\nThis cell is not a formula. Start a value with = to make one.";
        return text;
    }
    text += L"Formula:\r\n" + Lines(doc.Formula(row, col)) + L"\r\n\r\nInputs:\r\n";
    if (inspection->inputs.empty()) {
        text += L"(none)\r\n";
    }
    for (const FormulaInput& input : inspection->inputs) {
        text += Utf8ToWide(input.label) + L" = " + Lines(input.detail) + L"\r\n";
    }
    text += L"\r\nResult:\r\n" + (value.empty() ? std::wstring(L"(empty)") : Lines(value));
    if (!inspection->diagnostic.empty()) {
        text += L"\r\n\r\nProblem:\r\n" + Lines(inspection->diagnostic);
    }
    return text;
}

const FormulaInspection* InspectorPane::ActiveInspection() {
    Document& doc = app_.doc;
    const std::size_t row = app_.DocRowOf(app_.active.row);
    const auto col = static_cast<std::size_t>(std::max(app_.active.col, 0));
    const std::string& formula = doc.Formula(row, col);
    if (formula.empty()) {
        return nullptr;
    }
    if (!cache_.valid || cache_.revision != doc.Revision() || cache_.row != row || cache_.col != col ||
        cache_.formula != formula) {
        DocSource source(doc);
        cache_.inspection = InspectFormula(formula, source, doc.Cell(row, col));
        cache_.revision = doc.Revision();
        cache_.row = row;
        cache_.col = col;
        cache_.formula = formula;
        cache_.valid = true;
    }
    return &cache_.inspection;
}

void InspectorPane::Create(HWND frame, HINSTANCE instance) {
    Attach(CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 0, 0, 10, 10, frame,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInspectorId)), instance, nullptr));
    SendMessageW(*this, WM_SETFONT, reinterpret_cast<WPARAM>(app_.font), TRUE);
}

int InspectorPane::Width() const {
    return visible_ ? app_.Scale(280) : 0;
}

void InspectorPane::Update() {
    if (!visible_ || !hwnd_) {
        return;
    }
    const std::wstring text = Describe();
    if (WindowText(*this) != text) {
        SetWindowTextW(*this, text.c_str());
    }
}

void InspectorPane::Show(bool show) {
    visible_ = show;
    ShowWindow(*this, show ? SW_SHOW : SW_HIDE);
    app_.frame.Layout();
    Update();
    InvalidateRect(app_.grid, nullptr, FALSE);
}

}  // namespace app
