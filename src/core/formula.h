#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sheetpad {

enum class ErrorCode { Error, Div0, Ref, Value, Circular, Name, Num };

const char* ErrorText(ErrorCode code);

struct Value {
    enum class Type { Empty, Number, Text, Bool, Error };

    Type type = Type::Empty;
    double number = 0;
    // Text payload; for numbers read from a cell this is the original spelling.
    std::string text;
    ErrorCode error = ErrorCode::Error;

    static Value Num(double v);
    static Value Str(std::string s);
    static Value Boolean(bool b);
    static Value Err(ErrorCode code);
};

// Strict number parsing: optional sign, digits, decimal point, exponent. No separators.
std::optional<double> ParseNumber(std::string_view text);
Value ValueFromCell(const std::string& cell);
std::string FormatValue(const Value& value);

std::string ColumnLabel(std::size_t col);
std::string CellLabel(std::size_t row, std::size_t col);
bool ParseCellLabel(std::string_view label, std::size_t& row, std::size_t& col);

// Supplies cell values to the evaluator.
class CellSource {
public:
    virtual ~CellSource() = default;
    virtual Value GetCell(std::size_t row, std::size_t col) = 0;
    virtual std::size_t RowCount() const = 0;
    virtual std::size_t ColumnCount() const = 0;
};

struct Expr;
using ExprPtr = std::shared_ptr<const Expr>;

// Parses "=expression". Returns null on a syntax error.
ExprPtr ParseFormula(std::string_view text);

// A null expression evaluates to #ERROR.
Value Evaluate(const Expr* expr, CellSource& source);

std::optional<ErrorCode> ParseErrorText(std::string_view text);

struct FormulaInput {
    std::string label;
    // The value of a cell, or a summary of a range.
    std::string detail;
    bool isError = false;
};

struct FormulaInspection {
    bool parsed = false;
    std::vector<FormulaInput> inputs;
    // Explains an error result or a syntax error; empty otherwise.
    std::string diagnostic;
};

// Lists the cells and ranges a formula reads and explains why it failed. `result` is the cell's computed text.
FormulaInspection InspectFormula(const std::string& formula, CellSource& source, const std::string& result);

enum class Axis { Row, Column };

// Reference rewriting for structural edits. Formulas that cannot be tokenized are returned unchanged.
// References into deleted rows/columns become #REF!.
std::string RewriteForInsert(const std::string& formula, Axis axis, std::size_t at, std::size_t count);
std::string RewriteForDelete(const std::string& formula, Axis axis, std::size_t at, std::size_t count);
// Moves every relative (non-$) reference by the given offsets.
std::string RewriteForShift(const std::string& formula, long long rowDelta, long long colDelta);

}  // namespace sheetpad
