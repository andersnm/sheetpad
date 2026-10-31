#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "document.h"

namespace sheetpad {

// Platform hooks for locale-aware text handling. Without them, ASCII case folding and byte order are used.
struct TextHooks {
    // Returns a key whose bytewise order is the locale's case-insensitive collation order.
    std::function<std::string(const std::string&)> sortKey;
    std::function<std::string(const std::string&)> lower;
};
TextHooks& GetTextHooks();

std::string FoldCase(const std::string& text);

// Excel ordering: numbers, then text (case-insensitive), then blanks. Returns <0, 0 or >0.
int CompareForSort(const std::string& a, const std::string& b);

enum class FilterOp {
    Equals,
    NotEquals,
    Greater,
    GreaterEqual,
    Less,
    LessEqual,
    Contains,
    NotContains,
    BeginsWith,
    EndsWith,
};

const char* FilterOpName(FilterOp op);
std::optional<FilterOp> ParseFilterOp(const std::string& name);
bool ConditionMatches(FilterOp op, const std::string& operand, const std::string& cell);

// A column filter is either a list of allowed values (IN LIST, where "" stands for blank cells) or a
// single condition.
struct ColumnFilter {
    bool isList = true;
    std::set<std::string> allowed;
    FilterOp op = FilterOp::Equals;
    std::string operand;
};

struct SortLevel {
    std::size_t column = 0;
    bool descending = false;
};

struct ValueCount {
    std::string value;
    std::size_t count = 0;
};

// A filtered and sorted view over the rows of a document. It never changes the document.
//
// While the view is active, `rows_` maps display rows to document rows. The map is rebuilt on request and kept
// in step with structural edits, so that editing a cell does not make its row jump or vanish.
class RowView {
public:
    std::map<std::size_t, ColumnFilter> filters;
    std::vector<SortLevel> sorts;
    // Leading rows that are never filtered or sorted (the CSV header).
    std::size_t headerRows = 0;

    bool Active() const { return !filters.empty() || !sorts.empty(); }

    void Rebuild(const Document& doc);

    std::size_t RowCount(const Document& doc) const { return Active() ? rows_.size() : doc.RowCount(); }
    // Rows past the end of the map are appended document rows.
    std::size_t DocRow(std::size_t displayRow, const Document& doc) const;
    std::optional<std::size_t> DisplayRow(std::size_t docRow, const Document& doc) const;
    const std::vector<std::size_t>& Rows() const { return rows_; }
    std::size_t BuiltRowCount() const { return builtRows_; }
    std::size_t BuiltColumnCount() const { return builtColumns_; }

    bool Matches(const Document& doc, std::size_t row) const;
    // Distinct displayed values of a column with their row counts, in sort order. Header rows are excluded.
    std::vector<ValueCount> DistinctValues(const Document& doc, std::size_t column, bool respectOtherFilters) const;

    // Keeps the map in step with structural edits (call after the document was changed).
    void RowsInserted(std::size_t docAt, std::size_t count, std::size_t displayAt);
    void RowsDeleted(const std::vector<std::size_t>& sortedDocRows);
    void ColumnsInserted(std::size_t at, std::size_t count);
    void ColumnsDeleted(std::size_t at, std::size_t count);

    // For "Apply sort to data": source[i] is the old row that moves to row i. Only the visible, non-header rows
    // are permuted, within the positions they occupy. Requires an up-to-date map.
    std::vector<std::size_t> SortedPermutation(const Document& doc) const;

private:
    std::vector<std::size_t> rows_;
    std::size_t builtRows_ = 0;
    std::size_t builtColumns_ = 0;
};

// Find: plain text, whole-cell, case-sensitive and regular-expression matching.
struct FindOptions {
    std::string text;
    bool matchCase = false;
    bool wholeCell = false;
    bool regex = false;
};

class CellMatcher {
public:
    explicit CellMatcher(const FindOptions& options);
    ~CellMatcher();
    CellMatcher(CellMatcher&&) noexcept;
    CellMatcher& operator=(CellMatcher&&) noexcept;

    bool Valid() const;
    bool Empty() const;
    bool Matches(const std::string& cell) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sheetpad
