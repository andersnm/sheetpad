#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "csv.h"
#include "document.h"
#include "formula.h"
#include "formula_store.h"
#include "view.h"

using namespace sheetpad;

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        ++g_failures;
    }
}

#define CHECK(expr) Check((expr), #expr, __LINE__)

void RoundTrip(const std::string& text) {
    const ParsedCsv parsed = ParseCsv(text, DetectDelimiter(text));
    const Document doc = Document::FromParsed(parsed);
    CHECK(doc.Serialize() == text);
}

void TestParsing() {
    ParsedCsv p = ParseCsv("a,b,c\r\n1,\"x,y\",\"he said \"\"hi\"\"\"\r\n", ',');
    CHECK(p.rows.size() == 2);
    CHECK(p.columnCount == 3);
    CHECK(p.rows[1][1] == "x,y");
    CHECK(p.rows[1][2] == "he said \"hi\"");
    CHECK(p.crlf);
    CHECK(p.trailingNewline);

    p = ParseCsv("a,b\nc,d", ',');
    CHECK(!p.crlf);
    CHECK(!p.trailingNewline);
    CHECK(p.rows.size() == 2);

    p = ParseCsv("\"multi\nline\",x\n", ',');
    CHECK(p.rows.size() == 1);
    CHECK(p.rows[0][0] == "multi\nline");

    p = ParseCsv("\"open", ',');
    CHECK(p.unterminatedQuote);

    p = ParseCsv("", ',');
    CHECK(p.rows.empty());

    CHECK(DetectDelimiter("a;b;c\n1;2;3") == ';');
    CHECK(DetectDelimiter("a\tb\tc") == '\t');
    CHECK(DetectDelimiter("a|b|c") == '|');
    CHECK(DetectDelimiter("\"a,b\";c;d") == ';');
    CHECK(DetectDelimiter("plain") == ',');
}

void TestRoundTrip() {
    RoundTrip("a,b,c\r\n1,2,3\r\n");
    RoundTrip("a,b,c\n1,2,3\n");
    RoundTrip("a,b,c\n1,2,3");
    RoundTrip("a,b\n1,2,3\n4\n");
    RoundTrip("a;b\n\"x;y\";\"q\"\"r\"\n");
    RoundTrip("\"multi\nline\",x\n");
}

void TestPasteDelimiter() {
    CHECK(DetectPasteDelimiter("a\tb\r\nc\td\r\n") == '\t');
    CHECK(DetectPasteDelimiter("Hello, world") == '\t');
    CHECK(DetectPasteDelimiter("a,b\nc,d\n") == ',');
    CHECK(DetectPasteDelimiter("a,b\nc\n") == '\t');
    CHECK(DetectPasteDelimiter("a;b\nc;d") == ';');
}

void TestDocumentEditing() {
    Document doc = Document::FromParsed(ParseCsv("a,b\n1,2\n", ','));
    CHECK(!doc.IsDirty());
    CHECK(doc.ApplyEdits({{1, 0, "9"}}) == 1);
    CHECK(doc.IsDirty());
    CHECK(doc.Cell(1, 0) == "9");
    CHECK(doc.ApplyEdits({{1, 0, "9"}}) == 0);
    CHECK(doc.Undo());
    CHECK(doc.Cell(1, 0) == "1");
    CHECK(!doc.IsDirty());
    CHECK(doc.Redo());
    CHECK(doc.Cell(1, 0) == "9");
    CHECK(doc.IsDirty());
    doc.MarkSaved();
    CHECK(!doc.IsDirty());
    CHECK(doc.Undo());
    CHECK(doc.IsDirty());
    CHECK(doc.ApplyEdits({{0, 0, "z"}}) == 1);
    CHECK(!doc.CanRedo());
    doc.Undo();
    CHECK(doc.IsDirty());  // the saved state is no longer reachable
}

void TestGrowthAndUndo() {
    Document doc = Document::FromParsed(ParseCsv("a,b\n1,2\n", ','));
    const std::string original = doc.Serialize();
    CHECK(doc.ApplyEdits({{3, 4, "far"}, {0, 2, "c"}}) == 2);
    CHECK(doc.RowCount() == 4);
    CHECK(doc.ColumnCount() == 5);
    CHECK(doc.Cell(3, 4) == "far");
    CHECK(doc.Cell(10, 10).empty());
    CHECK(doc.Undo());
    CHECK(doc.RowCount() == 2);
    CHECK(doc.ColumnCount() == 2);
    CHECK(doc.Serialize() == original);
    CHECK(doc.Redo());
    CHECK(doc.RowCount() == 4);
    CHECK(doc.ColumnCount() == 5);
    CHECK(doc.Cell(0, 2) == "c");
    CHECK(doc.Undo());
    CHECK(doc.Serialize() == original);
}

