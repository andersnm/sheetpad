#pragma once

#include <string>
#include <string_view>

#include "document.h"

namespace sheetpad {

std::wstring Utf8ToWide(std::string_view text);
std::string WideToUtf8(std::wstring_view text);

// Detects the file encoding (BOMs, valid UTF-8, otherwise the ANSI code page)
// and returns the content as UTF-8 without a BOM.
std::string DecodeFileBytes(std::string_view bytes, Encoding& encoding);
std::string EncodeFileText(std::string_view utf8, Encoding encoding);
// False when saving as ANSI would replace characters the system code page cannot represent.
bool IsAnsiRepresentable(std::string_view utf8);

}  // namespace sheetpad
