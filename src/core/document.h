#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "csv.h"
#include "formula.h"

namespace sheetpad {

enum class Encoding { Utf8, Utf8Bom, Utf16Le };

struct CellEdit {
    std::size_t row = 0;
    std::size_t col = 0;
    // The cell text, or the formula text (starting with '=') when isFormula is set.
    std::string value;
    bool isFormula = false;
};

using CellKey = std::pair<std::size_t, std::size_t>;

struct FormulaEntry {
    std::string text;
    ExprPtr ast;
};

// Row/column storage, formulas, and undo/redo. Holds no UI state.
//
// Cells hold plain values, as they appear in the CSV. A formula cell also has an entry in the
// formula table, and its cell value is the most recent calculated result.
class Document {
public:
    static Document Blank(std::size_t rows, std::size_t columns);
    static Document FromParsed(ParsedCsv parsed);

    std::size_t RowCount() const { return rows_.size(); }
    std::size_t ColumnCount() const { return columnCount_; }
    const std::string& Cell(std::size_t row, std::size_t col) const;

    // Applies the edits as one undoable step; unchanged cells are skipped.
    // Writing outside the current bounds grows the table. Returns cells changed.
    std::size_t ApplyEdits(std::vector<CellEdit> edits);

    // Structural edits, each one undoable step. Positions and counts are clamped to the data.
    // They return false when nothing changed.
    bool InsertRows(std::size_t at, std::size_t count);
    bool DeleteRows(std::size_t at, std::size_t count);
    bool DuplicateRow(std::size_t row);
    // Deletes the given rows (ascending, unique) as one undoable step.
    bool DeleteRowSet(const std::vector<std::size_t>& rows);
    // Reorders rows as one undoable step: new row i is old row source[i] (a permutation of all rows).
    // Formulas move with their rows; relative references shift like a copy to the new position.
    bool PermuteRows(const std::vector<std::size_t>& source);
    bool InsertColumns(std::size_t at, std::size_t count);
    bool DeleteColumns(std::size_t at, std::size_t count);

    // Formula text for a cell ("=A1+B1"), or an empty string.
    const std::string& Formula(std::size_t row, std::size_t col) const;
    bool HasFormulas() const { return !formulas_.empty(); }
    std::vector<std::pair<CellKey, std::string>> FormulaList() const;
    // Replaces all formulas without recording history (used when loading).
    void LoadFormulas(const std::vector<std::pair<CellKey, std::string>>& formulas);

    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    // Changes whenever the cells or formulas change (including undo and redo), and differs between documents.
    std::uint64_t Revision() const { return revision_; }
    bool Undo();
    bool Redo();

    bool IsDirty() const;
    void MarkSaved();

    std::string Serialize() const;

    char delimiter = ',';
    bool crlf = true;
    bool trailingNewline = true;
    Encoding encoding = Encoding::Utf8;
    // A blank canvas is padded for display; trailing empty rows/columns are not saved.
    bool trimTrailingEmpty = false;
    // Column formulas from the metadata file are preserved but not evaluated yet.
    std::map<std::string, std::string> columnFormulas;

private:
    // Rows are copy-on-write so that an undo snapshot costs one pointer per row, not a copy of every cell.
    using RowPtr = std::shared_ptr<Row>;
    struct Change {
        std::size_t row;
        std::size_t col;
        std::string oldValue;
        std::string newValue;
        std::size_t oldRowSize;
        std::string oldFormula;
        std::string newFormula;
    };
    struct Snapshot {
        std::vector<RowPtr> rows;
        std::size_t columnCount = 1;
        std::map<CellKey, FormulaEntry> formulas;
    };
    struct Command {
        std::vector<Change> changes;
        std::size_t oldRowCount = 0;
        std::size_t oldColumnCount = 0;
        std::size_t newColumnCount = 0;
        // Structural commands swap the whole state with this snapshot on undo and redo.
        std::shared_ptr<Snapshot> snapshot;
    };

    class Evaluator;

    void SetRaw(std::size_t row, std::size_t col, const std::string& value);
    Row& MutableRow(std::size_t row);
    void GrowRows(std::size_t count);
    void SetFormula(std::size_t row, std::size_t col, const std::string& text);
    void Recalculate();
    void PushCommand(Command command);
    std::shared_ptr<Snapshot> CaptureState() const;
    void SwapState(Snapshot& other);
    void ShiftFormulasForInsert(Axis axis, std::size_t at, std::size_t count);
    void ShiftFormulasForDelete(Axis axis, std::size_t at, std::size_t count);
    void InsertRowsRaw(std::size_t at, std::size_t count);

    std::vector<RowPtr> rows_;
    std::size_t columnCount_ = 1;
    std::map<CellKey, FormulaEntry> formulas_;
    std::vector<Command> undo_;
    std::vector<Command> redo_;
    std::optional<std::size_t> savedDepth_ = 0;
    std::uint64_t revision_ = 0;
};

}  // namespace sheetpad