void TestBlankDocument() {
    Document doc = Document::Blank(20, 8);
    CHECK(doc.RowCount() == 20);
    CHECK(doc.ColumnCount() == 8);
    CHECK(doc.Serialize().empty());
    doc.ApplyEdits({{0, 0, "Name"}, {0, 1, "Age"}, {1, 0, "Ann"}, {1, 1, "30"}});
    CHECK(doc.Serialize() == "Name,Age\r\nAnn,30\r\n");
    doc.ApplyEdits({{1, 0, ""}, {1, 1, ""}});
    CHECK(doc.Serialize() == "Name,Age\r\n");
}

void TestOutput() {
    const Table table = {{"a", "b\tc"}, {"x\ny", "q\"r"}};
    CHECK(BuildTsv(table) == "a\t\"b\tc\"\r\n\"x\ny\"\t\"q\"\"r\"");
    CHECK(BuildHtmlTable({{"<&>"}}).find("&lt;&amp;&gt;") != std::string::npos);
    CHECK(IsValidUtf8("h\xC3\xA9llo"));
    CHECK(!IsValidUtf8("h\xE9llo"));
    CHECK(!IsValidUtf8("\xC3"));
    CHECK(!IsValidUtf8("\xC0\x80"));
}

CellEdit F(std::size_t row, std::size_t col, const std::string& formula) {
    return CellEdit{row, col, formula, true};
}

Document Numbers() {
    return Document::FromParsed(ParseCsv("1,2\n3,4\n", ','));
}

std::string Eval(const std::string& formula) {
    Document doc = Numbers();
    doc.ApplyEdits({F(5, 5, formula)});
    return doc.Cell(5, 5);
}

void TestFormulaBasics() {
    Document doc = Numbers();
    doc.ApplyEdits({F(0, 2, "=A1*B1")});
    CHECK(doc.Cell(0, 2) == "2");
    CHECK(doc.Formula(0, 2) == "=A1*B1");
    CHECK(doc.IsDirty());

    doc.ApplyEdits({{0, 0, "5"}});
    CHECK(doc.Cell(0, 2) == "10");
    CHECK(doc.Undo());
    CHECK(doc.Cell(0, 2) == "2");
    CHECK(doc.Undo());
    CHECK(doc.Formula(0, 2).empty());
    CHECK(doc.Cell(0, 2).empty());
    CHECK(!doc.IsDirty());
    CHECK(doc.Redo());
    CHECK(doc.Redo());
    CHECK(doc.Cell(0, 2) == "10");

    // A plain value replaces the formula, and undo brings it back.
    doc.ApplyEdits({{0, 2, "x"}});
    CHECK(doc.Formula(0, 2).empty());
    CHECK(doc.Cell(0, 2) == "x");
    CHECK(doc.Undo());
    CHECK(doc.Formula(0, 2) == "=A1*B1");
    CHECK(doc.Cell(0, 2) == "10");

    // The CSV holds the calculated values only.
    CHECK(doc.Serialize() == "5,2,10\n3,4\n");
}

