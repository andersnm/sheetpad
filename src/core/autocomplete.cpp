#include "autocomplete.h"

#include <algorithm>
#include <cctype>

namespace sheetpad {

namespace {

bool IsIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

char Upper(char c) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

}  // namespace

const std::vector<FunctionInfo>& FunctionRegistry() {
    static const std::vector<FunctionInfo> functions = {
        {"ABS", "ABS(number)", "Absolute value of a number.", 1, 1},
        {"AND", "AND(logical1, [logical2], ...)", "TRUE if all arguments are TRUE.", 1, 255},
        {"AVERAGE", "AVERAGE(number1, [number2], ...)", "Average of the numbers.", 1, 255},
        {"CONCAT", "CONCAT(text1, [text2], ...)", "Joins text from several values.", 1, 255},
        {"COUNT", "COUNT(value1, [value2], ...)", "Counts the cells that contain numbers.", 1, 255},
        {"COUNTA", "COUNTA(value1, [value2], ...)", "Counts the cells that are not empty.", 1, 255},
        {"IF", "IF(test, value_if_true, [value_if_false])", "One value if the test is TRUE, another if not.", 2, 3},
        {"IFERROR", "IFERROR(value, value_if_error)", "The value, or a fallback if it is an error.", 2, 2},
        {"INT", "INT(number)", "Rounds down to the nearest integer.", 1, 1},
        {"LEN", "LEN(text)", "Number of characters in the text.", 1, 1},
        {"MAX", "MAX(number1, [number2], ...)", "Largest of the numbers.", 1, 255},
        {"MIN", "MIN(number1, [number2], ...)", "Smallest of the numbers.", 1, 255},
        {"MOD", "MOD(number, divisor)", "Remainder after division.", 2, 2},
        {"NOT", "NOT(logical)", "Reverses TRUE and FALSE.", 1, 1},
        {"OR", "OR(logical1, [logical2], ...)", "TRUE if any argument is TRUE.", 1, 255},
        {"ROUND", "ROUND(number, digits)", "Rounds to a number of digits.", 2, 2},
        {"ROUNDDOWN", "ROUNDDOWN(number, digits)", "Rounds toward zero to a number of digits.", 2, 2},
        {"ROUNDUP", "ROUNDUP(number, digits)", "Rounds away from zero to a number of digits.", 2, 2},
        {"SUM", "SUM(number1, [number2], ...)", "Adds the numbers.", 1, 255},
        {"SUMIF", "SUMIF(range, criteria, [sum_range])", "Adds the cells that meet a condition.", 2, 3},
    };
    return functions;
}

const FunctionInfo* FindFunction(std::string_view name) {
    for (const FunctionInfo& info : FunctionRegistry()) {
        if (name == info.name) {
            return &info;
        }
    }
    return nullptr;
}

TokenRange TokenAt(std::string_view text, std::size_t caret) {
    caret = std::min(caret, text.size());
    std::size_t start = caret;
    while (start > 0 && IsIdentChar(text[start - 1])) {
        --start;
    }
    std::size_t end = caret;
    while (end < text.size() && IsIdentChar(text[end])) {
        ++end;
    }
    return {start, end};
}

bool InStringLiteral(std::string_view text, std::size_t caret) {
    bool inString = false;
    for (std::size_t i = 0; i < std::min(caret, text.size()); ++i) {
        if (text[i] == '"') {
            inString = !inString;
        }
    }
    return inString;
}

std::vector<const FunctionInfo*> CompletionsFor(std::string_view prefix) {
    std::vector<const FunctionInfo*> result;
    for (const FunctionInfo& info : FunctionRegistry()) {
        const std::string_view name = info.name;
        if (name.size() >= prefix.size() &&
            std::equal(prefix.begin(), prefix.end(), name.begin(), [](char a, char b) { return Upper(a) == Upper(b); })) {
            result.push_back(&info);
        }
    }
    return result;
}

bool NeedsParentheses(std::string_view text, std::size_t pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    return pos >= text.size() || text[pos] != '(';
}

std::optional<CallContext> CallAt(std::string_view text, std::size_t caret) {
    struct Level {
        std::string name;  // empty for a grouping parenthesis
        std::size_t commas = 0;
    };
    std::vector<Level> stack;
    bool inString = false;
    caret = std::min(caret, text.size());
    for (std::size_t i = 0; i < caret; ++i) {
        const char c = text[i];
        if (c == '"') {
            inString = !inString;
            continue;
        }
        if (inString) {
            continue;
        }
        if (c == '(') {
            std::size_t start = i;
            while (start > 0 && IsIdentChar(text[start - 1])) {
                --start;
            }
            std::string name(text.substr(start, i - start));
            std::transform(name.begin(), name.end(), name.begin(), Upper);
            stack.push_back({std::move(name), 0});
        } else if (c == ')') {
            if (!stack.empty()) {
                stack.pop_back();
            }
        } else if (c == ',' && !stack.empty()) {
            ++stack.back().commas;
        }
    }
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
        if (!it->name.empty()) {
            const FunctionInfo* info = FindFunction(it->name);
            if (!info) {
                return std::nullopt;
            }
            return CallContext{info, it->commas};
        }
    }
    return std::nullopt;
}

std::pair<std::size_t, std::size_t> SignatureArgRange(const FunctionInfo& info, std::size_t argIndex) {
    const std::string_view signature = info.signature;
    const std::size_t open = signature.find('(');
    const std::size_t close = signature.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open + 1) {
        return {0, 0};
    }
    std::vector<std::pair<std::size_t, std::size_t>> args;
    std::size_t start = open + 1;
    while (start < close) {
        std::size_t end = signature.find(", ", start);
        if (end == std::string_view::npos || end > close) {
            end = close;
        }
        args.emplace_back(start, end - start);
        start = end + 2;
    }
    return args[std::min(argIndex, args.size() - 1)];
}

}  // namespace sheetpad
