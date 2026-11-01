#pragma once

#include <string>

#include "csv.h"
#include "document.h"
#include "platform.h"

namespace app {

class App;

// Settings shared by Import and Paste Special; they live for the session only.
struct CsvImportOptions {
    int separator = 0;  // 0 auto, 1 comma, 2 semicolon, 3 tab, 4 pipe, 5 space, 6 custom
    std::wstring customSeparator;
    int quote = 0;  // 0 double, 1 single, 2 none, 3 custom
    std::wstring customQuote;
    int encoding = 0;  // 0 auto, then the sheetpad::Encoding values plus one
};

struct HtmlImportOptions {
    int spans = 1;  // 0 duplicate, 1 top-left only, 2 ignore
    int encoding = 0;
};

struct ImportResult {
    sheetpad::Table table;
    std::size_t columnCount = 0;
    char delimiter = ',';
    sheetpad::Encoding encoding = sheetpad::Encoding::Utf8;
    bool crlf = true;
    bool trailingNewline = true;
};

// The modal dialog that previews and configures an import. `data` is the raw file content, or UTF-8 text from the
// clipboard (which is never re-decoded).
class ImportDialog {
public:
    enum class Kind { Csv, Html };
    enum class Source { File, Clipboard };

    ImportDialog(App& app, Kind kind, Source source, std::string data);

    // Returns false when the user cancels.
    bool Run(ImportResult& result);

private:
    struct Parsed {
        ImportResult result;
        bool unterminatedQuote = false;
        bool truncated = false;
        char detectedDelimiter = 0;
    };

    static INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR Handle(UINT message, WPARAM wParam, LPARAM lParam);
    void Initialize();
    void ReadControls();
    void Refresh();
    void Accept();
    void FillPreview(const sheetpad::Table& table, std::size_t columnCount);
    bool Parse(bool full, Parsed& out) const;
    sheetpad::Encoding EffectiveEncoding(int selection) const;

    HWND Item(int id) const { return GetDlgItem(dialog_, id); }

    App& app_;
    Kind kind_;
    Source source_;
    std::string data_;
    HWND dialog_ = nullptr;
    bool initializing_ = true;
    sheetpad::Encoding detected_ = sheetpad::Encoding::Utf8;
    ImportResult result_;
};

}  // namespace app
