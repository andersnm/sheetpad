#include "document.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace sheetpad {

namespace {

const std::string kEmptyCell;
constexpr int kMaxEvaluationDepth = 20000;

}  // namespace

// Evaluates formulas on demand and memoizes results for one recalculation pass.
class Document::Evaluator : public CellSource {
public:
    explicit Evaluator(const Document& doc) : doc_(doc) {}

    Value GetCell(std::size_t row, std::size_t col) override {
        const auto formula = doc_.formulas_.find({row, col});
        if (formula == doc_.formulas_.end()) {
            return ValueFromCell(doc_.Cell(row, col));
        }

        const std::uint64_t key = (static_cast<std::uint64_t>(row) << 32) | static_cast<std::uint64_t>(col);
        const auto known = memo_.find(key);
        if (known != memo_.end()) {
            return known->second.done ? known->second.value : Value::Err(ErrorCode::Circular);
        }
        if (depth_ >= kMaxEvaluationDepth) {
            return Value::Err(ErrorCode::Error);
        }

        memo_[key] = {};
        ++depth_;
        Value value = Evaluate(formula->second.ast.get(), *this);
        --depth_;
        memo_[key] = {true, value};
        return value;
    }

    std::size_t RowCount() const override { return doc_.rows_.size(); }
    std::size_t ColumnCount() const override { return doc_.columnCount_; }

private:
    struct State {
        bool done = false;
        Value value;
    };

    const Document& doc_;
    std::unordered_map<std::uint64_t, State> memo_;
    int depth_ = 0;
};

Document Document::Blank(std::size_t rows, std::size_t columns) {
    Document doc;
    doc.columnCount_ = std::max<std::size_t>(columns, 1);
    doc.rows_.reserve(std::max<std::size_t>(rows, 1));
    for (std::size_t r = 0; r < std::max<std::size_t>(rows, 1); ++r) {
        doc.rows_.push_back(std::make_shared<Row>(doc.columnCount_));
    }
    doc.trimTrailingEmpty = true;
    return doc;
}

Document Document::FromParsed(ParsedCsv parsed) {
    if (parsed.rows.empty()) {
        Document doc = Blank(20, 8);
        doc.delimiter = parsed.delimiter;
        doc.crlf = parsed.crlf;
        return doc;
    }
    Document doc;
    doc.columnCount_ = std::max<std::size_t>(parsed.columnCount, 1);
    doc.rows_.reserve(parsed.rows.size());
    for (Row& row : parsed.rows) {
        doc.rows_.push_back(std::make_shared<Row>(std::move(row)));
    }
    doc.delimiter = parsed.delimiter;
    doc.crlf = parsed.crlf;
    doc.trailingNewline = parsed.trailingNewline;
    return doc;
}

const std::string& Document::Cell(std::size_t row, std::size_t col) const {
    if (row >= rows_.size() || col >= rows_[row]->size()) {
        return kEmptyCell;
    }
    return (*rows_[row])[col];
}

const std::string& Document::Formula(std::size_t row, std::size_t col) const {
    const auto it = formulas_.find({row, col});
    return it == formulas_.end() ? kEmptyCell : it->second.text;
}

std::vector<std::pair<CellKey, std::string>> Document::FormulaList() const {
    std::vector<std::pair<CellKey, std::string>> list;
    list.reserve(formulas_.size());
    for (const auto& [key, entry] : formulas_) {
        list.emplace_back(key, entry.text);
    }
    return list;
}

void Document::GrowRows(std::size_t count) {
    while (rows_.size() < count) {
        rows_.push_back(std::make_shared<Row>());
    }
}

Row& Document::MutableRow(std::size_t row) {
    RowPtr& ptr = rows_[row];
    if (ptr.use_count() > 1) {
        ptr = std::make_shared<Row>(*ptr);
    }
    return *ptr;
}

void Document::SetRaw(std::size_t row, std::size_t col, const std::string& value) {
    GrowRows(row + 1);
    // Skip identical writes so recalculation does not unshare rows held by undo snapshots.
    if (col < rows_[row]->size() && (*rows_[row])[col] == value) {
        return;
    }
    Row& r = MutableRow(row);
    if (col >= r.size()) {
        r.resize(col + 1);
    }
    r[col] = value;
    columnCount_ = std::max(columnCount_, r.size());
}

