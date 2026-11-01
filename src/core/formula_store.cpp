#include "formula_store.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string_view>
#include <vector>

namespace sheetpad {

namespace {

void AppendEscaped(std::string& out, const std::string& text) {
    out.push_back('"');
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
                out += buffer;
            } else {
                out.push_back(c);
            }
        }
    }
    out.push_back('"');
}

void AppendObject(std::string& out, const char* name, const std::map<std::string, std::string>& entries) {
    out += "  \"";
    out += name;
    out += "\": {\n";
    std::size_t index = 0;
    for (const auto& [key, value] : entries) {
        out += "    ";
        AppendEscaped(out, key);
        out += ": ";
        AppendEscaped(out, value);
        out += ++index < entries.size() ? ",\n" : "\n";
    }
    out += "  }";
}

void AppendUtf8(std::string& out, unsigned code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

// Minimal JSON reader: understands objects of string-to-string maps under known keys and skips the rest.
class Reader {
public:
    explicit Reader(const std::string& text) : s_(text) {
        if (s_.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            pos_ = 3;
        }
    }

    struct Parsed {
        std::map<std::string, std::string> cells;
        std::map<std::string, std::string> columns;
        std::vector<std::pair<std::string, ColumnFilter>> filters;
        std::vector<std::pair<std::string, int>> widths;
        bool headerRow = false;
    };

    bool ParseDocument(Parsed& parsed) {
        if (!Expect('{')) {
            return false;
        }
        SkipSpace();
        if (Peek() == '}') {
            ++pos_;
            return AtEnd();
        }
        while (true) {
            std::string key;
            if (!ReadString(key) || !Expect(':')) {
                return false;
            }
            if (key == "cells" || key == "columns") {
                if (!ReadStringMap(key == "cells" ? parsed.cells : parsed.columns)) {
                    return false;
                }
            } else if (key == "filters") {
                if (!ReadFilters(parsed.filters)) {
                    return false;
                }
            } else if (key == "columnWidths") {
                if (!ReadWidths(parsed.widths)) {
                    return false;
                }
            } else if (key == "headerRow") {
                SkipSpace();
                if (s_.compare(pos_, 4, "true") == 0) {
                    parsed.headerRow = true;
                    pos_ += 4;
                } else if (!SkipValue(0)) {
                    return false;
                }
            } else if (!SkipValue(0)) {
                return false;
            }
            SkipSpace();
            if (Peek() == ',') {
                ++pos_;
                continue;
            }
            if (!Expect('}')) {
                return false;
            }
            return AtEnd();
        }
    }

private:
    // Reads {"A": 120, ...}; entries whose value is not a number are ignored.
    bool ReadWidths(std::vector<std::pair<std::string, int>>& out) {
        if (!Expect('{')) {
            return false;
        }
        SkipSpace();
        if (Peek() == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            std::string column;
            if (!ReadString(column) || !Expect(':')) {
                return false;
            }
            SkipSpace();
            const std::size_t start = pos_;
            if (!SkipValue(0)) {
                return false;
            }
            const std::string token = s_.substr(start, pos_ - start);
            char* end = nullptr;
            const double value = std::strtod(token.c_str(), &end);
            if (!token.empty() && token.front() != '"' && end == token.c_str() + token.size() && value >= 1 &&
                value <= 100000) {
                out.emplace_back(std::move(column), static_cast<int>(value + 0.5));
            }
            SkipSpace();
            if (Peek() == ',') {
                ++pos_;
                continue;
            }
            return Expect('}');
        }
    }

    bool ReadStringArray(std::set<std::string>& out) {
        if (!Expect('[')) {
            return false;
        }
        SkipSpace();
        if (Peek() == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            std::string value;
            if (!ReadString(value)) {
                return false;
            }
            out.insert(std::move(value));
            SkipSpace();
            if (Peek() == ',') {
                ++pos_;
                continue;
            }
            return Expect(']');
        }
    }

    bool ReadFilterObject(ColumnFilter& filter, bool& valid) {
        if (!Expect('{')) {
            return false;
        }
        SkipSpace();
        if (Peek() == '}') {
            ++pos_;
            valid = false;
            return true;
        }
        bool hasList = false;
        bool hasOp = false;
        while (true) {
            std::string field;
            if (!ReadString(field) || !Expect(':')) {
                return false;
            }
            if (field == "in") {
                if (!ReadStringArray(filter.allowed)) {
                    return false;
                }
                hasList = true;
            } else if (field == "op") {
                std::string name;
                if (!ReadString(name)) {
                    return false;
                }
                if (const auto op = ParseFilterOp(name)) {
                    filter.op = *op;
                    hasOp = true;
                }
            } else if (field == "value") {
                if (!ReadString(filter.operand)) {
                    return false;
                }
            } else if (!SkipValue(0)) {
                return false;
            }
            SkipSpace();
            if (Peek() == ',') {
                ++pos_;
                continue;
            }
            if (!Expect('}')) {
                return false;
            }
            filter.isList = hasList;
            valid = hasList || hasOp;
            return true;
        }
    }

    bool ReadFilters(std::vector<std::pair<std::string, ColumnFilter>>& out) {
        if (!Expect('{')) {
            return false;
        }
        SkipSpace();
        if (Peek() == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            std::string column;
            ColumnFilter filter;
            bool valid = false;
            if (!ReadString(column) || !Expect(':') || !ReadFilterObject(filter, valid)) {
                return false;
            }
            if (valid) {
                out.emplace_back(std::move(column), std::move(filter));
            }
            SkipSpace();
            if (Peek() == ',') {
                ++pos_;
                continue;
            }
            return Expect('}');
        }
    }
    char Peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }

    void SkipSpace() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\r' || s_[pos_] == '\n')) {
            ++pos_;
        }
    }

    bool AtEnd() {
        SkipSpace();
        return pos_ == s_.size();
    }

    bool Expect(char c) {
        SkipSpace();
        if (Peek() != c) {
            return false;
        }
        ++pos_;
        return true;
    }

    bool ReadHex4(unsigned& code) {
        if (pos_ + 4 > s_.size()) {
            return false;
        }
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            code <<= 4;
            if (c >= '0' && c <= '9') {
                code |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                code |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                code |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                return false;
            }
        }
        return true;
    }

    bool ReadString(std::string& out) {
        if (!Expect('"')) {
            return false;
        }
        out.clear();
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= s_.size()) {
                return false;
            }
            const char escape = s_[pos_++];
            switch (escape) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                unsigned code = 0;
                if (!ReadHex4(code)) {
                    return false;
                }
                if (code >= 0xD800 && code < 0xDC00 && s_.compare(pos_, 2, "\\u") == 0) {
                    pos_ += 2;
                    unsigned low = 0;
                    if (!ReadHex4(low) || low < 0xDC00 || low > 0xDFFF) {
                        return false;
                    }
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
                AppendUtf8(out, code);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool ReadStringMap(std::map<std::string, std::string>& out) {
        if (!Expect('{')) {
            return false;
        }
        SkipSpace();
        if (Peek() == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            std::string key;
            std::string value;
            if (!ReadString(key) || !Expect(':') || !ReadString(value)) {
                return false;
            }
            out[key] = value;
            SkipSpace();
            if (Peek() == ',') {
                ++pos_;
                continue;
            }
            return Expect('}');
        }
    }

    bool SkipValue(int depth) {
        if (depth > 64) {
            return false;
        }
        SkipSpace();
        const char c = Peek();
        if (c == '"') {
            std::string ignored;
            return ReadString(ignored);
        }
        if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++pos_;
            SkipSpace();
            if (Peek() == close) {
                ++pos_;
                return true;
            }
            while (true) {
                if (c == '{') {
                    std::string key;
                    if (!ReadString(key) || !Expect(':')) {
                        return false;
                    }
                }
                if (!SkipValue(depth + 1)) {
                    return false;
                }
                SkipSpace();
                if (Peek() == ',') {
                    ++pos_;
                    continue;
                }
                return Expect(close);
            }
        }
        const std::size_t start = pos_;
        while (pos_ < s_.size() && std::string_view(",}] \t\r\n").find(s_[pos_]) == std::string_view::npos) {
            ++pos_;
        }
        return pos_ > start;
    }

    const std::string& s_;
    std::size_t pos_ = 0;
};

}  // namespace

