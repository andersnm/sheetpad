#include "view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <numeric>
#include <regex>
#include <unordered_map>

namespace sheetpad {

namespace {

struct SortKey {
    int kind = 2;  // 0 number, 1 text, 2 blank
    double number = 0;
    std::string text;
};

std::string Trim(const std::string& text) {
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    std::size_t start = 0;
    std::size_t end = text.size();
    while (start < end && isSpace(text[start])) {
        ++start;
    }
    while (end > start && isSpace(text[end - 1])) {
        --end;
    }
    return text.substr(start, end - start);
}

bool ParseNumber(const std::string& trimmed, double& out) {
    if (trimmed.empty()) {
        return false;
    }
    for (const char c : trimmed) {
        if (!((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E')) {
            return false;
        }
    }
    char* end = nullptr;
    out = std::strtod(trimmed.c_str(), &end);
    return end == trimmed.c_str() + trimmed.size() && std::isfinite(out);
}

std::string CollationKey(const std::string& text) {
    const TextHooks& hooks = GetTextHooks();
    return hooks.sortKey ? hooks.sortKey(text) : FoldCase(text);
}

SortKey MakeKey(const std::string& value) {
    SortKey key;
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) {
        return key;
    }
    if (ParseNumber(trimmed, key.number)) {
        key.kind = 0;
    } else {
        key.kind = 1;
        key.text = CollationKey(value);
    }
    return key;
}

int CompareKeys(const SortKey& a, const SortKey& b) {
    if (a.kind != b.kind) {
        return a.kind < b.kind ? -1 : 1;
    }
    if (a.kind == 0) {
        return a.number < b.number ? -1 : (a.number > b.number ? 1 : 0);
    }
    if (a.kind == 1) {
        const int c = a.text.compare(b.text);
        return c < 0 ? -1 : (c > 0 ? 1 : 0);
    }
    return 0;
}

}  // namespace

TextHooks& GetTextHooks() {
    static TextHooks hooks;
    return hooks;
}

std::string FoldCase(const std::string& text) {
    const TextHooks& hooks = GetTextHooks();
    const bool ascii = std::all_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    if (hooks.lower && !ascii) {
        return hooks.lower(text);
    }
    std::string out = text;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

int CompareForSort(const std::string& a, const std::string& b) {
    return CompareKeys(MakeKey(a), MakeKey(b));
}

const char* FilterOpName(FilterOp op) {
    switch (op) {
    case FilterOp::Equals: return "=";
    case FilterOp::NotEquals: return "<>";
    case FilterOp::Greater: return ">";
    case FilterOp::GreaterEqual: return ">=";
    case FilterOp::Less: return "<";
    case FilterOp::LessEqual: return "<=";
    case FilterOp::Contains: return "contains";
    case FilterOp::NotContains: return "notcontains";
    case FilterOp::BeginsWith: return "begins";
    case FilterOp::EndsWith: return "ends";
    }
    return "=";
}

std::optional<FilterOp> ParseFilterOp(const std::string& name) {
    static const FilterOp all[] = {FilterOp::Equals,      FilterOp::NotEquals,   FilterOp::Greater,
                                   FilterOp::GreaterEqual, FilterOp::Less,        FilterOp::LessEqual,
                                   FilterOp::Contains,     FilterOp::NotContains, FilterOp::BeginsWith,
                                   FilterOp::EndsWith};
    for (const FilterOp op : all) {
        if (name == FilterOpName(op)) {
            return op;
        }
    }
    return std::nullopt;
}

bool ConditionMatches(FilterOp op, const std::string& operand, const std::string& cell) {
    switch (op) {
    case FilterOp::Contains:
    case FilterOp::NotContains:
    case FilterOp::BeginsWith:
    case FilterOp::EndsWith: {
        const std::string text = FoldCase(cell);
        const std::string needle = FoldCase(operand);
        bool found = false;
        if (op == FilterOp::BeginsWith) {
            found = text.compare(0, needle.size(), needle) == 0 && text.size() >= needle.size();
        } else if (op == FilterOp::EndsWith) {
            found = text.size() >= needle.size() && text.compare(text.size() - needle.size(), needle.size(), needle) == 0;
        } else {
            found = text.find(needle) != std::string::npos;
        }
        return op == FilterOp::NotContains ? !found : found;
    }
    default:
        break;
    }

    const SortKey cellKey = MakeKey(cell);
    const SortKey operandKey = MakeKey(operand);
    if (cellKey.kind == 2 || operandKey.kind == 2) {
        const bool bothBlank = cellKey.kind == 2 && operandKey.kind == 2;
        if (op == FilterOp::Equals) {
            return bothBlank;
        }
        return op == FilterOp::NotEquals && !bothBlank;
    }
    const int c = CompareKeys(cellKey, operandKey);
    switch (op) {
    case FilterOp::Equals: return c == 0;
    case FilterOp::NotEquals: return c != 0;
    case FilterOp::Greater: return c > 0;
    case FilterOp::GreaterEqual: return c >= 0;
    case FilterOp::Less: return c < 0;
    case FilterOp::LessEqual: return c <= 0;
    default: return false;
    }
}

bool RowView::Matches(const Document& doc, std::size_t row) const {
    for (const auto& [column, filter] : filters) {
        const std::string& value = doc.Cell(row, column);
        const bool pass = filter.isList ? filter.allowed.count(value) != 0
                                        : ConditionMatches(filter.op, filter.operand, value);
        if (!pass) {
            return false;
        }
    }
    return true;
}

void RowView::Rebuild(const Document& doc) {
    rows_.clear();
    builtRows_ = doc.RowCount();
    builtColumns_ = doc.ColumnCount();
    const std::size_t header = std::min(headerRows, builtRows_);
    for (std::size_t r = 0; r < header; ++r) {
        rows_.push_back(r);
    }
    std::vector<std::size_t> body;
    for (std::size_t r = header; r < builtRows_; ++r) {
        if (Matches(doc, r)) {
            body.push_back(r);
        }
    }

    if (!sorts.empty() && !body.empty()) {
        const std::size_t levels = sorts.size();
        // Per level: numbers compare by value, text by the rank of its collation key. Each distinct text is
        // keyed once, which keeps the locale-aware key calls to one per distinct value.
        struct LevelKey {
            std::uint8_t kind = 2;
            double number = 0;
            std::uint32_t rank = 0;
        };
        std::vector<LevelKey> keys(body.size() * levels);
        for (std::size_t l = 0; l < levels; ++l) {
            std::unordered_map<std::string, std::uint32_t> ids;
            std::vector<std::string> collation;
            std::vector<std::uint32_t> textId(body.size(), 0);
            for (std::size_t i = 0; i < body.size(); ++i) {
                const std::string& cell = doc.Cell(body[i], sorts[l].column);
                LevelKey& key = keys[i * levels + l];
                const std::string trimmed = Trim(cell);
                if (trimmed.empty()) {
                    continue;
                }
                if (ParseNumber(trimmed, key.number)) {
                    key.kind = 0;
                    continue;
                }
                key.kind = 1;
                const auto [it, added] = ids.try_emplace(cell, static_cast<std::uint32_t>(collation.size()));
                if (added) {
                    collation.push_back(CollationKey(cell));
                }
                textId[i] = it->second;
            }
            std::vector<std::uint32_t> order(collation.size());
            std::iota(order.begin(), order.end(), std::uint32_t{0});
            std::sort(order.begin(), order.end(),
                      [&](std::uint32_t a, std::uint32_t b) { return collation[a] < collation[b]; });
            std::vector<std::uint32_t> rankOf(collation.size(), 0);
            std::uint32_t rank = 0;
            for (std::size_t k = 0; k < order.size(); ++k) {
                if (k > 0 && collation[order[k]] != collation[order[k - 1]]) {
                    ++rank;
                }
                rankOf[order[k]] = rank;
            }
            for (std::size_t i = 0; i < body.size(); ++i) {
                if (keys[i * levels + l].kind == 1) {
                    keys[i * levels + l].rank = rankOf[textId[i]];
                }
            }
        }
        std::vector<std::size_t> order(body.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            for (std::size_t l = 0; l < levels; ++l) {
                const LevelKey& ka = keys[a * levels + l];
                const LevelKey& kb = keys[b * levels + l];
                if (ka.kind == 2 || kb.kind == 2) {
                    if (ka.kind == kb.kind) {
                        continue;
                    }
                    return kb.kind == 2;  // blanks last in both directions
                }
                int c = 0;
                if (ka.kind != kb.kind) {
                    c = ka.kind < kb.kind ? -1 : 1;
                } else if (ka.kind == 0) {
                    c = ka.number < kb.number ? -1 : (ka.number > kb.number ? 1 : 0);
                } else {
                    c = ka.rank < kb.rank ? -1 : (ka.rank > kb.rank ? 1 : 0);
                }
                if (sorts[l].descending) {
                    c = -c;
                }
                if (c != 0) {
                    return c < 0;
                }
            }
            return false;
        });
        for (const std::size_t i : order) {
            rows_.push_back(body[i]);
        }
    } else {
        rows_.insert(rows_.end(), body.begin(), body.end());
    }
}

std::size_t RowView::DocRow(std::size_t displayRow, const Document& doc) const {
    if (!Active()) {
        return displayRow;
    }
    return displayRow < rows_.size() ? rows_[displayRow] : doc.RowCount() + (displayRow - rows_.size());
}

std::optional<std::size_t> RowView::DisplayRow(std::size_t docRow, const Document& doc) const {
    if (!Active()) {
        return docRow;
    }
    const auto it = std::find(rows_.begin(), rows_.end(), docRow);
    if (it != rows_.end()) {
        return static_cast<std::size_t>(it - rows_.begin());
    }
    if (docRow >= doc.RowCount()) {
        return rows_.size() + (docRow - doc.RowCount());
    }
    return std::nullopt;
}

std::vector<ValueCount> RowView::DistinctValues(const Document& doc, std::size_t column,
                                                bool respectOtherFilters) const {
    std::unordered_map<std::string, std::size_t> counts;
    for (std::size_t r = std::min(headerRows, doc.RowCount()); r < doc.RowCount(); ++r) {
        bool pass = true;
        if (respectOtherFilters) {
            for (const auto& [other, filter] : filters) {
                if (other == column) {
                    continue;
                }
                const std::string& value = doc.Cell(r, other);
                if (!(filter.isList ? filter.allowed.count(value) != 0
                                    : ConditionMatches(filter.op, filter.operand, value))) {
                    pass = false;
                    break;
                }
            }
        }
        if (pass) {
            ++counts[doc.Cell(r, column)];
        }
    }

    struct Entry {
        SortKey key;
        ValueCount item;
    };
    std::vector<Entry> entries;
    entries.reserve(counts.size());
    for (auto& [value, count] : counts) {
        entries.push_back({MakeKey(value), {value, count}});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        const int c = CompareKeys(a.key, b.key);
        return c != 0 ? c < 0 : a.item.value < b.item.value;
    });
    std::vector<ValueCount> result;
    result.reserve(entries.size());
    for (Entry& entry : entries) {
        result.push_back(std::move(entry.item));
    }
    return result;
}

void RowView::RowsInserted(std::size_t docAt, std::size_t count, std::size_t displayAt) {
    builtRows_ += count;
    if (!Active()) {
        return;
    }
    for (std::size_t& row : rows_) {
        if (row >= docAt) {
            row += count;
        }
    }
    displayAt = std::min(displayAt, rows_.size());
    std::vector<std::size_t> added(count);
    std::iota(added.begin(), added.end(), docAt);
    rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(displayAt), added.begin(), added.end());
}