void TestFormulaLanguage() {
    CHECK(Eval("=1+2*3") == "7");
    CHECK(Eval("=(1+2)*3") == "9");
    CHECK(Eval("=10/4") == "2.5");
    CHECK(Eval("=-A1+B2") == "3");
    CHECK(Eval("=0.1+0.2") == "0.3");
    CHECK(Eval("=1/0") == "#DIV/0!");
    CHECK(Eval("=A1+") == "#ERROR");
    CHECK(Eval("=FOO(1)") == "#NAME?");
    CHECK(Eval("=FOO") == "#NAME?");
    CHECK(Eval("=\"abc\"+1") == "#VALUE!");
    CHECK(Eval("=\"Hello\"") == "Hello");
    CHECK(Eval("=\"a\"\"b\"") == "a\"b");
    CHECK(Eval("=A1 > B1") == "FALSE");
    CHECK(Eval("=A1 < B1") == "TRUE");
    CHECK(Eval("=A1 = 1") == "TRUE");
    CHECK(Eval("=A1 <> 1") == "FALSE");
    CHECK(Eval("=A1 == 1") == "#ERROR");
    CHECK(Eval("=A1 != 1") == "#ERROR");
    CHECK(Eval("=A1 <= 1") == "TRUE");
    CHECK(Eval("=\"a\" = \"A\"") == "TRUE");
    CHECK(Eval("=SUM(A1:B2)") == "10");
    CHECK(Eval("=SUM(A1:B2, 5, A1)") == "16");
    CHECK(Eval("=AVERAGE(A1:B2)") == "2.5");
    CHECK(Eval("=AVG(A1:B2)") == "#NAME?");
    CHECK(Eval("=MIN(A1:B2)") == "1");
    CHECK(Eval("=MAX(A1:B2)") == "4");
    CHECK(Eval("=COUNT(A1:C3)") == "4");
    CHECK(Eval("=AVERAGE(D1:D3)") == "#DIV/0!");
    CHECK(Eval("=SUM(D1:D3)") == "0");
    CHECK(Eval("=ABS(-3.5)") == "3.5");
    CHECK(Eval("=ROUND(3.14159, 2)") == "3.14");
    CHECK(Eval("=ROUND(2.5, 0)") == "3");
    CHECK(Eval("=ROUND(-2.5, 0)") == "-3");
    CHECK(Eval("=ROUND(2.5)") == "#ERROR");
    CHECK(Eval("=IF(A1 > 100, \"Large\", \"Small\")") == "Small");
    CHECK(Eval("=IF(B2 > 3, \"Large\", \"Small\")") == "Large");
    CHECK(Eval("=IF(1, 1/0, 2)") == "#DIV/0!");
    CHECK(Eval("=IF(0, 1/0, 2)") == "2");
    CHECK(Eval("=$A$1+A$2") == "4");
    CHECK(Eval("=a1+b1") == "3");
    CHECK(Eval("=A1:B2") == "#VALUE!");
    CHECK(Eval("=#REF!+1") == "#REF!");

    // Excel operators.
    CHECK(Eval("=2^3") == "8");
    CHECK(Eval("=2^3^2") == "64");
    CHECK(Eval("=-2^2") == "4");
    CHECK(Eval("=2*3^2") == "18");
    CHECK(Eval("=2^-1") == "0.5");
    CHECK(Eval("=0^0") == "#NUM!");
    CHECK(Eval("=(-8)^0.5") == "#NUM!");
    CHECK(Eval("=50%") == "0.5");
    CHECK(Eval("=200*10%") == "20");
    CHECK(Eval("=\"a\"&\"b\"&1") == "ab1");
    CHECK(Eval("=1+2&3") == "33");
    CHECK(Eval("=A1&\"-\"&B1") == "1-2");
    CHECK(Eval("=\"a\"&\"b\"=\"AB\"") == "TRUE");
    CHECK(Eval("=1<\"a\"") == "TRUE");
    CHECK(Eval("=TRUE>\"a\"") == "TRUE");

    // More functions.
    CHECK(Eval("=COUNTA(A1:D3)") == "4");
    CHECK(Eval("=AND(1,TRUE,A1=1)") == "TRUE");
    CHECK(Eval("=AND(1,0)") == "FALSE");
    CHECK(Eval("=OR(0,0,B1=2)") == "TRUE");
    CHECK(Eval("=OR(0,FALSE)") == "FALSE");
    CHECK(Eval("=AND(D1:D2)") == "#VALUE!");
    CHECK(Eval("=NOT(A1=1)") == "FALSE");
    CHECK(Eval("=IFERROR(1/0,\"n/a\")") == "n/a");
    CHECK(Eval("=IFERROR(5,1/0)") == "5");
    CHECK(Eval("=CONCAT(\"a\",A1:B1,TRUE)") == "a12TRUE");
    CHECK(Eval("=LEN(\"hello\")") == "5");
    CHECK(Eval("=LEN(12.5)") == "4");
    CHECK(Eval("=ROUNDUP(2.01,1)") == "2.1");
    CHECK(Eval("=ROUNDUP(-2.01,1)") == "-2.1");
    CHECK(Eval("=ROUNDDOWN(2.99,1)") == "2.9");
    CHECK(Eval("=ROUNDDOWN(-2.99,0)") == "-2");
    CHECK(Eval("=ROUND(1.005,2)") == "1.01");
    CHECK(Eval("=ROUNDUP(1.1*100,0)") == "110");
    CHECK(Eval("=ROUND(1234,-2)") == "1200");
    CHECK(Eval("=INT(-1.5)") == "-2");
    CHECK(Eval("=INT(2.9)") == "2");
    CHECK(Eval("=MOD(7,3)") == "1");
    CHECK(Eval("=MOD(-7,3)") == "2");
    CHECK(Eval("=MOD(7,0)") == "#DIV/0!");

    // Whole-column and whole-row ranges.
    CHECK(Eval("=SUM(A:A)") == "4");
    CHECK(Eval("=SUM(A:B)") == "10");
    CHECK(Eval("=SUM($B:$B)") == "6");
    CHECK(Eval("=SUM(1:1)") == "3");
    CHECK(Eval("=SUM(1:2)") == "10");
    CHECK(Eval("=COUNT(C:C)") == "0");
}

