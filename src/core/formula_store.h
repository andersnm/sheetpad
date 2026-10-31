#pragma once

#include <string>

#include "document.h"
#include "view.h"

namespace sheetpad {

// Builds the contents of "<file>.formulas.json", or an empty string when there is nothing to store.
// With a view, its column filters and header-row flag are stored too.
std::string BuildFormulaJson(const Document& doc, const RowView* view = nullptr);

// Loads formulas (and, with a view, filters and the header-row flag) from the metadata file.
// Returns false when the JSON is malformed.
bool ApplyFormulaJson(Document& doc, const std::string& json, RowView* view = nullptr);

}  // namespace sheetpad