void Document::SetFormula(std::size_t row, std::size_t col, const std::string& text) {
    if (text.empty()) {
        formulas_.erase({row, col});
    } else {
        formulas_[{row, col}] = {text, ParseFormula(text)};
    }
}

void Document::Recalculate() {
    // Every mutation ends here, so this doubles as a change counter for caches.
    static std::uint64_t counter = 0;
    revision_ = ++counter;
    if (formulas_.empty()) {
        return;
    }
    std::vector<std::pair<CellKey, std::string>> results;
    results.reserve(formulas_.size());
    {
        Evaluator evaluator(*this);
        // Row-major order keeps dependency chains shallow in the common case.
        for (const auto& entry : formulas_) {
            results.emplace_back(entry.first, FormatValue(evaluator.GetCell(entry.first.first, entry.first.second)));
        }
    }
    for (const auto& [key, value] : results) {
        SetRaw(key.first, key.second, value);
    }
}

void Document::LoadFormulas(const std::vector<std::pair<CellKey, std::string>>& formulas) {
    formulas_.clear();
    for (const auto& [key, text] : formulas) {
        if (text.empty()) {
            continue;
        }
        const std::string current = Cell(key.first, key.second);
        SetRaw(key.first, key.second, current);
        SetFormula(key.first, key.second, text);
    }
    Recalculate();
    undo_.clear();
    redo_.clear();
    savedDepth_ = 0;
}

void Document::PushCommand(Command command) {
    if (savedDepth_ && *savedDepth_ > undo_.size()) {
        savedDepth_.reset();
    }
    undo_.push_back(std::move(command));
    redo_.clear();
}

std::size_t Document::ApplyEdits(std::vector<CellEdit> edits) {
    Command command;
    command.oldRowCount = rows_.size();
    command.oldColumnCount = columnCount_;

    for (CellEdit& edit : edits) {
        const std::string& current = Cell(edit.row, edit.col);
        const std::string& existing = Formula(edit.row, edit.col);
        const bool unchanged = edit.isFormula ? existing == edit.value : (existing.empty() && current == edit.value);
        if (unchanged) {
            continue;
        }
        Change change{edit.row,
                      edit.col,
                      current,
                      edit.isFormula ? current : edit.value,
                      edit.row < rows_.size() ? rows_[edit.row]->size() : 0,
                      existing,
                      edit.isFormula ? edit.value : std::string()};
        SetRaw(change.row, change.col, change.newValue);
        SetFormula(change.row, change.col, change.newFormula);
        command.changes.push_back(std::move(change));
    }
    if (command.changes.empty()) {
        return 0;
    }

    command.newColumnCount = columnCount_;
    Recalculate();
    const std::size_t changed = command.changes.size();
    PushCommand(std::move(command));
    return changed;
}

std::shared_ptr<Document::Snapshot> Document::CaptureState() const {
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->rows = rows_;
    snapshot->columnCount = columnCount_;
    snapshot->formulas = formulas_;
    return snapshot;
}

void Document::SwapState(Snapshot& other) {
    std::swap(rows_, other.rows);
    std::swap(columnCount_, other.columnCount);
    std::swap(formulas_, other.formulas);
}

void Document::ShiftFormulasForInsert(Axis axis, std::size_t at, std::size_t count) {
    std::map<CellKey, FormulaEntry> next;
    for (auto& [key, entry] : formulas_) {
        CellKey moved = key;
        std::size_t& coord = axis == Axis::Row ? moved.first : moved.second;
        if (coord >= at) {
            coord += count;
        }
        std::string text = RewriteForInsert(entry.text, axis, at, count);
        if (text != entry.text) {
            entry.ast = ParseFormula(text);
            entry.text = std::move(text);
        }
        next.emplace(moved, std::move(entry));
    }
    formulas_ = std::move(next);
}

void Document::ShiftFormulasForDelete(Axis axis, std::size_t at, std::size_t count) {
    std::map<CellKey, FormulaEntry> next;
    for (auto& [key, entry] : formulas_) {
        CellKey moved = key;
        std::size_t& coord = axis == Axis::Row ? moved.first : moved.second;
        if (coord >= at && coord < at + count) {
            continue;
        }
        if (coord >= at + count) {
            coord -= count;
        }
        std::string text = RewriteForDelete(entry.text, axis, at, count);
        if (text != entry.text) {
            entry.ast = ParseFormula(text);
            entry.text = std::move(text);
        }
        next.emplace(moved, std::move(entry));
    }
    formulas_ = std::move(next);
}