void TestSumIf() {
    Document doc = Document::FromParsed(ParseCsv("apple,5\nbanana,7\navocado,3\ncherry,10\n", ','));
    const auto sumif = [&](const std::string& formula) {
        doc.ApplyEdits({F(0, 3, formula)});
        return doc.Cell(0, 3);
    };
    CHECK(sumif("=SUMIF(A1:A4,\"apple\",B1:B4)") == "5");
    CHECK(sumif("=SUMIF(A1:A4,\"a*\",B1:B4)") == "8");
    CHECK(sumif("=SUMIF(A1:A4,\"?anana\",B1:B4)") == "7");
    CHECK(sumif("=SUMIF(A1:A4,\"<>apple\",B1:B4)") == "20");
    CHECK(sumif("=SUMIF(B1:B4,\">4\")") == "22");
    CHECK(sumif("=SUMIF(B1:B4,\"<=5\")") == "8");
    CHECK(sumif("=SUMIF(B1:B4,10)") == "10");
    CHECK(sumif("=SUMIF(B1:B4,\"=7\")") == "7");
    CHECK(sumif("=SUMIF(A:A,\"c*\",B:B)") == "10");
    CHECK(sumif("=SUMIF(5,1)") == "#VALUE!");
}

void TestNumberSpelling() {
    // Numbers keep their original spelling when referenced directly.
    Document doc = Document::FromParsed(ParseCsv("007,x\n", ','));
    doc.ApplyEdits({F(0, 2, "=A1"), F(0, 3, "=A1+0"), F(0, 4, "=B1+1")});
    CHECK(doc.Cell(0, 2) == "007");
    CHECK(doc.Cell(0, 3) == "7");
    CHECK(doc.Cell(0, 4) == "#VALUE!");
}

void TestFormulaDependencies() {
    Document doc;
    doc = Document::FromParsed(ParseCsv("1\n", ','));
    doc.ApplyEdits({F(0, 1, "=C1+1"), {0, 2, "5"}});
    CHECK(doc.Cell(0, 1) == "6");

    doc.ApplyEdits({F(1, 0, "=B2+1"), F(1, 1, "=A2+1")});
    CHECK(doc.Cell(1, 0) == "#CIRCULAR");
    CHECK(doc.Cell(1, 1) == "#CIRCULAR");
    doc.ApplyEdits({F(2, 0, "=A3")});
    CHECK(doc.Cell(2, 0) == "#CIRCULAR");
    doc.ApplyEdits({{1, 1, "1"}});
    CHECK(doc.Cell(1, 0) == "2");

    Document chain = Document::FromParsed(ParseCsv("1\n", ','));
    std::vector<CellEdit> edits;
    for (std::size_t r = 1; r < 3000; ++r) {
        edits.push_back(F(r, 0, "=A" + std::to_string(r) + "+1"));
    }
    chain.ApplyEdits(std::move(edits));
    CHECK(chain.Cell(2999, 0) == "3000");

    Document backwards = Document::FromParsed(ParseCsv("1\n", ','));
    edits.clear();
    for (std::size_t r = 2998; r >= 1; --r) {
        edits.push_back(F(r, 0, "=A" + std::to_string(r + 2) + "+1"));
    }
    edits.push_back({2999, 0, "0"});
    backwards.ApplyEdits(std::move(edits));
    CHECK(backwards.Cell(1, 0) == "2998");
}

