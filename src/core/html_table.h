#pragma once

#include <string>
#include <string_view>

#include "csv.h"

namespace sheetpad {

// How cells covered by a colspan/rowspan are filled, other than the cell that owns the text. Ignore treats the
// spans as absent, so every cell covers exactly one position.
enum class SpanFill { Blank, Repeat, Ignore };

// Returns the HTML fragment of a Windows "HTML Format" clipboard payload (the bytes between StartFragment and
// EndFragment from its header). Input without a usable header is returned as is.
std::string_view ExtractClipboardHtmlFragment(std::string_view clipboard);

// Parses all content of `html` (UTF-8) into rows of cell text, in document order. Each table contributes its rows
// (which may differ in length); text outside tables contributes one single-cell row per line, split at <br> and
// block elements. Within cells whitespace is collapsed as a browser would, <br> and block ends become newlines,
// entities are decoded and nested tags are dropped. Returns false when nothing was found.
bool ParseHtml(std::string_view html, Table& out, SpanFill fill = SpanFill::Blank);

}  // namespace sheetpad