void RowView::RowsDeleted(const std::vector<std::size_t>& sortedDocRows) {
    builtRows_ -= std::min(builtRows_, sortedDocRows.size());
    if (!Active()) {
        return;
    }
    std::vector<std::size_t> kept;
    kept.reserve(rows_.size());
    for (const std::size_t row : rows_) {
        const auto below = std::lower_bound(sortedDocRows.begin(), sortedDocRows.end(), row);
        if (below != sortedDocRows.end() && *below == row) {
            continue;
        }
        kept.push_back(row - static_cast<std::size_t>(below - sortedDocRows.begin()));
    }
    rows_ = std::move(kept);
}

void RowView::ColumnsInserted(std::size_t at, std::size_t count) {
    builtColumns_ += count;
    std::map<std::size_t, ColumnFilter> moved;
    for (auto& [column, filter] : filters) {
        moved.emplace(column >= at ? column + count : column, std::move(filter));
    }
    filters = std::move(moved);
    std::map<std::size_t, int> widths;
    for (const auto& [column, width] : columnWidths) {
        widths.emplace(column >= at ? column + count : column, width);
    }
    columnWidths = std::move(widths);
    for (SortLevel& level : sorts) {
        if (level.column >= at) {
            level.column += count;
        }
    }
}

void RowView::ColumnsDeleted(std::size_t at, std::size_t count) {
    builtColumns_ = std::max<std::size_t>(builtColumns_ - std::min(builtColumns_, count), 1);
    std::map<std::size_t, ColumnFilter> moved;
    for (auto& [column, filter] : filters) {
        if (column >= at && column < at + count) {
            continue;
        }
        moved.emplace(column >= at + count ? column - count : column, std::move(filter));
    }
    filters = std::move(moved);
    std::map<std::size_t, int> widths;
    for (const auto& [column, width] : columnWidths) {
        if (column >= at && column < at + count) {
            continue;
        }
        widths.emplace(column >= at + count ? column - count : column, width);
    }
    columnWidths = std::move(widths);
    std::vector<SortLevel> kept;
    for (SortLevel level : sorts) {
        if (level.column >= at && level.column < at + count) {
            continue;
        }
        if (level.column >= at + count) {
            level.column -= count;
        }
        kept.push_back(level);
    }
    sorts = std::move(kept);
}