void TestStructuralEdits() {
    Document doc = Document::FromParsed(ParseCsv("1\n2\n\n", ','));
    doc.ApplyEdits({F(2, 0, "=SUM(A1:A2)")});
    CHECK(doc.Cell(2, 0) == "3");

    CHECK(doc.InsertRows(1, 1));
    CHECK(doc.RowCount() == 4);
    CHECK(doc.Formula(3, 0) == "=SUM(A1:A3)");
    doc.ApplyEdits({{1, 0, "10"}});
    CHECK(doc.Cell(3, 0) == "13");
    CHECK(doc.Undo());
    CHECK(doc.Undo());
    CHECK(doc.RowCount() == 3);
    CHECK(doc.Formula(2, 0) == "=SUM(A1:A2)");
    CHECK(doc.Redo());
    CHECK(doc.RowCount() == 4);

    CHECK(doc.Undo());
    CHECK(doc.DeleteRows(1, 1));
    CHECK(doc.Formula(1, 0) == "=SUM(A1:A1)");
    CHECK(doc.Cell(1, 0) == "1");
    CHECK(doc.Undo());
    CHECK(doc.Formula(2, 0) == "=SUM(A1:A2)");

    Document refs = Document::FromParsed(ParseCsv("1,2\n3,4\n", ','));
    refs.ApplyEdits({F(0, 2, "=B1+A2")});
    CHECK(refs.DeleteRows(1, 1));
    CHECK(refs.Formula(0, 2) == "=B1+#REF!");
    CHECK(refs.Cell(0, 2) == "#REF!");
    CHECK(refs.Undo());
    CHECK(refs.Formula(0, 2) == "=B1+A2");
    CHECK(refs.Cell(0, 2) == "5");

    CHECK(refs.DeleteColumns(0, 1));
    CHECK(refs.ColumnCount() == 2);
    CHECK(refs.Formula(0, 1) == "=A1+#REF!");
    CHECK(refs.Cell(0, 0) == "2");
    CHECK(refs.Undo());
    CHECK(refs.ColumnCount() == 3);
    CHECK(refs.InsertColumns(1, 2));
    CHECK(refs.ColumnCount() == 5);
    CHECK(refs.Formula(0, 4) == "=D1+A2");
    CHECK(refs.Cell(0, 4) == "5");
    CHECK(!refs.DeleteRows(99, 1));
    CHECK(!refs.DeleteColumns(99, 1));

    Document dup = Document::FromParsed(ParseCsv("5,\n7,\n", ','));
    dup.ApplyEdits({F(0, 1, "=A1*2+$A$2")});
    CHECK(dup.DuplicateRow(0));
    CHECK(dup.RowCount() == 3);
    CHECK(dup.Cell(1, 0) == "5");
    CHECK(dup.Formula(1, 1) == "=A2*2+$A$3");
    CHECK(dup.Cell(0, 1) == "17");
    CHECK(dup.Undo());
    CHECK(dup.RowCount() == 2);

    // Deleting every row leaves one empty row.
    Document all = Numbers();
    CHECK(all.DeleteRows(0, 5));
    CHECK(all.RowCount() == 1);

    CHECK(RewriteForShift("=$A$1+A1", 1, 0) == "=$A$1+A2");
    CHECK(RewriteForShift("=A1", -1, 0) == "=#REF!");
    CHECK(RewriteForDelete("=SUM(A1:A5)", Axis::Row, 1, 2) == "=SUM(A1:A3)");
    CHECK(RewriteForDelete("=SUM(A2:A3)", Axis::Row, 1, 2) == "=SUM(#REF!)");
    CHECK(RewriteForInsert("=SUM(A1:A5)", Axis::Row, 2, 1) == "=SUM(A1:A6)");

    // Whole-column and whole-row references only react to edits on their own axis.
    CHECK(RewriteForInsert("=SUM(A:B)", Axis::Row, 0, 3) == "=SUM(A:B)");
    CHECK(RewriteForInsert("=SUM(A:B)", Axis::Column, 1, 1) == "=SUM(A:C)");
    CHECK(RewriteForDelete("=SUM(B:B)", Axis::Column, 1, 1) == "=SUM(#REF!)");
    CHECK(RewriteForDelete("=SUM(1:3)", Axis::Column, 0, 1) == "=SUM(1:3)");
    CHECK(RewriteForInsert("=SUM($2:3)", Axis::Row, 0, 1) == "=SUM($3:4)");
    CHECK(RewriteForShift("=SUM(A:B)+SUM(1:2)", 1, 1) == "=SUM(B:C)+SUM(2:3)");
    CHECK(RewriteForShift("=SUM($A:$A)", 1, 1) == "=SUM($A:$A)");
}

void TestFormulaStore() {
    Document doc = Numbers();
    doc.ApplyEdits({F(0, 2, "=A1+B1"), F(1, 2, "=IF(A2 > 1, \"x\\y\", \"\")")});
    const std::string json = BuildFormulaJson(doc);
    CHECK(json.find("\"C1\": \"=A1+B1\"") != std::string::npos);

    Document loaded = Document::FromParsed(ParseCsv("1,2,99\n3,4\n", ','));
    CHECK(ApplyFormulaJson(loaded, json));
    CHECK(loaded.Formula(0, 2) == "=A1+B1");
    CHECK(loaded.Cell(0, 2) == "3");
    CHECK(loaded.Formula(1, 2) == "=IF(A2 > 1, \"x\\y\", \"\")");
    CHECK(loaded.Cell(1, 2) == "x\\y");
    CHECK(!loaded.IsDirty());
    CHECK(!loaded.CanUndo());

    CHECK(BuildFormulaJson(Numbers()).empty());

    Document extra = Numbers();
    CHECK(ApplyFormulaJson(extra, "{\"version\": 1, \"columns\": {\"Revenue\": \"=Price * Quantity\"}, "
                                  "\"cells\": {\"A1\": \"=1+1\", \"bad\": \"=2\", \"B1\": \"nope\"}}"));
    CHECK(extra.Cell(0, 0) == "2");
    CHECK(extra.Formula(0, 1).empty());
    CHECK(extra.columnFormulas.at("Revenue") == "=Price * Quantity");
    CHECK(BuildFormulaJson(extra).find("\"Revenue\"") != std::string::npos);

    Document unicode = Numbers();
    CHECK(ApplyFormulaJson(unicode, "{\"cells\": {\"A1\": \"=\\\"\\u00e5\\ud83d\\ude00\\\"\"}}"));
    CHECK(unicode.Cell(0, 0) == "\xC3\xA5\xF0\x9F\x98\x80");

    Document bad = Numbers();
    CHECK(!ApplyFormulaJson(bad, "{\"cells\": "));
    CHECK(!ApplyFormulaJson(bad, "not json"));
    CHECK(!ApplyFormulaJson(bad, "{\"cells\": {\"A1\": 5}}"));
}

