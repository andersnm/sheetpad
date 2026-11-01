#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "app.h"
#include "html_table.h"
#include "text_encoding.h"

using namespace sheetpad;

namespace app {

namespace {

std::string MakeClipboardHtml(const std::string& fragment) {
    const std::string prefix = "<html>\r\n<body>\r\n<!--StartFragment-->";
    const std::string suffix = "<!--EndFragment-->\r\n</body>\r\n</html>";
    const char* headerFormat =
        "Version:0.9\r\nStartHTML:%010lu\r\nEndHTML:%010lu\r\nStartFragment:%010lu\r\nEndFragment:%010lu\r\n";
    char header[160];
    const auto headerLength =
        static_cast<unsigned long>(std::snprintf(header, sizeof(header), headerFormat, 0ul, 0ul, 0ul, 0ul));
    const unsigned long startFragment = headerLength + static_cast<unsigned long>(prefix.size());
    const unsigned long endFragment = startFragment + static_cast<unsigned long>(fragment.size());
    const unsigned long endHtml = endFragment + static_cast<unsigned long>(suffix.size());
    std::snprintf(header, sizeof(header), headerFormat, headerLength, endHtml, startFragment, endFragment);
    return std::string(header) + prefix + fragment + suffix;
}

bool PutClipboardData(UINT format, const void* data, std::size_t bytes) {
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        return false;
    }
    void* target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(target, data, bytes);
    GlobalUnlock(memory);
    if (!SetClipboardData(format, memory)) {
        GlobalFree(memory);
        return false;
    }
    return true;
}

// Builds the edit for one pasted cell. Text that parses as a formula becomes a formula; when the clipboard came
// from this app, relative references shift by the distance from the copied cell, as in Excel.
CellEdit PastedCell(std::size_t row, std::size_t col, const std::string& text, bool internal, Pos origin) {
    if (text.size() > 1 && text.front() == '=' && ParseFormula(text)) {
        if (!internal) {
            return {row, col, text, true};
        }
        return {row, col,
                RewriteForShift(text, static_cast<long long>(row) - origin.row,
                                static_cast<long long>(col) - origin.col),
                true};
    }
    return {row, col, text, false};
}

}  // namespace

// ---------------------------------------------------------------- clipboard

Table App::SelectionTable() const {
    const CellRange sel = Selection();
    Table table;
    for (int row = sel.top; row <= sel.bottom; ++row) {
        Row cells;
        for (int col = sel.left; col <= sel.right; ++col) {
            const std::size_t r = DocRowOf(row);
            const auto c = static_cast<std::size_t>(col);
            // Formulas are copied as text so Excel pastes them as live formulas.
            const std::string& formula = doc.Formula(r, c);
            cells.push_back(formula.empty() ? doc.Cell(r, c) : formula);
        }
        table.push_back(std::move(cells));
    }
    return table;
}