std::vector<std::size_t> RowView::SortedPermutation(const Document& doc) const {
    std::vector<std::size_t> source(doc.RowCount());
    std::iota(source.begin(), source.end(), std::size_t{0});
    const std::size_t header = std::min(headerRows, rows_.size());
    std::vector<std::size_t> body(rows_.begin() + static_cast<std::ptrdiff_t>(header), rows_.end());
    std::vector<std::size_t> positions = body;
    std::sort(positions.begin(), positions.end());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        if (positions[i] < source.size()) {
            source[positions[i]] = body[i];
        }
    }
    return source;
}

struct CellMatcher::Impl {
    FindOptions options;
    std::string needle;
    std::optional<std::regex> pattern;
    bool valid = true;
};

CellMatcher::CellMatcher(const FindOptions& options) : impl_(std::make_unique<Impl>()) {
    impl_->options = options;
    if (options.regex && !options.text.empty()) {
        try {
            auto flags = std::regex::ECMAScript;
            if (!options.matchCase) {
                flags |= std::regex::icase;
            }
            impl_->pattern.emplace(options.text, flags);
        } catch (const std::regex_error&) {
            impl_->valid = false;
        }
    } else {
        impl_->needle = options.matchCase ? options.text : FoldCase(options.text);
    }
}

CellMatcher::~CellMatcher() = default;
CellMatcher::CellMatcher(CellMatcher&&) noexcept = default;
CellMatcher& CellMatcher::operator=(CellMatcher&&) noexcept = default;

bool CellMatcher::Valid() const {
    return impl_->valid;
}

bool CellMatcher::Empty() const {
    return impl_->options.text.empty();
}

bool CellMatcher::Matches(const std::string& cell) const {
    if (!impl_->valid || impl_->options.text.empty() || cell.empty()) {
        return false;
    }
    if (impl_->pattern) {
        try {
            return impl_->options.wholeCell ? std::regex_match(cell, *impl_->pattern)
                                            : std::regex_search(cell, *impl_->pattern);
        } catch (const std::regex_error&) {
            return false;
        }
    }
    const std::string text = impl_->options.matchCase ? cell : FoldCase(cell);
    return impl_->options.wholeCell ? text == impl_->needle : text.find(impl_->needle) != std::string::npos;
}

}  // namespace sheetpad
