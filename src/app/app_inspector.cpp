#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdint>
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

namespace {

constexpr int kInspectorId = 5000;
constexpr std::size_t kMaxShownBytes = 1000;

// Evaluates nothing: formula cells already hold their calculated text.
class DocSource : public CellSource {
public:
    Value GetCell(std::size_t row, std::size_t col) override { return ValueFromCell(g.doc.Cell(row, col)); }
    std::size_t RowCount() const override { return g.doc.RowCount(); }
    std::size_t ColumnCount() const override { return g.doc.ColumnCount(); }
};

// Inspecting a range scans it, so the result is reused until the document or the selection changes.
struct Cache {
    bool valid = false;
    std::uint64_t revision = 0;
    std::size_t row = 0;
    std::size_t col = 0;
    std::string formula;
    FormulaInspection inspection;
};
Cache cache;

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

std::wstring Describe() {
    const std::size_t row = DocRowOf(g.active.row);
    const auto col = static_cast<std::size_t>(g.active.col);
    const std::string& value = g.doc.Cell(row, col);
    std::wstring text = L"Cell " + CellName(g.active) + L"\r\n\r\n";
    const FormulaInspection* inspection = ActiveInspection();
    if (!inspection) {
        text += L"Value:\r\n" + (value.empty() ? std::wstring(L"(empty)") : Lines(value));
        text += L"\r\n\r\nThis cell is not a formula. Start a value with = to make one.";
        return text;
    }
    text += L"Formula:\r\n" + Lines(g.doc.Formula(row, col)) + L"\r\n\r\nInputs:\r\n";
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

}  // namespace

const FormulaInspection* ActiveInspection() {
    const std::size_t row = DocRowOf(g.active.row);
    const auto col = static_cast<std::size_t>(std::max(g.active.col, 0));
    const std::string& formula = g.doc.Formula(row, col);
    if (formula.empty()) {
        return nullptr;
    }
    if (!cache.valid || cache.revision != g.doc.Revision() || cache.row != row || cache.col != col ||
        cache.formula != formula) {
        DocSource source;
        cache.inspection = InspectFormula(formula, source, g.doc.Cell(row, col));
        cache.revision = g.doc.Revision();
        cache.row = row;
        cache.col = col;
        cache.formula = formula;
        cache.valid = true;
    }
    return &cache.inspection;
}

void CreateInspector(HWND frame, HINSTANCE instance) {
    g.inspector = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 0, 0, 10, 10,
                                  frame, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInspectorId)), instance,
                                  nullptr);
    SendMessageW(g.inspector, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
}

int InspectorWidth() {
    return g.inspectorVisible ? Scale(280) : 0;
}

void UpdateInspector() {
    if (!g.inspectorVisible || !g.inspector) {
        return;
    }
    const std::wstring text = Describe();
    if (WindowText(g.inspector) != text) {
        SetWindowTextW(g.inspector, text.c_str());
    }
}

void ShowInspector(bool show) {
    g.inspectorVisible = show;
    ShowWindow(g.inspector, show ? SW_SHOW : SW_HIDE);
    LayoutFrame();
    UpdateInspector();
    InvalidateRect(g.grid, nullptr, FALSE);
}

}  // namespace app