Document People() {
    return Document::FromParsed(
        ParseCsv("Name,Age,City\nbob,30,Oslo\nAnn,25,Bergen\ncarl,,Oslo\nDan,25,\n", ','));
}

void TestSortAndFilter() {
    CHECK(CompareForSort("2", "10") < 0);
    CHECK(CompareForSort("10", "abc") < 0);
    CHECK(CompareForSort("abc", "ABD") < 0);
    CHECK(CompareForSort("zzz", "") < 0);
    CHECK(CompareForSort("", "") == 0);

    Document doc = People();
    RowView view;
    view.headerRows = 1;
    CHECK(!view.Active());
    CHECK(view.RowCount(doc) == 5);
    CHECK(view.DocRow(3, doc) == 3);

    view.sorts = {{1, false}, {0, true}};
    view.Rebuild(doc);
    // Header stays first; blanks last; ties broken by the second level (descending name).
    CHECK((view.Rows() == std::vector<std::size_t>{0, 4, 2, 1, 3}));
    view.sorts = {{1, true}};
    view.Rebuild(doc);
    CHECK((view.Rows() == std::vector<std::size_t>{0, 1, 2, 4, 3}));

    view.sorts.clear();
    ColumnFilter oslo;
    oslo.allowed = {"Oslo"};
    view.filters[2] = oslo;
    view.Rebuild(doc);
    CHECK((view.Rows() == std::vector<std::size_t>{0, 1, 3}));
    CHECK(view.DocRow(2, doc) == 3);
    CHECK(view.DocRow(3, doc) == 5);
    CHECK(!view.DisplayRow(2, doc));
    CHECK(view.DisplayRow(3, doc).value() == 2);

    // Blanks are part of the list as "".
    view.filters[2].allowed = {"", "Bergen"};
    view.Rebuild(doc);
    CHECK((view.Rows() == std::vector<std::size_t>{0, 2, 4}));

    // The value list respects the other columns' filters and leaves out the header row.
    view.filters.clear();
    view.filters[1].allowed = {"25"};
    const auto cities = view.DistinctValues(doc, 2, true);
    CHECK(cities.size() == 2);
    CHECK(cities[0].value == "Bergen" && cities[0].count == 1);
    CHECK(cities[1].value.empty() && cities[1].count == 1);
    CHECK(view.DistinctValues(doc, 2, false).size() == 3);
    const auto ages = view.DistinctValues(doc, 1, true);
    CHECK(ages.size() == 3);
    CHECK(ages[0].value == "25" && ages[0].count == 2);
    CHECK(ages[1].value == "30");
    CHECK(ages[2].value.empty());

    CHECK(ConditionMatches(FilterOp::Greater, "26", "30"));
    CHECK(!ConditionMatches(FilterOp::Greater, "26", ""));
    CHECK(ConditionMatches(FilterOp::Equals, "oslo", "OSLO"));
    CHECK(ConditionMatches(FilterOp::NotEquals, "oslo", "Bergen"));
    CHECK(ConditionMatches(FilterOp::Contains, "ER", "Bergen"));
    CHECK(ConditionMatches(FilterOp::NotContains, "x", "Bergen"));
    CHECK(ConditionMatches(FilterOp::BeginsWith, "be", "Bergen"));
    CHECK(!ConditionMatches(FilterOp::BeginsWith, "bergen!", "Bergen"));
    CHECK(ConditionMatches(FilterOp::EndsWith, "GEN", "Bergen"));
    CHECK(ConditionMatches(FilterOp::LessEqual, "9", "10") == false);
    CHECK(ConditionMatches(FilterOp::Equals, "", ""));

    ColumnFilter older;
    older.isList = false;
    older.op = FilterOp::Greater;
    older.operand = "26";
    view.filters.clear();
    view.filters[1] = older;
    view.Rebuild(doc);
    CHECK((view.Rows() == std::vector<std::size_t>{0, 1}));

    // The map follows row inserts and deletes without a rebuild.
    view.filters.clear();
    view.sorts = {{0, false}};
    view.Rebuild(doc);
    CHECK((view.Rows() == std::vector<std::size_t>{0, 2, 1, 3, 4}));
    view.RowsInserted(2, 1, 2);
    CHECK((view.Rows() == std::vector<std::size_t>{0, 3, 2, 1, 4, 5}));
    view.RowsDeleted({1, 3});
    CHECK((view.Rows() == std::vector<std::size_t>{0, 1, 2, 3}));
    CHECK(view.BuiltRowCount() == 4);

    view.ColumnsInserted(0, 1);
    CHECK(view.sorts[0].column == 1);
    view.ColumnsDeleted(1, 1);
    CHECK(view.sorts.empty());
}