void Document::InsertRowsRaw(std::size_t at, std::size_t count) {
    rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(at), count, std::make_shared<Row>(columnCount_));
    ShiftFormulasForInsert(Axis::Row, at, count);
}

bool Document::InsertRows(std::size_t at, std::size_t count) {
    if (count == 0) {
        return false;
    }
    at = std::min(at, rows_.size());
    Command command;
    command.snapshot = CaptureState();
    InsertRowsRaw(at, count);
    Recalculate();
    PushCommand(std::move(command));
    return true;
}

bool Document::DeleteRows(std::size_t at, std::size_t count) {
    if (count == 0 || at >= rows_.size()) {
        return false;
    }
    count = std::min(count, rows_.size() - at);
    Command command;
    command.snapshot = CaptureState();
    rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(at),
                rows_.begin() + static_cast<std::ptrdiff_t>(at + count));
    if (rows_.empty()) {
        rows_.push_back(std::make_shared<Row>(columnCount_));
    }
    ShiftFormulasForDelete(Axis::Row, at, count);
    Recalculate();
    PushCommand(std::move(command));
    return true;
}

bool Document::DeleteRowSet(const std::vector<std::size_t>& rows) {
    if (rows.empty() || rows.back() >= rows_.size()) {
        return false;
    }
    Command command;
    command.snapshot = CaptureState();
    // Delete runs from the bottom so earlier positions stay valid.
    std::size_t end = rows.size();
    while (end > 0) {
        std::size_t begin = end - 1;
        while (begin > 0 && rows[begin - 1] + 1 == rows[begin]) {
            --begin;
        }
        const std::size_t at = rows[begin];
        const std::size_t count = end - begin;
        rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(at),
                    rows_.begin() + static_cast<std::ptrdiff_t>(at + count));
        ShiftFormulasForDelete(Axis::Row, at, count);
        end = begin;
    }
    if (rows_.empty()) {
        rows_.push_back(std::make_shared<Row>(columnCount_));
    }
    Recalculate();
    PushCommand(std::move(command));
    return true;
}

bool Document::PermuteRows(const std::vector<std::size_t>& source) {
    if (source.size() != rows_.size()) {
        return false;
    }
    std::vector<std::size_t> target(source.size());
    std::vector<bool> seen(source.size(), false);
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] >= source.size() || seen[source[i]]) {
            return false;
        }
        seen[source[i]] = true;
        target[source[i]] = i;
    }
    bool identity = true;
    for (std::size_t i = 0; i < source.size() && identity; ++i) {
        identity = source[i] == i;
    }
    if (identity) {
        return false;
    }

    Command command;
    command.snapshot = CaptureState();
    std::vector<RowPtr> reordered;
    reordered.reserve(rows_.size());
    for (const std::size_t from : source) {
        reordered.push_back(std::move(rows_[from]));
    }
    rows_ = std::move(reordered);

    std::map<CellKey, FormulaEntry> moved;
    for (auto& [key, entry] : formulas_) {
        const std::size_t row = target[key.first];
        std::string text = RewriteForShift(entry.text, static_cast<long long>(row) - static_cast<long long>(key.first), 0);
        if (text != entry.text) {
            entry.ast = ParseFormula(text);
            entry.text = std::move(text);
        }
        moved.emplace(CellKey{row, key.second}, std::move(entry));
    }
    formulas_ = std::move(moved);
    Recalculate();
    PushCommand(std::move(command));
    return true;
}

bool Document::DuplicateRow(std::size_t row) {
    if (row >= rows_.size()) {
        return false;
    }
    Command command;
    command.snapshot = CaptureState();
    InsertRowsRaw(row + 1, 1);
    rows_[row + 1] = rows_[row];

    std::vector<std::pair<CellKey, std::string>> copies;
    for (const auto& [key, entry] : formulas_) {
        if (key.first == row) {
            copies.emplace_back(CellKey{row + 1, key.second}, RewriteForShift(entry.text, 1, 0));
        }
    }
    for (const auto& [key, text] : copies) {
        SetFormula(key.first, key.second, text);
    }
    Recalculate();
    PushCommand(std::move(command));
    return true;
}

