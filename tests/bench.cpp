// Large-file benchmark for the portable core. Usage: bench [rows] [columns]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "csv.h"
#include "document.h"
#include "formula_store.h"
#include "view.h"

using namespace sheetpad;
using Clock = std::chrono::steady_clock;

namespace {

template <typename F>
double Time(const char* label, F&& body) {
    const auto start = Clock::now();
    body();
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    std::printf("%-34s %9.1f ms\n", label, ms);
    return ms;
}

std::string MakeCsv(std::size_t rows, std::size_t columns) {
    std::string text;
    text.reserve(rows * columns * 8);
    for (std::size_t c = 0; c < columns; ++c) {
        text += (c ? "," : "") + std::string("col") + std::to_string(c);
    }
    text += "\r\n";
    unsigned state = 12345;
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < columns; ++c) {
            state = state * 1664525u + 1013904223u;
            if (c) {
                text += ',';
            }
            if (c % 3 == 0) {
                text += std::to_string(state % 100000);
            } else if (c % 3 == 1) {
                text += "name" + std::to_string(state % 5000);
            } else {
                text += "\"city, " + std::to_string(state % 40) + "\"";
            }
        }
        text += "\r\n";
    }
    return text;
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t rows = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1000000;
    const std::size_t columns = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 10;
    std::printf("%zu rows x %zu columns\n", rows, columns);

    std::string text;
    Time("generate text", [&] { text = MakeCsv(rows, columns); });
    std::printf("file size: %.1f MB\n", static_cast<double>(text.size()) / 1e6);

    ParsedCsv parsed;
    Time("parse", [&] { parsed = ParseCsv(text, DetectDelimiter(text)); });
    Document doc;
    Time("build document", [&] { doc = Document::FromParsed(std::move(parsed)); });
    Time("serialize", [&] { text = doc.Serialize(); });

    Time("edit one cell", [&] { doc.ApplyEdits({{10, 1, "edited"}}); });
    Time("edit one formula (SUM column)", [&] { doc.ApplyEdits({{10, 0, "=SUM(A2:A1000)", true}}); });
    Time("edit cell with formula present", [&] { doc.ApplyEdits({{11, 1, "edited2"}}); });
    Time("undo", [&] { doc.Undo(); });
    Time("insert row", [&] { doc.InsertRows(5, 1); });
    Time("undo insert row", [&] { doc.Undo(); });

    {
        std::vector<CellEdit> fill;
        const std::size_t count = std::min<std::size_t>(rows - 1, 100000);
        for (std::size_t r = 1; r <= count; ++r) {
            fill.push_back({r, columns - 1, "=A" + std::to_string(r + 1) + "*2+B" + std::to_string(r + 1), true});
        }
        Time("fill 100k formulas (one step)", [&] { doc.ApplyEdits(std::move(fill)); });
        Time("edit one cell, 100k formulas", [&] { doc.ApplyEdits({{20, 0, "7"}}); });
        Time("undo, 100k formulas", [&] { doc.Undo(); });
        Time("insert row, 100k formulas", [&] { doc.InsertRows(5, 1); });
        for (int i = 0; i < 5; ++i) {
            doc.InsertRows(5, 1);
        }
        Time("delete row, 100k formulas", [&] { doc.DeleteRows(5, 1); });
        Time("undo delete", [&] { doc.Undo(); });
    }
#ifdef __linux__
    if (FILE* status = std::fopen("/proc/self/status", "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), status)) {
            if (std::strncmp(line, "VmRSS", 5) == 0 || std::strncmp(line, "VmHWM", 5) == 0) {
                std::printf("%s", line);
            }
        }
        std::fclose(status);
    }
#endif

    RowView view;
    view.headerRows = 1;
    Time("distinct values (col 1)", [&] { (void)view.DistinctValues(doc, 1, true); });
    ColumnFilter filter;
    filter.allowed = {"name1", "name2", "name3"};
    view.filters[1] = filter;
    Time("filter rebuild", [&] { view.Rebuild(doc); });
    view.sorts.push_back({0, false});
    Time("filter + sort rebuild", [&] { view.Rebuild(doc); });
    view.filters.clear();
    Time("sort only rebuild", [&] { view.Rebuild(doc); });
    view.sorts = {{1, true}, {0, false}};
    Time("two-level text sort rebuild", [&] { view.Rebuild(doc); });

    FindOptions options;
    options.text = "name42";
    CellMatcher matcher(options);
    std::size_t hits = 0;
    Time("find scan (all cells)", [&] {
        for (std::size_t r = 0; r < doc.RowCount(); ++r) {
            for (std::size_t c = 0; c < doc.ColumnCount(); ++c) {
                hits += matcher.Matches(doc.Cell(r, c)) ? 1 : 0;
            }
        }
    });
    std::printf("hits: %zu\n", hits);
    return 0;
}