void TestRowEditsForView() {
    Document doc = People();
    CHECK(doc.DeleteRowSet({1, 2, 4}));
    CHECK(doc.RowCount() == 2);
    CHECK(doc.Cell(1, 0) == "carl");
    CHECK(doc.Undo());
    CHECK(doc.RowCount() == 5);
    CHECK(doc.Cell(4, 0) == "Dan");
    CHECK(!doc.DeleteRowSet({9}));

    Document sums = Document::FromParsed(ParseCsv("3\n1\n2\n", ','));
    sums.ApplyEdits({F(0, 1, "=A1*10")});
    CHECK(sums.Cell(0, 1) == "30");
    // New order: old row 1, old row 2, old row 0.
    CHECK(sums.PermuteRows({1, 2, 0}));
    CHECK(sums.Cell(0, 0) == "1");
    CHECK(sums.Cell(2, 0) == "3");
    CHECK(sums.Formula(2, 1) == "=A3*10");
    CHECK(sums.Cell(2, 1) == "30");
    CHECK(sums.Undo());
    CHECK(sums.Cell(0, 0) == "3");
    CHECK(sums.Formula(0, 1) == "=A1*10");
    CHECK(!sums.PermuteRows({0, 1, 2}));
    CHECK(!sums.PermuteRows({0, 0, 1}));

    // "Apply sort": only visible non-header rows trade places.
    Document doc2 = People();
    RowView view;
    view.headerRows = 1;
    view.filters[2].allowed = {"Oslo"};
    view.sorts = {{0, true}};
    view.Rebuild(doc2);
    CHECK((view.Rows() == std::vector<std::size_t>{0, 3, 1}));
    CHECK((view.SortedPermutation(doc2) == std::vector<std::size_t>{0, 3, 2, 1, 4}));
}

void TestFilterStore() {
    Document doc = People();
    RowView view;
    view.headerRows = 1;
    view.filters[2].allowed = {"Oslo", "", "q\"x"};
    ColumnFilter older;
    older.isList = false;
    older.op = FilterOp::GreaterEqual;
    older.operand = "26";
    view.filters[1] = older;

    const std::string json = BuildFormulaJson(doc, &view);
    CHECK(!json.empty());
    CHECK(BuildFormulaJson(doc).empty());

    Document loaded = People();
    RowView restored;
    CHECK(ApplyFormulaJson(loaded, json, &restored));
    CHECK(restored.headerRows == 1);
    CHECK(restored.filters.size() == 2);
    CHECK(restored.filters.at(2).isList);
    CHECK((restored.filters.at(2).allowed == std::set<std::string>{"Oslo", "", "q\"x"}));
    CHECK(!restored.filters.at(1).isList);
    CHECK(restored.filters.at(1).op == FilterOp::GreaterEqual);
    CHECK(restored.filters.at(1).operand == "26");

    // Formulas, columns and filters live together.
    Document both = People();
    both.ApplyEdits({F(1, 3, "=B2+1")});
    const std::string combined = BuildFormulaJson(both, &view);
    Document again = People();
    RowView againView;
    CHECK(ApplyFormulaJson(again, combined, &againView));
    CHECK(again.Formula(1, 3) == "=B2+1");
    CHECK(againView.filters.size() == 2);

    RowView headerOnly;
    headerOnly.headerRows = 1;
    CHECK(BuildFormulaJson(doc, &headerOnly).find("\"headerRow\": true") != std::string::npos);
    CHECK(!ApplyFormulaJson(loaded, "{\"filters\": {\"B\": {\"in\": [1]}}}", &restored));
}