bool Document::InsertColumns(std::size_t at, std::size_t count) {
    if (count == 0) {
        return false;
    }
    at = std::min(at, columnCount_);
    Command command;
    command.snapshot = CaptureState();
    for (std::size_t r = 0; r < rows_.size(); ++r) {
        if (at < rows_[r]->size()) {
            Row& row = MutableRow(r);
            row.insert(row.begin() + static_cast<std::ptrdiff_t>(at), count, std::string());
        }
    }
    columnCount_ += count;
    ShiftFormulasForInsert(Axis::Column, at, count);
    Recalculate();
    PushCommand(std::move(command));
    return true;
}

bool Document::DeleteColumns(std::size_t at, std::size_t count) {
    if (count == 0 || at >= columnCount_) {
        return false;
    }
    count = std::min(count, columnCount_ - at);
    Command command;
    command.snapshot = CaptureState();
    for (std::size_t r = 0; r < rows_.size(); ++r) {
        if (at < rows_[r]->size()) {
            Row& row = MutableRow(r);
            row.erase(row.begin() + static_cast<std::ptrdiff_t>(at),
                      row.begin() + static_cast<std::ptrdiff_t>(std::min(row.size(), at + count)));
        }
    }
    columnCount_ = std::max<std::size_t>(columnCount_ - count, 1);
    ShiftFormulasForDelete(Axis::Column, at, count);
    Recalculate();
    PushCommand(std::move(command));
    return true;
}

bool Document::Undo() {
    if (undo_.empty()) {
        return false;
    }
    Command command = std::move(undo_.back());
    undo_.pop_back();

    if (command.snapshot) {
        SwapState(*command.snapshot);
        redo_.push_back(std::move(command));
        return true;
    }

    for (auto it = command.changes.rbegin(); it != command.changes.rend(); ++it) {
        SetFormula(it->row, it->col, it->oldFormula);
        if (it->row >= rows_.size()) {
            continue;
        }
        Row& r = MutableRow(it->row);
        if (it->col < r.size()) {
            r[it->col] = it->oldValue;
        }
        if (r.size() > it->oldRowSize) {
            r.resize(it->oldRowSize);
        }
    }
    if (rows_.size() > command.oldRowCount) {
        rows_.resize(command.oldRowCount);
    }
    GrowRows(command.oldRowCount);
    columnCount_ = command.oldColumnCount;
    Recalculate();
    redo_.push_back(std::move(command));
    return true;
}

bool Document::Redo() {
    if (redo_.empty()) {
        return false;
    }
    Command command = std::move(redo_.back());
    redo_.pop_back();

    if (command.snapshot) {
        SwapState(*command.snapshot);
        undo_.push_back(std::move(command));
        return true;
    }

    for (const Change& change : command.changes) {
        SetRaw(change.row, change.col, change.newValue);
        SetFormula(change.row, change.col, change.newFormula);
    }
    columnCount_ = command.newColumnCount;
    Recalculate();
    undo_.push_back(std::move(command));
    return true;
}

bool Document::IsDirty() const {
    return !savedDepth_ || *savedDepth_ != undo_.size();
}

void Document::MarkSaved() {
    savedDepth_ = undo_.size();
}

std::string Document::Serialize() const {
    if (!trimTrailingEmpty) {
        return SerializeCsvRows(
            rows_.size(), [&](std::size_t i) -> const Row& { return *rows_[i]; }, delimiter, crlf, trailingNewline);
    }

    std::size_t lastRow = 0;
    std::size_t width = 0;
    bool any = false;
    for (std::size_t r = 0; r < rows_.size(); ++r) {
        for (std::size_t c = 0; c < rows_[r]->size(); ++c) {
            if (!(*rows_[r])[c].empty()) {
                any = true;
                lastRow = r;
                width = std::max(width, c + 1);
            }
        }
    }

    Table trimmed;
    if (any) {
        for (std::size_t r = 0; r <= lastRow; ++r) {
            trimmed.push_back(*rows_[r]);
            trimmed.back().resize(width);
        }
    }
    return SerializeCsv(trimmed, delimiter, crlf, trailingNewline);
}

}  // namespace sheetpad