// `moving` is set for Cut: pasted formulas keep their references instead of shifting.
bool App::CopySelection(bool moving) {
    const Table table = SelectionTable();
    const std::wstring text = Utf8ToWide(BuildTsv(table));
    // Rich HTML is only worth building for modest selections; plain text is always provided.
    const bool withHtml = table.size() * (table.empty() ? 0 : table[0].size()) <= 200000;
    const std::string html = withHtml ? MakeClipboardHtml(BuildHtmlTable(table)) : std::string();
    if (!OpenClipboard(grid)) {
        return false;
    }
    EmptyClipboard();
    bool ok = PutClipboardData(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    static const UINT htmlFormat = RegisterClipboardFormatW(L"HTML Format");
    if (withHtml && htmlFormat != 0) {
        PutClipboardData(htmlFormat, html.c_str(), html.size() + 1);
    }
    const CellRange sel = Selection();
    copySequence = GetClipboardSequenceNumber();
    copyOrigin = {static_cast<int>(DocRowOf(sel.top)), sel.left};
    copyMoving = moving;
    CloseClipboard();
    return ok;
}

void App::ClearSelection() {
    const CellRange sel = Selection();
    std::vector<CellEdit> edits;
    const int lastRow = std::min(sel.bottom, DataRows() - 1);
    const int lastCol = std::min(sel.right, static_cast<int>(doc.ColumnCount()) - 1);
    for (int row = sel.top; row <= lastRow; ++row) {
        for (int col = sel.left; col <= lastCol; ++col) {
            const std::size_t r = DocRowOf(row);
            const auto c = static_cast<std::size_t>(col);
            if (!doc.Cell(r, c).empty() || !doc.Formula(r, c).empty()) {
                edits.push_back({r, c, std::string()});
            }
        }
    }
    doc.ApplyEdits(std::move(edits));
    AfterDocumentChanged();
}

// Copies the first row (or column) of the selection over the rest, or the neighbor above (or left) when only
// one row (or column) is selected. Relative references in formulas shift as in Excel.
void App::FillSelection(bool down) {
    const CellRange sel = Selection();
    std::vector<CellEdit> edits;
    for (int row = sel.top; row <= sel.bottom; ++row) {
        for (int col = sel.left; col <= sel.right; ++col) {
            const int sourceRow = down ? (sel.bottom > sel.top ? sel.top : sel.top - 1) : row;
            const int sourceCol = down ? col : (sel.right > sel.left ? sel.left : sel.left - 1);
            if (sourceRow < 0 || sourceCol < 0 || (sourceRow == row && sourceCol == col)) {
                continue;
            }
            const std::size_t sr = DocRowOf(sourceRow);
            const auto sc = static_cast<std::size_t>(sourceCol);
            const std::size_t dr = DocRowOf(row);
            const std::string& formula = doc.Formula(sr, sc);
            if (formula.empty()) {
                edits.push_back({dr, static_cast<std::size_t>(col), doc.Cell(sr, sc)});
            } else {
                edits.push_back({dr, static_cast<std::size_t>(col),
                                 RewriteForShift(formula, static_cast<long long>(dr) - static_cast<long long>(sr),
                                                 col - sourceCol),
                                 true});
            }
        }
    }
    doc.ApplyEdits(std::move(edits));
    AfterDocumentChanged();
}

void App::PasteClipboard() {
    if (!OpenClipboard(grid)) {
        return;
    }
    const bool internal = copySequence != 0 && copySequence == GetClipboardSequenceNumber();

    // Browsers and Word often flatten a copied table to one cell per line in the text format, so the HTML format
    // wins. Our own copies keep their formulas in the text format, so those use it.
    Table table;
    if (!internal) {
        static const UINT htmlFormat = RegisterClipboardFormatW(L"HTML Format");
        if (htmlFormat != 0) {
            if (HANDLE handle = GetClipboardData(htmlFormat)) {
                if (const auto* bytes = static_cast<const char*>(GlobalLock(handle))) {
                    const std::string_view payload(bytes, GlobalSize(handle));
                    ParseHtml(ExtractClipboardHtmlFragment(payload), table);
                    GlobalUnlock(handle);
                }
            }
        }
    }

    std::wstring wide;
    if (table.empty()) {
        if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
            if (const auto* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
                wide = text;
                GlobalUnlock(handle);
            }
        }
    }
    CloseClipboard();

    std::size_t columnCount = 0;
    if (table.empty()) {
        if (wide.empty()) {
            return;
        }
        const std::string utf8 = WideToUtf8(wide);
        ParsedCsv parsed = ParseCsv(utf8, DetectPasteDelimiter(utf8));
        table = std::move(parsed.rows);
        columnCount = parsed.columnCount;
    } else {
        for (const Row& row : table) {
            columnCount = std::max(columnCount, row.size());
        }
    }
    if (table.empty()) {
        return;
    }

    const CellRange sel = Selection();
    const bool singleValue = table.size() == 1 && table[0].size() == 1;
    const bool fill = singleValue && (sel.bottom > sel.top || sel.right > sel.left);
    const bool shifts = internal && !copyMoving;
    std::vector<CellEdit> edits;
    if (fill) {
        for (int row = sel.top; row <= sel.bottom; ++row) {
            for (int col = sel.left; col <= sel.right; ++col) {
                edits.push_back(PastedCell(DocRowOf(row), static_cast<std::size_t>(col), table[0][0], shifts,
                                           copyOrigin));
            }
        }
    } else {
        for (std::size_t r = 0; r < table.size(); ++r) {
            for (std::size_t c = 0; c < table[r].size(); ++c) {
                edits.push_back(PastedCell(DocRowOf(sel.top + static_cast<int>(r)),
                                           static_cast<std::size_t>(sel.left) + c, table[r][c], shifts,
                                           copyOrigin));
            }
        }
    }
    doc.ApplyEdits(std::move(edits));
    AfterDocumentChanged();
    if (!fill) {
        const Pos end{sel.top + static_cast<int>(table.size()) - 1,
                      sel.left + static_cast<int>(columnCount) - 1};
        SetSelection({sel.top, sel.left}, end);
    }
}

}  // namespace app
