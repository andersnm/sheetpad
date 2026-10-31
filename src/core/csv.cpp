#include "csv.h"

#include <algorithm>
#include <array>

namespace sheetpad {

namespace {

bool NeedsQuoting(std::string_view field, char delimiter) {
    return field.find_first_of(std::string{delimiter, '"', '\r', '\n'}) != std::string_view::npos;
}

void AppendField(std::string& out, std::string_view field, char delimiter) {
    if (!NeedsQuoting(field, delimiter)) {
        out.append(field);
        return;
    }
    out.push_back('"');
    for (const char c : field) {
        if (c == '"') {
            out.push_back('"');
        }
        out.push_back(c);
    }
    out.push_back('"');
}

void AppendHtmlEscaped(std::string& out, std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\r':
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            out += "<br>";
            break;
        case '\n': out += "<br>"; break;
        default: out.push_back(c); break;
        }
    }
}

}  // namespace

char DetectDelimiter(std::string_view text) {
    constexpr std::array<char, 4> candidates{',', ';', '\t', '|'};
    std::array<int, 4> counts{};
    bool inQuotes = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"') {
            if (inQuotes && i + 1 < text.size() && text[i + 1] == '"') {
                ++i;
            } else {
                inQuotes = !inQuotes;
            }
            continue;
        }
        if (inQuotes) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            break;
        }
        for (std::size_t k = 0; k < candidates.size(); ++k) {
            if (c == candidates[k]) {
                ++counts[k];
            }
        }
    }
    const auto best = std::max_element(counts.begin(), counts.end());
    return *best == 0 ? ',' : candidates[static_cast<std::size_t>(best - counts.begin())];
}

ParsedCsv ParseCsv(std::string_view text, char delimiter) {
    ParsedCsv result;
    result.delimiter = delimiter;

    Row fields;
    std::string field;
    bool inQuotes = false;
    bool started = false;
    bool newlineSeen = false;
    bool endedWithNewline = false;

    const auto finishRecord = [&]() {
        fields.push_back(std::move(field));
        field.clear();
        result.columnCount = std::max(result.columnCount, fields.size());
        result.rows.push_back(std::move(fields));
        fields.clear();
        started = false;
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        endedWithNewline = false;

        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field.push_back(c);
            }
            continue;
        }

        if (c == '"' && field.empty()) {
            inQuotes = true;
            started = true;
        } else if (c == delimiter) {
            fields.push_back(std::move(field));
            field.clear();
            started = true;
        } else if (c == '\r' || c == '\n') {
            const bool crlf = c == '\r' && i + 1 < text.size() && text[i + 1] == '\n';
            if (crlf) {
                ++i;
            }
            if (!newlineSeen) {
                newlineSeen = true;
                result.crlf = crlf;
            }
            finishRecord();
            endedWithNewline = true;
        } else {
            field.push_back(c);
            started = true;
        }
    }

    result.unterminatedQuote = inQuotes;
    if (started || !field.empty() || !fields.empty()) {
        finishRecord();
        endedWithNewline = false;
    }
    result.trailingNewline = endedWithNewline || result.rows.empty();
    return result;
}

char DetectPasteDelimiter(std::string_view text) {
    if (text.find('\t') != std::string_view::npos) {
        return '\t';
    }
    for (const char delimiter : {',', ';', '|'}) {
        const ParsedCsv parsed = ParseCsv(text, delimiter);
        if (parsed.rows.size() < 2 || parsed.columnCount < 2) {
            continue;
        }
        const bool rectangular = std::all_of(
            parsed.rows.begin(), parsed.rows.end(),
            [&](const Row& row) { return row.size() == parsed.columnCount; });
        if (rectangular) {
            return delimiter;
        }
    }
    return '\t';
}

std::string SerializeCsv(const Table& rows, char delimiter, bool crlf, bool trailingNewline) {
    return SerializeCsvRows(
        rows.size(), [&](std::size_t i) -> const Row& { return rows[i]; }, delimiter, crlf, trailingNewline);
}

std::string SerializeCsvRows(std::size_t count, const std::function<const Row&(std::size_t)>& rowAt, char delimiter,
                             bool crlf, bool trailingNewline) {
    const char* newline = crlf ? "\r\n" : "\n";
    std::string out;
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) {
            out += newline;
        }
        const Row& row = rowAt(i);
        for (std::size_t j = 0; j < row.size(); ++j) {
            if (j > 0) {
                out.push_back(delimiter);
            }
            AppendField(out, row[j], delimiter);
        }
    }
    if (count > 0 && trailingNewline) {
        out += newline;
    }
    return out;
}

std::string BuildTsv(const Table& rows) {
    return SerializeCsv(rows, '\t', true, false);
}

std::string BuildHtmlTable(const Table& rows) {
    std::string out = "<table border=\"1\" cellspacing=\"0\" cellpadding=\"3\">";
    for (const Row& row : rows) {
        out += "<tr>";
        for (const std::string& cell : row) {
            out += "<td>";
            AppendHtmlEscaped(out, cell);
            out += "</td>";
        }
        out += "</tr>";
    }
    out += "</table>";
    return out;
}

bool IsValidUtf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto byte = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        unsigned int codePoint = 0;
        if (byte < 0x80) {
            ++i;
            continue;
        } else if (byte >= 0xC2 && byte <= 0xDF) {
            extra = 1;
            codePoint = byte & 0x1Fu;
        } else if (byte >= 0xE0 && byte <= 0xEF) {
            extra = 2;
            codePoint = byte & 0x0Fu;
        } else if (byte >= 0xF0 && byte <= 0xF4) {
            extra = 3;
            codePoint = byte & 0x07u;
        } else {
            return false;
        }
        if (i + extra >= text.size()) {
            return false;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) {
                return false;
            }
            codePoint = (codePoint << 6) | (next & 0x3Fu);
        }
        const unsigned int minimum = extra == 1 ? 0x80u : extra == 2 ? 0x800u : 0x10000u;
        if (codePoint < minimum || codePoint > 0x10FFFFu ||
            (codePoint >= 0xD800u && codePoint <= 0xDFFFu)) {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

}  // namespace sheetpad
