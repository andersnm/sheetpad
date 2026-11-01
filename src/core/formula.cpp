#include "formula.h"

#include "autocomplete.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <set>
#include <utility>
#include <vector>

namespace sheetpad {

namespace {

constexpr int kMaxNesting = 200;
constexpr std::size_t kMaxRow = 9999999;

struct CellRef {
    std::size_t row = 0;
    std::size_t col = 0;
    bool absRow = false;
    bool absCol = false;
};

bool IsAlpha(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool IsDigit(char c) {
    return c >= '0' && c <= '9';
}

bool IsIdentChar(char c) {
    return IsAlpha(c) || IsDigit(c) || c == '_';
}

std::string ToUpper(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

std::string RefText(const CellRef& ref) {
    return std::string(ref.absCol ? "$" : "") + ColumnLabel(ref.col) + (ref.absRow ? "$" : "") +
           std::to_string(ref.row + 1);
}

// ---------------------------------------------------------------- lexer

enum class Span { None, Cols, Rows };

enum class Tok { Number, String, Cell, Span, Ident, RefError, Op, End };

struct Token {
    Tok type = Tok::End;
    std::string text;
    std::size_t begin = 0;
    std::size_t end = 0;
    double number = 0;
    CellRef ref;
    CellRef ref2;
    Span span = Span::None;
};

// Matches one side of a whole-column (A, $A) or whole-row (1, $1) reference.
bool MatchSpanSide(std::string_view s, std::size_t& j, bool letters, bool& absolute, std::size_t& index) {
    absolute = j < s.size() && s[j] == '$';
    if (absolute) {
        ++j;
    }
    const std::size_t start = j;
    while (j < s.size() && (letters ? IsAlpha(s[j]) : IsDigit(s[j]))) {
        ++j;
    }
    const std::size_t count = j - start;
    if (count < 1 || count > (letters ? 3u : 7u)) {
        return false;
    }
    index = 0;
    for (std::size_t k = start; k < j; ++k) {
        index = letters ? index * 26 + static_cast<std::size_t>(std::toupper(static_cast<unsigned char>(s[k])) - 'A' + 1)
                        : index * 10 + static_cast<std::size_t>(s[k] - '0');
    }
    return index >= 1 && (letters || index <= kMaxRow);
}

// Matches A:C, $A:$A, 1:3 or $1:$3. Returns the end index or 0.
std::size_t MatchSpan(std::string_view s, std::size_t i, Token& token) {
    for (const bool letters : {true, false}) {
        std::size_t j = i;
        std::size_t first = 0;
        std::size_t second = 0;
        bool firstAbs = false;
        bool secondAbs = false;
        if (!MatchSpanSide(s, j, letters, firstAbs, first) || j >= s.size() || s[j] != ':') {
            continue;
        }
        ++j;
        if (!MatchSpanSide(s, j, letters, secondAbs, second)) {
            continue;
        }
        if (j < s.size() && (IsIdentChar(s[j]) || s[j] == '(' || s[j] == '$')) {
            continue;
        }
        token.span = letters ? Span::Cols : Span::Rows;
        (letters ? token.ref.col : token.ref.row) = first - 1;
        (letters ? token.ref2.col : token.ref2.row) = second - 1;
        (letters ? token.ref.absCol : token.ref.absRow) = firstAbs;
        (letters ? token.ref2.absCol : token.ref2.absRow) = secondAbs;
        return j;
    }
    return 0;
}

// Matches $?LETTERS$?DIGITS at `i`. Returns the end index or 0 when it is not a cell reference.
std::size_t MatchCell(std::string_view s, std::size_t i, CellRef& ref) {
    std::size_t j = i;
    ref.absCol = j < s.size() && s[j] == '$';
    if (ref.absCol) {
        ++j;
    }
    const std::size_t letters = j;
    while (j < s.size() && IsAlpha(s[j])) {
        ++j;
    }
    const std::size_t letterCount = j - letters;
    if (letterCount < 1 || letterCount > 3) {
        return 0;
    }
    ref.absRow = j < s.size() && s[j] == '$';
    if (ref.absRow) {
        ++j;
    }
    const std::size_t digits = j;
    while (j < s.size() && IsDigit(s[j])) {
        ++j;
    }
    const std::size_t digitCount = j - digits;
    if (digitCount < 1 || digitCount > 7) {
        return 0;
    }
    if (j < s.size() && (IsIdentChar(s[j]) || s[j] == '(')) {
        return 0;
    }

    std::size_t col = 0;
    for (std::size_t k = letters; k < letters + letterCount; ++k) {
        col = col * 26 + static_cast<std::size_t>(std::toupper(static_cast<unsigned char>(s[k])) - 'A' + 1);
    }
    const std::size_t rowNumber = static_cast<std::size_t>(std::strtoull(std::string(s.substr(digits, digitCount)).c_str(),
                                                                         nullptr, 10));
    if (rowNumber < 1 || rowNumber > kMaxRow) {
        return 0;
    }
    ref.col = col - 1;
    ref.row = rowNumber - 1;
    return j;
}

// Tokenizes the text after the leading '='. `bodyStart` receives the offset just past it.
bool Tokenize(std::string_view s, std::vector<Token>& out, std::size_t& bodyStart) {
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
    }
    if (i >= s.size() || s[i] != '=') {
        return false;
    }
    ++i;
    bodyStart = i;

    while (i < s.size()) {
        const char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        Token token;
        token.begin = i;

        CellRef ref;
        if (const std::size_t end = (c == '$' || IsAlpha(c)) ? MatchCell(s, i, ref) : 0; end != 0) {
            token.type = Tok::Cell;
            token.ref = ref;
            token.end = end;
        } else if (const std::size_t end = (c == '$' || IsAlpha(c) || IsDigit(c)) ? MatchSpan(s, i, token) : 0;
                   end != 0) {
            token.type = Tok::Span;
            token.end = end;
        } else if (IsAlpha(c) || c == '_') {
            std::size_t j = i;
            while (j < s.size() && IsIdentChar(s[j])) {
                ++j;
            }
            token.type = Tok::Ident;
            token.end = j;
        } else if (IsDigit(c) || (c == '.' && i + 1 < s.size() && IsDigit(s[i + 1]))) {
            std::size_t j = i;
            while (j < s.size() && IsDigit(s[j])) {
                ++j;
            }
            if (j < s.size() && s[j] == '.') {
                ++j;
                while (j < s.size() && IsDigit(s[j])) {
                    ++j;
                }
            }
            if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
                std::size_t k = j + 1;
                if (k < s.size() && (s[k] == '+' || s[k] == '-')) {
                    ++k;
                }
                if (k < s.size() && IsDigit(s[k])) {
                    while (k < s.size() && IsDigit(s[k])) {
                        ++k;
                    }
                    j = k;
                }
            }
            token.type = Tok::Number;
            token.number = std::strtod(std::string(s.substr(i, j - i)).c_str(), nullptr);
            token.end = j;
        } else if (c == '"') {
            std::size_t j = i + 1;
            std::string value;
            bool closed = false;
            while (j < s.size()) {
                if (s[j] == '"') {
                    if (j + 1 < s.size() && s[j + 1] == '"') {
                        value.push_back('"');
                        j += 2;
                        continue;
                    }
                    closed = true;
                    ++j;
                    break;
                }
                value.push_back(s[j++]);
            }
            if (!closed) {
                return false;
            }
            token.type = Tok::String;
            token.text = std::move(value);
            token.end = j;
        } else if (c == '#') {
            const std::string upper = ToUpper(std::string(s.substr(i, 5)));
            if (upper == "#REF!") {
                token.end = i + 5;
            } else if (upper.compare(0, 4, "#REF") == 0) {
                token.end = i + 4;
            } else {
                return false;
            }
            token.type = Tok::RefError;
        } else {
            static const char* const twoChar[] = {"<=", ">=", "<>"};
            std::size_t length = 0;
            for (const char* op : twoChar) {
                if (s.compare(i, 2, op) == 0) {
                    length = 2;
                    break;
                }
            }
            if (length == 0) {
                if (std::string_view("+-*/^&%(),:<>=").find(c) == std::string_view::npos) {
                    return false;
                }
                length = 1;
            }
            token.type = Tok::Op;
            token.text = std::string(s.substr(i, length));
            token.end = i + length;
        }
        if (token.type == Tok::Ident) {
            token.text = std::string(s.substr(token.begin, token.end - token.begin));
        }
        i = token.end;
        out.push_back(std::move(token));
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------- values

const char* ErrorText(ErrorCode code) {
    switch (code) {
    case ErrorCode::Div0: return "#DIV/0!";
    case ErrorCode::Ref: return "#REF!";
    case ErrorCode::Value: return "#VALUE!";
    case ErrorCode::Name: return "#NAME?";
    case ErrorCode::Num: return "#NUM!";
    case ErrorCode::Circular: return "#CIRCULAR";
    default: return "#ERROR";
    }
}

Value Value::Num(double v) {
    Value value;
    value.type = Type::Number;
    value.number = v;
    return value;
}

Value Value::Str(std::string s) {
    Value value;
    value.type = Type::Text;
    value.text = std::move(s);
    return value;
}

Value Value::Boolean(bool b) {
    Value value;
    value.type = Type::Bool;
    value.number = b ? 1 : 0;
    return value;
}

Value Value::Err(ErrorCode code) {
    Value value;
    value.type = Type::Error;
    value.error = code;
    return value;
}

std::optional<double> ParseNumber(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }
    bool digit = false;
    for (const char c : text) {
        if (IsDigit(c)) {
            digit = true;
        } else if (c != '.' && c != '+' && c != '-' && c != 'e' && c != 'E') {
            return std::nullopt;
        }
    }
    if (!digit) {
        return std::nullopt;
    }
    const std::string copy(text);
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

Value ValueFromCell(const std::string& cell) {
    if (cell.empty()) {
        return {};
    }
    if (const auto number = ParseNumber(cell)) {
        Value value = Value::Num(*number);
        value.text = cell;
        return value;
    }
    return Value::Str(cell);
}

std::string FormatValue(const Value& value) {
    switch (value.type) {
    case Value::Type::Empty: return {};
    case Value::Type::Text: return value.text;
    case Value::Type::Bool: return value.number != 0 ? "TRUE" : "FALSE";
    case Value::Type::Error: return ErrorText(value.error);
    case Value::Type::Number: {
        if (!value.text.empty()) {
            return value.text;
        }
        if (value.number == 0) {
            return "0";
        }
        char buffer[64];
        if (std::fabs(value.number) < 1e15 && value.number == std::floor(value.number)) {
            std::snprintf(buffer, sizeof(buffer), "%.0f", value.number);
        } else {
            std::snprintf(buffer, sizeof(buffer), "%.15g", value.number);
        }
        return buffer;
    }
    }
    return {};
}

std::string ColumnLabel(std::size_t col) {
    std::string name;
    for (std::size_t n = col + 1; n > 0; n = (n - 1) / 26) {
        name.insert(name.begin(), static_cast<char>('A' + (n - 1) % 26));
    }
    return name;
}

std::string CellLabel(std::size_t row, std::size_t col) {
    return ColumnLabel(col) + std::to_string(row + 1);
}

bool ParseCellLabel(std::string_view label, std::size_t& row, std::size_t& col) {
    CellRef ref;
    if (label.empty() || label.front() == '$' || MatchCell(label, 0, ref) != label.size()) {
        return false;
    }
    if (label.find('$') != std::string_view::npos) {
        return false;
    }
    row = ref.row;
    col = ref.col;
    return true;
}

// ---------------------------------------------------------------- AST and parser

struct Expr {
    enum class Kind { Number, Text, Bool, Cell, Range, RefError, NameError, Unary, Binary, Call };
    enum class Op { Add, Sub, Mul, Div, Pow, Cat, Eq, Ne, Lt, Le, Gt, Ge, Neg, Pos, Pct };
    enum class Func {
        Unknown, Sum, Avg, Min, Max, Count, CountA, Abs, Round, RoundUp, RoundDown, Int, Mod, If, IfError, And, Or,
        Not, SumIf, Concat, Len
    };

    Kind kind = Kind::Number;
    double number = 0;
    std::string text;
    CellRef a;
    CellRef b;
    Span span = Span::None;
    Op op = Op::Add;
    Func func = Func::Sum;
    std::vector<ExprPtr> args;
};

namespace {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    ExprPtr Parse() {
        ExprPtr expr = ParseComparison(0);
        if (!expr || pos_ != tokens_.size()) {
            return nullptr;
        }
        return expr;
    }

private:
    const Token* Peek() const { return pos_ < tokens_.size() ? &tokens_[pos_] : nullptr; }

    bool PeekOp(const char* op) const {
        const Token* t = Peek();
        return t && t->type == Tok::Op && t->text == op;
    }

    static std::shared_ptr<Expr> Make(Expr::Kind kind) {
        auto expr = std::make_shared<Expr>();
        expr->kind = kind;
        return expr;
    }

    ExprPtr ParseComparison(int depth) {
        if (depth > kMaxNesting) {
            return nullptr;
        }
        ExprPtr left = ParseConcat(depth);
        while (left) {
            const Token* t = Peek();
            if (!t || t->type != Tok::Op) {
                break;
            }
            Expr::Op op;
            if (t->text == "=") {
                op = Expr::Op::Eq;
            } else if (t->text == "<>") {
                op = Expr::Op::Ne;
            } else if (t->text == "<") {
                op = Expr::Op::Lt;
            } else if (t->text == "<=") {
                op = Expr::Op::Le;
            } else if (t->text == ">") {
                op = Expr::Op::Gt;
            } else if (t->text == ">=") {
                op = Expr::Op::Ge;
            } else {
                break;
            }
            ++pos_;
            ExprPtr right = ParseConcat(depth);
            if (!right) {
                return nullptr;
            }
            auto node = Make(Expr::Kind::Binary);
            node->op = op;
            node->args = {left, right};
            left = node;
        }
        return left;
    }

    ExprPtr ParseConcat(int depth) {
        ExprPtr left = ParseAdditive(depth);
        while (left && PeekOp("&")) {
            ++pos_;
            ExprPtr right = ParseAdditive(depth);
            if (!right) {
                return nullptr;
            }
            auto node = Make(Expr::Kind::Binary);
            node->op = Expr::Op::Cat;
            node->args = {left, right};
            left = node;
        }
        return left;
    }

    ExprPtr ParseAdditive(int depth) {
        ExprPtr left = ParseTerm(depth);
        while (left && (PeekOp("+") || PeekOp("-"))) {
            const Expr::Op op = PeekOp("+") ? Expr::Op::Add : Expr::Op::Sub;
            ++pos_;
            ExprPtr right = ParseTerm(depth);
            if (!right) {
                return nullptr;
            }
            auto node = Make(Expr::Kind::Binary);
            node->op = op;
            node->args = {left, right};
            left = node;
        }
        return left;
    }

    ExprPtr ParseTerm(int depth) {
        ExprPtr left = ParsePower(depth);
        while (left && (PeekOp("*") || PeekOp("/"))) {
            const Expr::Op op = PeekOp("*") ? Expr::Op::Mul : Expr::Op::Div;
            ++pos_;
            ExprPtr right = ParsePower(depth);
            if (!right) {
                return nullptr;
            }
            auto node = Make(Expr::Kind::Binary);
            node->op = op;
            node->args = {left, right};
            left = node;
        }
        return left;
    }

    // Excel: ^ is left-associative and binds looser than unary minus (-2^2 = 4).
    ExprPtr ParsePower(int depth) {
        ExprPtr left = ParseUnary(depth);
        while (left && PeekOp("^")) {
            ++pos_;
            ExprPtr right = ParseUnary(depth);
            if (!right) {
                return nullptr;
            }
            auto node = Make(Expr::Kind::Binary);
            node->op = Expr::Op::Pow;
            node->args = {left, right};
            left = node;
        }
        return left;
    }

    ExprPtr ParseUnary(int depth) {
        if (depth > kMaxNesting) {
            return nullptr;
        }
        if (PeekOp("-") || PeekOp("+")) {
            const Expr::Op op = PeekOp("-") ? Expr::Op::Neg : Expr::Op::Pos;
            ++pos_;
            ExprPtr operand = ParseUnary(depth + 1);
            if (!operand) {
                return nullptr;
            }
            auto node = Make(Expr::Kind::Unary);
            node->op = op;
            node->args = {operand};
            return node;
        }
        ExprPtr operand = ParsePrimary(depth);
        while (operand && PeekOp("%")) {
            ++pos_;
            auto node = Make(Expr::Kind::Unary);
            node->op = Expr::Op::Pct;
            node->args = {operand};
            operand = node;
        }
        return operand;
    }

    ExprPtr ParsePrimary(int depth) {
        const Token* t = Peek();
        if (!t) {
            return nullptr;
        }
        switch (t->type) {
        case Tok::Number: {
            auto node = Make(Expr::Kind::Number);
            node->number = t->number;
            ++pos_;
            return node;
        }
        case Tok::String: {
            auto node = Make(Expr::Kind::Text);
            node->text = t->text;
            ++pos_;
            return node;
        }
        case Tok::RefError:
            ++pos_;
            return Make(Expr::Kind::RefError);
        case Tok::Cell: {
            const CellRef first = t->ref;
            ++pos_;
            if (PeekOp(":")) {
                ++pos_;
                const Token* second = Peek();
                if (!second || second->type != Tok::Cell) {
                    return nullptr;
                }
                auto node = Make(Expr::Kind::Range);
                node->a = first;
                node->b = second->ref;
                ++pos_;
                return node;
            }
            auto node = Make(Expr::Kind::Cell);
            node->a = first;
            return node;
        }
        case Tok::Span: {
            auto node = Make(Expr::Kind::Range);
            node->a = t->ref;
            node->b = t->ref2;
            node->span = t->span;
            ++pos_;
            return node;
        }
        case Tok::Ident:
            return ParseIdentifier(depth);
        case Tok::Op:
            if (t->text == "(") {
                ++pos_;
                ExprPtr inner = ParseComparison(depth + 1);
                if (!inner || !PeekOp(")")) {
                    return nullptr;
                }
                ++pos_;
                return inner;
            }
            return nullptr;
        default:
            return nullptr;
        }
    }

    ExprPtr ParseIdentifier(int depth) {
        const std::string name = ToUpper(tokens_[pos_].text);
        ++pos_;
        if (!PeekOp("(")) {
            if (name == "TRUE" || name == "FALSE") {
                auto node = Make(Expr::Kind::Bool);
                node->number = name == "TRUE" ? 1 : 0;
                return node;
            }
            auto unknown = Make(Expr::Kind::NameError);
            unknown->text = name;
            return unknown;
        }

        // Argument limits come from the shared registry; this table only maps names to evaluator ids.
        static const std::pair<const char*, Expr::Func> funcIds[] = {
            {"SUM", Expr::Func::Sum},       {"AVERAGE", Expr::Func::Avg},     {"MIN", Expr::Func::Min},
            {"MAX", Expr::Func::Max},       {"COUNT", Expr::Func::Count},     {"COUNTA", Expr::Func::CountA},
            {"ABS", Expr::Func::Abs},       {"ROUND", Expr::Func::Round},     {"ROUNDUP", Expr::Func::RoundUp},
            {"ROUNDDOWN", Expr::Func::RoundDown}, {"INT", Expr::Func::Int},   {"MOD", Expr::Func::Mod},
            {"IF", Expr::Func::If},         {"IFERROR", Expr::Func::IfError}, {"AND", Expr::Func::And},
            {"OR", Expr::Func::Or},         {"NOT", Expr::Func::Not},         {"SUMIF", Expr::Func::SumIf},
            {"CONCAT", Expr::Func::Concat}, {"LEN", Expr::Func::Len},
        };
        const FunctionInfo* info = FindFunction(name);
        Expr::Func funcId = Expr::Func::Unknown;
        if (info) {
            for (const auto& entry : funcIds) {
                if (name == entry.first) {
                    funcId = entry.second;
                }
            }
        }

        ++pos_;  // '('
        auto node = Make(Expr::Kind::Call);
        node->func = funcId;
        node->text = name;
        if (PeekOp(")")) {
            return nullptr;
        }
        while (true) {
            ExprPtr arg = ParseComparison(depth + 1);
            if (!arg) {
                return nullptr;
            }
            node->args.push_back(std::move(arg));
            if (PeekOp(",")) {
                ++pos_;
                continue;
            }
            break;
        }
        if (!PeekOp(")")) {
            return nullptr;
        }
        ++pos_;
        if (info && (node->args.size() < info->minArgs || node->args.size() > info->maxArgs)) {
            return nullptr;
        }
        return node;
    }

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
};

// ---------------------------------------------------------------- evaluation

bool ToNumber(const Value& value, double& out, Value& error) {
    switch (value.type) {
    case Value::Type::Empty:
        out = 0;
        return true;
    case Value::Type::Number:
    case Value::Type::Bool:
        out = value.number;
        return true;
    case Value::Type::Text:
        if (const auto parsed = ParseNumber(value.text)) {
            out = *parsed;
            return true;
        }
        error = Value::Err(ErrorCode::Value);
        return false;
    case Value::Type::Error:
        error = value;
        return false;
    }
    return false;
}

std::string LowerAscii(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

int CompareValues(const Value& left, const Value& right) {
    // Excel order: numbers < text < logicals. Empty cells take the type of the other side.
    const auto rank = [](const Value& v) {
        switch (v.type) {
        case Value::Type::Text: return 1;
        case Value::Type::Bool: return 2;
        default: return 0;
        }
    };
    int leftRank = rank(left);
    int rightRank = rank(right);
    if (left.type == Value::Type::Empty) {
        leftRank = rightRank;
    } else if (right.type == Value::Type::Empty) {
        rightRank = leftRank;
    }
    if (leftRank != rightRank) {
        return leftRank < rightRank ? -1 : 1;
    }
    if (leftRank == 1) {
        const std::string a = LowerAscii(left.text);
        const std::string b = LowerAscii(right.text);
        return a < b ? -1 : (a == b ? 0 : 1);
    }
    return left.number < right.number ? -1 : (left.number > right.number ? 1 : 0);
}

Value Eval(const Expr& e, CellSource& source);

void ClampRange(const Expr& e, CellSource& source, std::size_t& r0, std::size_t& r1, std::size_t& c0,
                std::size_t& c1, bool& any) {
    const CellRef& b = e.kind == Expr::Kind::Cell ? e.a : e.b;
    r0 = std::min(e.a.row, b.row);
    r1 = std::max(e.a.row, b.row);
    c0 = std::min(e.a.col, b.col);
    c1 = std::max(e.a.col, b.col);
    if (e.span == Span::Cols) {
        r0 = 0;
        r1 = std::max<std::size_t>(source.RowCount(), 1) - 1;
    } else if (e.span == Span::Rows) {
        c0 = 0;
        c1 = std::max<std::size_t>(source.ColumnCount(), 1) - 1;
    }
    any = source.RowCount() > 0 && source.ColumnCount() > 0 && r0 < source.RowCount() && c0 < source.ColumnCount();
    if (any) {
        r1 = std::min(r1, source.RowCount() - 1);
        c1 = std::min(c1, source.ColumnCount() - 1);
    }
}

// Visits every value among a call's arguments, expanding ranges. `direct` is false for values read from
// cells and true for literals and computed arguments. The visitor returns false to stop.
template <class Visitor>
void ForEachValue(const Expr& call, CellSource& source, Visitor&& visit) {
    for (const ExprPtr& arg : call.args) {
        if (arg->kind == Expr::Kind::Range) {
            std::size_t r0, r1, c0, c1;
            bool any;
            ClampRange(*arg, source, r0, r1, c0, c1, any);
            if (!any) {
                continue;
            }
            for (std::size_t r = r0; r <= r1; ++r) {
                for (std::size_t c = c0; c <= c1; ++c) {
                    if (!visit(source.GetCell(r, c), false)) {
                        return;
                    }
                }
            }
        } else if (arg->kind == Expr::Kind::Cell) {
            if (!visit(source.GetCell(arg->a.row, arg->a.col), false)) {
                return;
            }
        } else if (!visit(Eval(*arg, source), true)) {
            return;
        }
    }
}

bool ToBool(const Value& value, bool& out, Value& error) {
    switch (value.type) {
    case Value::Type::Error:
        error = value;
        return false;
    case Value::Type::Empty:
        out = false;
        return true;
    case Value::Type::Number:
    case Value::Type::Bool:
        out = value.number != 0;
        return true;
    case Value::Type::Text: {
        const std::string upper = ToUpper(value.text);
        if (upper == "TRUE" || upper == "FALSE") {
            out = upper == "TRUE";
            return true;
        }
        error = Value::Err(ErrorCode::Value);
        return false;
    }
    }
    return false;
}

Value Aggregate(const Expr& e, CellSource& source) {
    std::vector<double> numbers;
    std::optional<Value> failure;
    ForEachValue(e, source, [&](const Value& value, bool direct) {
        if (value.type == Value::Type::Error) {
            failure = value;
            return false;
        }
        if (direct) {
            double number = 0;
            Value error;
            if (!ToNumber(value, number, error)) {
                failure = error;
                return false;
            }
            numbers.push_back(number);
        } else if (value.type == Value::Type::Number) {
            numbers.push_back(value.number);
        }
        return true;
    });
    if (failure) {
        return *failure;
    }
    if (numbers.empty()) {
        return e.func == Expr::Func::Avg ? Value::Err(ErrorCode::Div0) : Value::Num(0);
    }
    double result = 0;
    switch (e.func) {
    case Expr::Func::Min: result = *std::min_element(numbers.begin(), numbers.end()); break;
    case Expr::Func::Max: result = *std::max_element(numbers.begin(), numbers.end()); break;
    default:
        for (const double n : numbers) {
            result += n;
        }
        if (e.func == Expr::Func::Avg) {
            result /= static_cast<double>(numbers.size());
        }
        break;
    }
    return std::isfinite(result) ? Value::Num(result) : Value::Err(ErrorCode::Num);
}

Value CountValues(const Expr& e, CellSource& source) {
    double count = 0;
    ForEachValue(e, source, [&](const Value& value, bool direct) {
        if (e.func == Expr::Func::CountA) {
            count += value.type != Value::Type::Empty ? 1 : 0;
        } else if (value.type == Value::Type::Number ||
                   (direct && (value.type == Value::Type::Bool ||
                               (value.type == Value::Type::Text && ParseNumber(value.text))))) {
            count += 1;
        }
        return true;
    });
    return Value::Num(count);
}

Value Logical(const Expr& e, CellSource& source) {
    const bool isAnd = e.func == Expr::Func::And;
    bool result = isAnd;
    bool any = false;
    std::optional<Value> failure;
    ForEachValue(e, source, [&](const Value& value, bool direct) {
        if (value.type == Value::Type::Error) {
            failure = value;
            return false;
        }
        if (!direct && (value.type == Value::Type::Text || value.type == Value::Type::Empty)) {
            return true;
        }
        bool truth = false;
        Value error;
        if (!ToBool(value, truth, error)) {
            failure = error;
            return false;
        }
        any = true;
        result = isAnd ? (result && truth) : (result || truth);
        return true;
    });
    if (failure) {
        return *failure;
    }
    return any ? Value::Boolean(result) : Value::Err(ErrorCode::Value);
}

// Excel evaluates 1.005*100 as 100.5; snapping removes binary noise before rounding.
double Snap(double value) {
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.15g", value);
    return std::strtod(buffer, nullptr);
}

Value RoundNumber(Expr::Func func, double number, double digits) {
    digits = std::clamp(std::trunc(digits), -15.0, 15.0);
    const double factor = std::pow(10.0, std::fabs(digits));
    double scaled = std::fabs(number) * (digits >= 0 ? factor : 1 / factor);
    scaled = Snap(scaled);
    switch (func) {
    case Expr::Func::RoundUp: scaled = std::ceil(scaled); break;
    case Expr::Func::RoundDown: scaled = std::floor(scaled); break;
    default: scaled = std::floor(scaled + 0.5); break;
    }
    const double result = digits >= 0 ? scaled / factor : scaled * factor;
    return Value::Num(std::copysign(result, number));
}

bool WildcardMatch(const std::string& pattern, const std::string& text) {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string::npos;
    std::size_t starText = 0;
    while (t < text.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            starText = t;
            continue;
        }
        if (p < pattern.size()) {
            char expected = pattern[p];
            std::size_t step = 1;
            bool escaped = false;
            if (expected == '~' && p + 1 < pattern.size()) {
                expected = pattern[p + 1];
                step = 2;
                escaped = true;
            }
            if ((expected == '?' && !escaped) || expected == text[t]) {
                p += step;
                ++t;
                continue;
            }
        }
        if (star != std::string::npos) {
            p = star + 1;
            t = ++starText;
            continue;
        }
        return false;
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

struct Criteria {
    Expr::Op op = Expr::Op::Eq;
    bool numeric = false;
    double number = 0;
    std::string text;
};

Criteria ParseCriteria(const Value& value) {
    Criteria criteria;
    if (value.type == Value::Type::Number || value.type == Value::Type::Bool) {
        criteria.numeric = true;
        criteria.number = value.number;
        return criteria;
    }
    if (value.type == Value::Type::Empty) {
        criteria.numeric = true;
        return criteria;
    }
    std::string text = value.text;
    static const std::pair<const char*, Expr::Op> prefixes[] = {
        {">=", Expr::Op::Ge}, {"<=", Expr::Op::Le}, {"<>", Expr::Op::Ne},
        {">", Expr::Op::Gt},  {"<", Expr::Op::Lt},  {"=", Expr::Op::Eq},
    };
    for (const auto& [prefix, op] : prefixes) {
        if (text.rfind(prefix, 0) == 0) {
            criteria.op = op;
            text.erase(0, std::char_traits<char>::length(prefix));
            break;
        }
    }
    if (const auto number = ParseNumber(text)) {
        criteria.numeric = true;
        criteria.number = *number;
    }
    criteria.text = LowerAscii(text);
    return criteria;
}

bool CriteriaMatch(const Criteria& criteria, const Value& cell) {
    const auto test = [&](int comparison) {
        switch (criteria.op) {
        case Expr::Op::Eq: return comparison == 0;
        case Expr::Op::Ne: return comparison != 0;
        case Expr::Op::Lt: return comparison < 0;
        case Expr::Op::Le: return comparison <= 0;
        case Expr::Op::Gt: return comparison > 0;
        default: return comparison >= 0;
        }
    };
    if (criteria.numeric) {
        if (cell.type == Value::Type::Number) {
            return test(cell.number < criteria.number ? -1 : (cell.number > criteria.number ? 1 : 0));
        }
        return criteria.op == Expr::Op::Ne;
    }
    if (cell.type == Value::Type::Text || cell.type == Value::Type::Empty) {
        const std::string text = LowerAscii(cell.text);
        if (criteria.op == Expr::Op::Eq || criteria.op == Expr::Op::Ne) {
            return WildcardMatch(criteria.text, text) == (criteria.op == Expr::Op::Eq);
        }
        return test(text < criteria.text ? -1 : (text > criteria.text ? 1 : 0));
    }
    return criteria.op == Expr::Op::Ne;
}

Value SumIf(const Expr& e, CellSource& source) {
    const auto isRef = [](const Expr& arg) { return arg.kind == Expr::Kind::Range || arg.kind == Expr::Kind::Cell; };
    if (!isRef(*e.args[0]) || (e.args.size() > 2 && !isRef(*e.args[2]))) {
        return Value::Err(ErrorCode::Value);
    }
    const Value criteriaValue = Eval(*e.args[1], source);
    if (criteriaValue.type == Value::Type::Error) {
        return criteriaValue;
    }
    const Criteria criteria = ParseCriteria(criteriaValue);

    std::size_t r0, r1, c0, c1;
    bool any;
    ClampRange(*e.args[0], source, r0, r1, c0, c1, any);
    if (!any) {
        return Value::Num(0);
    }
    std::size_t sumRow = r0;
    std::size_t sumCol = c0;
    if (e.args.size() > 2) {
        const Expr& sum = *e.args[2];
        const CellRef& b = sum.kind == Expr::Kind::Cell ? sum.a : sum.b;
        sumRow = std::min(sum.a.row, b.row);
        sumCol = std::min(sum.a.col, b.col);
    }

    double total = 0;
    for (std::size_t r = r0; r <= r1; ++r) {
        for (std::size_t c = c0; c <= c1; ++c) {
            if (!CriteriaMatch(criteria, source.GetCell(r, c))) {
                continue;
            }
            const std::size_t row = sumRow + (r - r0);
            const std::size_t col = sumCol + (c - c0);
            if (row >= source.RowCount() || col >= source.ColumnCount()) {
                continue;
            }
            const Value cell = source.GetCell(row, col);
            if (cell.type == Value::Type::Error) {
                return cell;
            }
            if (cell.type == Value::Type::Number) {
                total += cell.number;
            }
        }
    }
    return Value::Num(total);
}

Value EvalCall(const Expr& e, CellSource& source) {
    switch (e.func) {
    case Expr::Func::Unknown: return Value::Err(ErrorCode::Name);
    case Expr::Func::Sum:
    case Expr::Func::Avg:
    case Expr::Func::Min:
    case Expr::Func::Max: return Aggregate(e, source);
    case Expr::Func::Count:
    case Expr::Func::CountA: return CountValues(e, source);
    case Expr::Func::And:
    case Expr::Func::Or: return Logical(e, source);
    case Expr::Func::SumIf: return SumIf(e, source);
    case Expr::Func::Concat: {
        std::string result;
        std::optional<Value> failure;
        ForEachValue(e, source, [&](const Value& value, bool) {
            if (value.type == Value::Type::Error) {
                failure = value;
                return false;
            }
            result += FormatValue(value);
            return true;
        });
        return failure ? *failure : Value::Str(std::move(result));
    }
    case Expr::Func::Len: {
        const Value value = Eval(*e.args[0], source);
        if (value.type == Value::Type::Error) {
            return value;
        }
        double length = 0;
        for (const unsigned char c : FormatValue(value)) {
            if ((c & 0xC0) != 0x80) {
                length += c >= 0xF0 ? 2 : 1;
            }
        }
        return Value::Num(length);
    }
    case Expr::Func::Not: {
        bool truth = false;
        Value error;
        if (!ToBool(Eval(*e.args[0], source), truth, error)) {
            return error;
        }
        return Value::Boolean(!truth);
    }
    case Expr::Func::IfError: {
        Value value = Eval(*e.args[0], source);
        return value.type == Value::Type::Error ? Eval(*e.args[1], source) : value;
    }
    case Expr::Func::Abs:
    case Expr::Func::Int: {
        double number = 0;
        Value error;
        if (!ToNumber(Eval(*e.args[0], source), number, error)) {
            return error;
        }
        return Value::Num(e.func == Expr::Func::Abs ? std::fabs(number) : std::floor(number));
    }
    case Expr::Func::Mod: {
        double number = 0;
        double divisor = 0;
        Value error;
        if (!ToNumber(Eval(*e.args[0], source), number, error) ||
            !ToNumber(Eval(*e.args[1], source), divisor, error)) {
            return error;
        }
        if (divisor == 0) {
            return Value::Err(ErrorCode::Div0);
        }
        const double result = number - divisor * std::floor(number / divisor);
        return std::isfinite(result) ? Value::Num(result) : Value::Err(ErrorCode::Num);
    }
    case Expr::Func::Round:
    case Expr::Func::RoundUp:
    case Expr::Func::RoundDown: {
        double number = 0;
        double digits = 0;
        Value error;
        if (!ToNumber(Eval(*e.args[0], source), number, error) ||
            !ToNumber(Eval(*e.args[1], source), digits, error)) {
            return error;
        }
        return RoundNumber(e.func, number, digits);
    }
    case Expr::Func::If: {
        bool truth = false;
        Value error;
        if (!ToBool(Eval(*e.args[0], source), truth, error)) {
            return error;
        }
        if (truth) {
            return Eval(*e.args[1], source);
        }
        return e.args.size() > 2 ? Eval(*e.args[2], source) : Value::Boolean(false);
    }
    }
    return Value::Err(ErrorCode::Error);
}

Value Eval(const Expr& e, CellSource& source) {
    switch (e.kind) {
    case Expr::Kind::Number: return Value::Num(e.number);
    case Expr::Kind::Text: return Value::Str(e.text);
    case Expr::Kind::Bool: return Value::Boolean(e.number != 0);
    case Expr::Kind::RefError: return Value::Err(ErrorCode::Ref);
    case Expr::Kind::NameError: return Value::Err(ErrorCode::Name);
    case Expr::Kind::Range: return Value::Err(ErrorCode::Value);
    case Expr::Kind::Cell: return source.GetCell(e.a.row, e.a.col);
    case Expr::Kind::Call: return EvalCall(e, source);
    case Expr::Kind::Unary: {
        double number = 0;
        Value error;
        if (!ToNumber(Eval(*e.args[0], source), number, error)) {
            return error;
        }
        if (e.op == Expr::Op::Pct) {
            return Value::Num(number / 100);
        }
        return Value::Num(e.op == Expr::Op::Neg ? -number : number);
    }
    case Expr::Kind::Binary: {
        const Value left = Eval(*e.args[0], source);
        if (left.type == Value::Type::Error) {
            return left;
        }
        const Value right = Eval(*e.args[1], source);
        if (right.type == Value::Type::Error) {
            return right;
        }
        switch (e.op) {
        case Expr::Op::Eq: return Value::Boolean(CompareValues(left, right) == 0);
        case Expr::Op::Ne: return Value::Boolean(CompareValues(left, right) != 0);
        case Expr::Op::Lt: return Value::Boolean(CompareValues(left, right) < 0);
        case Expr::Op::Le: return Value::Boolean(CompareValues(left, right) <= 0);
        case Expr::Op::Gt: return Value::Boolean(CompareValues(left, right) > 0);
        case Expr::Op::Ge: return Value::Boolean(CompareValues(left, right) >= 0);
        case Expr::Op::Cat: return Value::Str(FormatValue(left) + FormatValue(right));
        default: break;
        }
        double a = 0;
        double b = 0;
        Value error;
        if (!ToNumber(left, a, error) || !ToNumber(right, b, error)) {
            return error;
        }
        double result = 0;
        switch (e.op) {
        case Expr::Op::Add: result = a + b; break;
        case Expr::Op::Sub: result = a - b; break;
        case Expr::Op::Mul: result = a * b; break;
        case Expr::Op::Pow:
            if (a == 0 && b == 0) {
                return Value::Err(ErrorCode::Num);
            }
            if (a == 0 && b < 0) {
                return Value::Err(ErrorCode::Div0);
            }
            result = std::pow(a, b);
            break;
        default:
            if (b == 0) {
                return Value::Err(ErrorCode::Div0);
            }
            result = a / b;
            break;
        }
        return std::isfinite(result) ? Value::Num(result) : Value::Err(ErrorCode::Num);
    }
    }
    return Value::Err(ErrorCode::Error);
}

// ---------------------------------------------------------------- rewriting

using RewriteFn = std::function<bool(CellRef& a, CellRef& b, bool isRange, Span span)>;

std::string SpanText(const Token& token, const CellRef& a, const CellRef& b) {
    if (token.span == Span::Cols) {
        return std::string(a.absCol ? "$" : "") + ColumnLabel(a.col) + ":" + (b.absCol ? "$" : "") + ColumnLabel(b.col);
    }
    return std::string(a.absRow ? "$" : "") + std::to_string(a.row + 1) + ":" + (b.absRow ? "$" : "") +
           std::to_string(b.row + 1);
}

std::string RewriteRefs(const std::string& formula, const RewriteFn& fn) {
    std::vector<Token> tokens;
    std::size_t bodyStart = 0;
    if (!Tokenize(formula, tokens, bodyStart)) {
        return formula;
    }

    std::string out;
    std::size_t copied = 0;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == Tok::Span) {
            CellRef a = tokens[i].ref;
            CellRef b = tokens[i].ref2;
            const bool keep = fn(a, b, true, tokens[i].span);
            out.append(formula, copied, tokens[i].begin - copied);
            out += keep ? SpanText(tokens[i], a, b) : "#REF!";
            copied = tokens[i].end;
            continue;
        }
        if (tokens[i].type != Tok::Cell) {
            continue;
        }
        CellRef a = tokens[i].ref;
        CellRef b = a;
        std::size_t last = i;
        const bool isRange = i + 2 < tokens.size() && tokens[i + 1].type == Tok::Op && tokens[i + 1].text == ":" &&
                             tokens[i + 2].type == Tok::Cell;
        if (isRange) {
            b = tokens[i + 2].ref;
            last = i + 2;
        }
        const bool keep = fn(a, b, isRange, Span::None);
        out.append(formula, copied, tokens[i].begin - copied);
        if (!keep) {
            out += "#REF!";
        } else if (isRange) {
            out += RefText(a) + ":" + RefText(b);
        } else {
            out += RefText(a);
        }
        copied = tokens[last].end;
        i = last;
    }
    out.append(formula, copied, std::string::npos);
    return out;
}

std::size_t& Coord(CellRef& ref, Axis axis) {
    return axis == Axis::Row ? ref.row : ref.col;
}

bool& Absolute(CellRef& ref, Axis axis) {
    return axis == Axis::Row ? ref.absRow : ref.absCol;
}

}  // namespace

ExprPtr ParseFormula(std::string_view text) {
    std::vector<Token> tokens;
    std::size_t bodyStart = 0;
    if (!Tokenize(text, tokens, bodyStart) || tokens.empty()) {
        return nullptr;
    }
    return Parser(std::move(tokens)).Parse();
}

Value Evaluate(const Expr* expr, CellSource& source) {
    if (!expr) {
        return Value::Err(ErrorCode::Error);
    }
    return Eval(*expr, source);
}

// ---------------------------------------------------------------- inspection

std::optional<ErrorCode> ParseErrorText(std::string_view text) {
    for (const ErrorCode code : {ErrorCode::Error, ErrorCode::Div0, ErrorCode::Ref, ErrorCode::Value,
                                 ErrorCode::Circular, ErrorCode::Name, ErrorCode::Num}) {
        if (text == ErrorText(code)) {
            return code;
        }
    }
    return std::nullopt;
}

namespace {

constexpr unsigned long long kMaxInspectedCells = 5000000;

std::string Truncate(const std::string& text, std::size_t limit) {
    return text.size() <= limit ? text : text.substr(0, limit) + "...";
}

std::string DescribeValue(const Value& value, bool& isError) {
    isError = false;
    switch (value.type) {
    case Value::Type::Empty: return "(empty)";
    case Value::Type::Text:
        if (ParseErrorText(value.text)) {
            isError = true;
            return value.text;
        }
        return "\"" + Truncate(value.text, 60) + "\"";
    default: return FormatValue(value);
    }
}

class Inspector {
public:
    Inspector(CellSource& source, FormulaInspection& out) : source_(source), out_(out) {}

    void Visit(const Expr& expr) {
        switch (expr.kind) {
        case Expr::Kind::Cell: AddCell(expr.a); break;
        case Expr::Kind::Range: AddRange(expr); break;
        case Expr::Kind::RefError: hasRefError_ = true; break;
        case Expr::Kind::NameError: AddName(expr.text); break;
        case Expr::Kind::Call:
            if (expr.func == Expr::Func::Unknown) {
                AddName(expr.text + "()");
            }
            break;
        default: break;
        }
        for (const ExprPtr& arg : expr.args) {
            if (arg) {
                Visit(*arg);
            }
        }
    }

    const std::vector<std::string>& names() const { return names_; }
    const std::vector<std::string>& errorInputs() const { return errorInputs_; }
    bool hasRefError() const { return hasRefError_; }

private:
    Value Get(std::size_t row, std::size_t col) {
        if (row >= source_.RowCount() || col >= source_.ColumnCount()) {
            return {};
        }
        return source_.GetCell(row, col);
    }

    void AddCell(const CellRef& ref) {
        const std::string label = CellLabel(ref.row, ref.col);
        if (!seen_.insert(label).second) {
            return;
        }
        bool isError = false;
        std::string detail = DescribeValue(Get(ref.row, ref.col), isError);
        if (isError) {
            errorInputs_.push_back(label + " (" + detail + ")");
        }
        out_.inputs.push_back({label, std::move(detail), isError});
    }

    void AddRange(const Expr& expr) {
        std::size_t r0 = std::min(expr.a.row, expr.b.row);
        std::size_t r1 = std::max(expr.a.row, expr.b.row);
        std::size_t c0 = std::min(expr.a.col, expr.b.col);
        std::size_t c1 = std::max(expr.a.col, expr.b.col);
        std::string label;
        if (expr.span == Span::Cols) {
            label = ColumnLabel(c0) + ":" + ColumnLabel(c1);
            r0 = 0;
            r1 = source_.RowCount();
        } else if (expr.span == Span::Rows) {
            label = std::to_string(r0 + 1) + ":" + std::to_string(r1 + 1);
            c0 = 0;
            c1 = source_.ColumnCount();
        } else {
            label = CellLabel(r0, c0) + ":" + CellLabel(r1, c1);
        }
        if (!seen_.insert(label).second) {
            return;
        }
        // Clamp to the data: whole-column and whole-row ranges cover only what exists.
        r1 = std::min(r1, source_.RowCount() == 0 ? 0 : source_.RowCount() - 1);
        c1 = std::min(c1, source_.ColumnCount() == 0 ? 0 : source_.ColumnCount() - 1);
        const unsigned long long cells = r0 > r1 || c0 > c1
                                             ? 0ULL
                                             : static_cast<unsigned long long>(r1 - r0 + 1) * (c1 - c0 + 1);
        if (cells > kMaxInspectedCells) {
            out_.inputs.push_back({label, std::to_string(cells) + " cells", false});
            return;
        }
        std::size_t numbers = 0;
        std::size_t texts = 0;
        std::size_t errors = 0;
        std::size_t empties = 0;
        for (std::size_t r = r0; cells > 0 && r <= r1; ++r) {
            for (std::size_t c = c0; c <= c1; ++c) {
                const Value value = Get(r, c);
                if (value.type == Value::Type::Empty) {
                    ++empties;
                } else if (value.type == Value::Type::Number) {
                    ++numbers;
                } else if (ParseErrorText(value.text)) {
                    ++errors;
                } else {
                    ++texts;
                }
            }
        }
        std::string detail = std::to_string(cells) + (cells == 1 ? " cell" : " cells");
        const auto add = [&](std::size_t count, const char* what) {
            if (count > 0) {
                detail += (detail.find(':') == std::string::npos ? ": " : ", ") + std::to_string(count) + " " + what;
            }
        };
        add(numbers, numbers == 1 ? "number" : "numbers");
        add(texts, "text");
        add(empties, "empty");
        add(errors, errors == 1 ? "error" : "errors");
        if (errors > 0) {
            errorInputs_.push_back(label + " (" + std::to_string(errors) + (errors == 1 ? " error)" : " errors)"));
        }
        out_.inputs.push_back({label, std::move(detail), errors > 0});
    }

    void AddName(const std::string& name) {
        if (std::find(names_.begin(), names_.end(), name) == names_.end()) {
            names_.push_back(name);
        }
    }

    CellSource& source_;
    FormulaInspection& out_;
    std::set<std::string> seen_;
    std::vector<std::string> names_;
    std::vector<std::string> errorInputs_;
    bool hasRefError_ = false;
};

std::string DescribeSyntaxError(const std::string& formula) {
    int depth = 0;
    int closing = 0;
    bool inString = false;
    for (const char c : formula) {
        if (c == '"') {
            inString = !inString;
        } else if (!inString) {
            if (c == '(') {
                ++depth;
            } else if (c == ')') {
                if (depth > 0) {
                    --depth;
                } else {
                    ++closing;
                }
            }
        }
    }
    if (inString) {
        return "A text value is missing its closing quote (\").";
    }
    if (depth > 0) {
        return "Parentheses do not match: " + std::to_string(depth) + " opening ( without a closing ).";
    }
    if (closing > 0) {
        return "Parentheses do not match: " + std::to_string(closing) + " closing ) without an opening (.";
    }
    return "The formula could not be understood. Check operators, commas, function arguments and references.";
}

std::string Join(const std::vector<std::string>& items) {
    std::string text;
    for (const std::string& item : items) {
        text += (text.empty() ? "" : ", ") + item;
    }
    return text;
}

}  // namespace

FormulaInspection InspectFormula(const std::string& formula, CellSource& source, const std::string& result) {
    FormulaInspection inspection;
    const ExprPtr ast = ParseFormula(formula);
    inspection.parsed = ast != nullptr;
    Inspector inspector(source, inspection);
    if (ast) {
        inspector.Visit(*ast);
    }

    const std::optional<ErrorCode> error = ParseErrorText(result);
    if (!error && ast) {
        return inspection;
    }
    switch (error.value_or(ErrorCode::Error)) {
    case ErrorCode::Div0:
        inspection.diagnostic = "Division by zero: a divisor is 0 or an empty cell.";
        break;
    case ErrorCode::Ref:
        inspection.diagnostic = inspector.hasRefError()
                                    ? "The formula refers to a row or column that was deleted."
                                    : "An input cell has a reference error.";
        break;
    case ErrorCode::Value:
        inspection.diagnostic = "An operand has the wrong type, for example text where a number is needed, or a "
                                "range where a single value is expected.";
        break;
    case ErrorCode::Name:
        inspection.diagnostic = inspector.names().empty()
                                    ? "The formula uses a name that is not defined."
                                    : "Unknown name or function: " + Join(inspector.names()) + ".";
        break;
    case ErrorCode::Num:
        inspection.diagnostic = "A numeric result is invalid or out of range.";
        break;
    case ErrorCode::Circular:
        inspection.diagnostic = "This cell depends on itself, directly or through other cells.";
        break;
    case ErrorCode::Error:
        inspection.diagnostic = ast ? "The formula could not be evaluated." : DescribeSyntaxError(formula);
        break;
    }
    if (!inspector.errorInputs().empty()) {
        inspection.diagnostic += " Inputs with errors: " + Join(inspector.errorInputs()) + ".";
    }
    return inspection;
}

// A whole-column span ignores row edits and a whole-row span ignores column edits.
bool SpanIgnores(Span span, Axis axis) {
    return (span == Span::Cols && axis == Axis::Row) || (span == Span::Rows && axis == Axis::Column);
}

std::string RewriteForInsert(const std::string& formula, Axis axis, std::size_t at, std::size_t count) {
    return RewriteRefs(formula, [&](CellRef& a, CellRef& b, bool, Span span) {
        if (SpanIgnores(span, axis)) {
            return true;
        }
        for (CellRef* ref : {&a, &b}) {
            std::size_t& coord = Coord(*ref, axis);
            if (coord >= at) {
                coord += count;
            }
        }
        return true;
    });
}

std::string RewriteForDelete(const std::string& formula, Axis axis, std::size_t at, std::size_t count) {
    return RewriteRefs(formula, [&](CellRef& a, CellRef& b, bool isRange, Span span) {
        if (SpanIgnores(span, axis)) {
            return true;
        }
        if (!isRange) {
            std::size_t& coord = Coord(a, axis);
            if (coord >= at && coord < at + count) {
                return false;
            }
            if (coord >= at + count) {
                coord -= count;
            }
            return true;
        }
        if (Coord(a, axis) > Coord(b, axis)) {
            std::swap(Coord(a, axis), Coord(b, axis));
            std::swap(Absolute(a, axis), Absolute(b, axis));
        }
        const long long lo = static_cast<long long>(Coord(a, axis));
        const long long hi = static_cast<long long>(Coord(b, axis));
        const long long start = static_cast<long long>(at);
        const long long end = static_cast<long long>(at + count);
        const long long newLo = lo < start ? lo : (lo >= end ? lo - static_cast<long long>(count) : start);
        const long long newHi = hi < start ? hi : (hi >= end ? hi - static_cast<long long>(count) : start - 1);
        if (newLo > newHi || newHi < 0) {
            return false;
        }
        Coord(a, axis) = static_cast<std::size_t>(newLo);
        Coord(b, axis) = static_cast<std::size_t>(newHi);
        return true;
    });
}

std::string RewriteForShift(const std::string& formula, long long rowDelta, long long colDelta) {
    return RewriteRefs(formula, [&](CellRef& a, CellRef& b, bool, Span span) {
        for (CellRef* ref : {&a, &b}) {
            if (!ref->absRow && span != Span::Cols) {
                const long long row = static_cast<long long>(ref->row) + rowDelta;
                if (row < 0) {
                    return false;
                }
                ref->row = static_cast<std::size_t>(row);
            }
            if (!ref->absCol && span != Span::Rows) {
                const long long col = static_cast<long long>(ref->col) + colDelta;
                if (col < 0) {
                    return false;
                }
                ref->col = static_cast<std::size_t>(col);
            }
        }
        return true;
    });
}

}  // namespace sheetpad
