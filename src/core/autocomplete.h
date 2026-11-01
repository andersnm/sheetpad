#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sheetpad {

// The single description of every formula function. The parser takes argument limits from it, and the editor
// uses it for completion and signature help.
struct FunctionInfo {
    const char* name;
    // For example "ROUND(number, digits)". Optional arguments are in brackets; a trailing "..." means repeatable.
    const char* signature;
    const char* description;
    std::size_t minArgs;
    std::size_t maxArgs;
};

// All functions, sorted by name.
const std::vector<FunctionInfo>& FunctionRegistry();
// `name` must be upper case.
const FunctionInfo* FindFunction(std::string_view name);

// The editor text is handled as a narrow string with one char per UTF-16 unit, so indices match the EDIT control.
struct TokenRange {
    std::size_t start = 0;
    std::size_t end = 0;
};

// The run of letters, digits and underscores that contains or touches `caret`; empty at the caret if none.
TokenRange TokenAt(std::string_view text, std::size_t caret);
// True if `caret` lies inside a string literal.
bool InStringLiteral(std::string_view text, std::size_t caret);
// Functions whose name starts with `prefix` (case-insensitive), alphabetically.
std::vector<const FunctionInfo*> CompletionsFor(std::string_view prefix);
// False when the next non-blank character after `pos` is '(' so accepting a completion must not add parentheses.
bool NeedsParentheses(std::string_view text, std::size_t pos);

struct CallContext {
    const FunctionInfo* info = nullptr;
    std::size_t argIndex = 0;
};
// The innermost call around `caret` that names a known function, or nothing. Commas and parentheses in string
// literals are ignored.
std::optional<CallContext> CallAt(std::string_view text, std::size_t caret);
// Start and length within info.signature of the argument to highlight. Indices past the end select the last
// argument, which is the "..." entry of a variadic function. Length 0 means nothing to highlight.
std::pair<std::size_t, std::size_t> SignatureArgRange(const FunctionInfo& info, std::size_t argIndex);

}  // namespace sheetpad