std::string BuildFormulaJson(const Document& doc, const RowView* view) {
    std::map<std::size_t, std::map<std::size_t, std::string>> ordered;
    for (const auto& [key, text] : doc.FormulaList()) {
        ordered[key.first][key.second] = text;
    }

    std::vector<std::string> sections;
    if (!ordered.empty()) {
        // Keep cells in row-major order rather than label order.
        std::string out = "  \"cells\": {\n";
        std::size_t remaining = doc.FormulaList().size();
        for (const auto& [row, columns] : ordered) {
            for (const auto& [col, text] : columns) {
                out += "    ";
                AppendEscaped(out, CellLabel(row, col));
                out += ": ";
                AppendEscaped(out, text);
                out += --remaining > 0 ? ",\n" : "\n";
            }
        }
        out += "  }";
        sections.push_back(std::move(out));
    }
    if (!doc.columnFormulas.empty()) {
        std::string out;
        AppendObject(out, "columns", doc.columnFormulas);
        sections.push_back(std::move(out));
    }
    if (view && !view->filters.empty()) {
        std::string out = "  \"filters\": {\n";
        std::size_t remaining = view->filters.size();
        for (const auto& [column, filter] : view->filters) {
            out += "    ";
            AppendEscaped(out, ColumnLabel(column));
            if (filter.isList) {
                out += ": {\"in\": [";
                bool first = true;
                for (const std::string& value : filter.allowed) {
                    out += first ? "" : ", ";
                    AppendEscaped(out, value);
                    first = false;
                }
                out += "]}";
            } else {
                out += ": {\"op\": ";
                AppendEscaped(out, FilterOpName(filter.op));
                out += ", \"value\": ";
                AppendEscaped(out, filter.operand);
                out += "}";
            }
            out += --remaining > 0 ? ",\n" : "\n";
        }
        out += "  }";
        sections.push_back(std::move(out));
    }
    if (view && !view->columnWidths.empty()) {
        std::string out = "  \"columnWidths\": {\n";
        std::size_t remaining = view->columnWidths.size();
        for (const auto& [column, width] : view->columnWidths) {
            out += "    ";
            AppendEscaped(out, ColumnLabel(column));
            out += ": " + std::to_string(width);
            out += --remaining > 0 ? ",\n" : "\n";
        }
        out += "  }";
        sections.push_back(std::move(out));
    }
    if (view && view->headerRows > 0) {
        sections.push_back("  \"headerRow\": true");
    }
    if (sections.empty()) {
        return {};
    }

    std::string out = "{\n";
    for (std::size_t i = 0; i < sections.size(); ++i) {
        out += sections[i];
        out += i + 1 < sections.size() ? ",\n" : "\n";
    }
    out += "}\n";
    return out;
}