void TestMatcher() {
    CellMatcher plain({"oslo", false, false, false});
    CHECK(plain.Valid());
    CHECK(plain.Matches("Oslo city"));
    CHECK(!plain.Matches("Bergen"));
    CellMatcher exact({"Oslo", true, true, false});
    CHECK(exact.Matches("Oslo"));
    CHECK(!exact.Matches("oslo"));
    CHECK(!exact.Matches("Oslo city"));
    CellMatcher regex({"^b.r", false, false, true});
    CHECK(regex.Matches("Bergen"));
    CHECK(!regex.Matches("Oslo"));
    CellMatcher broken({"(", false, false, true});
    CHECK(!broken.Valid());
    CHECK(!broken.Matches("("));
    CellMatcher empty({"", false, false, false});
    CHECK(empty.Empty());
    CHECK(!empty.Matches("x"));
}

class DocSource : public CellSource {
public:
    explicit DocSource(const Document& doc) : doc_(doc) {}
    Value GetCell(std::size_t row, std::size_t col) override { return ValueFromCell(doc_.Cell(row, col)); }
    std::size_t RowCount() const override { return doc_.RowCount(); }
    std::size_t ColumnCount() const override { return doc_.ColumnCount(); }

private:
    const Document& doc_;
};

FormulaInspection Inspect(const Document& doc, std::size_t row, std::size_t col) {
    DocSource source(doc);
    return InspectFormula(doc.Formula(row, col), source, doc.Cell(row, col));
}

void TestInspector() {
    Document doc = Document::FromParsed(ParseCsv("12,7,x\n3,0,\n5,,\n", ','));
    doc.ApplyEdits({F(0, 3, "=A1+B1"), F(1, 3, "=A2/B2"), F(2, 3, "=SUM(A:A)+A1"), F(0, 4, "=FOO(A1)"),
                    F(1, 4, "=(A1+B1"), F(2, 4, "=\"abc"), F(0, 5, "=A1+C1"), F(1, 5, "=D2+1"),
                    F(2, 5, "=A1+B1+")});

    FormulaInspection ok = Inspect(doc, 0, 3);
    CHECK(ok.parsed);
    CHECK(ok.diagnostic.empty());
    CHECK(ok.inputs.size() == 2);
    CHECK(ok.inputs[0].label == "A1" && ok.inputs[0].detail == "12");
    CHECK(ok.inputs[1].label == "B1" && ok.inputs[1].detail == "7");

    FormulaInspection div = Inspect(doc, 1, 3);
    CHECK(div.diagnostic.find("Division by zero") != std::string::npos);

    // Ranges are summarized and clamped to the data; repeated references are listed once.
    FormulaInspection range = Inspect(doc, 2, 3);
    CHECK(range.inputs.size() == 2);
    CHECK(range.inputs[0].label == "A:A");
    CHECK(range.inputs[0].detail == "3 cells: 3 numbers");
    CHECK(range.inputs[1].label == "A1");

    FormulaInspection unknown = Inspect(doc, 0, 4);
    CHECK(unknown.diagnostic.find("FOO()") != std::string::npos);
    CHECK(Inspect(doc, 1, 4).diagnostic.find("Parentheses") != std::string::npos);
    CHECK(!Inspect(doc, 1, 4).parsed);
    CHECK(Inspect(doc, 2, 4).diagnostic.find("closing quote") != std::string::npos);
    CHECK(!Inspect(doc, 2, 5).diagnostic.empty());

    FormulaInspection text = Inspect(doc, 0, 5);
    CHECK(text.diagnostic.find("wrong type") != std::string::npos);
    CHECK(text.inputs[1].detail == "\"x\"");

    // An error in an input is named as the cause.
    FormulaInspection cause = Inspect(doc, 1, 5);
    CHECK(cause.diagnostic.find("D2") != std::string::npos);
    CHECK(cause.inputs[0].isError);

    CHECK(ParseErrorText("#DIV/0!") == ErrorCode::Div0);
    CHECK(!ParseErrorText("#nope"));

    const std::uint64_t before = doc.Revision();
    doc.ApplyEdits({{0, 0, "1"}});
    CHECK(doc.Revision() != before);
}

}  // namespace

int main() {
    TestParsing();
    TestRoundTrip();
    TestPasteDelimiter();
    TestDocumentEditing();
    TestGrowthAndUndo();
    TestBlankDocument();
    TestOutput();
    TestFormulaBasics();
    TestFormulaLanguage();
    TestSumIf();
    TestNumberSpelling();
    TestFormulaDependencies();
    TestStructuralEdits();
    TestFormulaStore();
    TestSortAndFilter();
    TestRowEditsForView();
    TestFilterStore();
    TestMatcher();
    TestInspector();
    if (g_failures == 0) {
        std::puts("All core tests passed.");
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
