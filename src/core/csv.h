#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sheetpad {

using Row = std::vector<std::string>;
using Table = std::vector<Row>;

struct ParsedCsv {
    Table rows;
    std::size_t columnCount = 0;
    char delimiter = ',';
    bool crlf = true;
    bool trailingNewline = true;
    bool unterminatedQuote = false;
};

// All text is UTF-8 without a BOM.
char DetectDelimiter(std::string_view text);
ParsedCsv ParseCsv(std::string_view text, char delimiter);

// Clipboard text prefers tabs; other delimiters are used only when the text
// is unambiguously a rectangular table, so prose containing commas stays in one cell.
char DetectPasteDelimiter(std::string_view text);

std::string SerializeCsv(const Table& rows, char delimiter, bool crlf, bool trailingNewline);
std::string SerializeCsvRows(std::size_t count, const std::function<const Row&(std::size_t)>& rowAt, char delimiter,
                             bool crlf, bool trailingNewline);
std::string BuildTsv(const Table& rows);
std::string BuildHtmlTable(const Table& rows);

bool IsValidUtf8(std::string_view text);

}  // namespace sheetpad