bool ApplyFormulaJson(Document& doc, const std::string& json, RowView* view) {
    Reader::Parsed parsed;
    if (!Reader(json).ParseDocument(parsed)) {
        return false;
    }

    std::vector<std::pair<CellKey, std::string>> formulas;
    for (const auto& [label, text] : parsed.cells) {
        std::size_t row = 0;
        std::size_t col = 0;
        if (ParseCellLabel(label, row, col) && text.size() > 1 && text.front() == '=') {
            formulas.emplace_back(CellKey{row, col}, text);
        }
    }
    doc.columnFormulas = std::move(parsed.columns);
    doc.LoadFormulas(formulas);

    if (view) {
        view->filters.clear();
        for (auto& [label, filter] : parsed.filters) {
            std::size_t row = 0;
            std::size_t col = 0;
            if (ParseCellLabel(label + "1", row, col)) {
                view->filters.emplace(col, std::move(filter));
            }
        }
        view->headerRows = parsed.headerRow ? 1 : 0;
        view->columnWidths.clear();
        for (const auto& [label, width] : parsed.widths) {
            std::size_t row = 0;
            std::size_t col = 0;
            if (ParseCellLabel(label + "1", row, col)) {
                view->columnWidths[col] = width;
            }
        }
    }
    return true;
}

}  // namespace sheetpad
