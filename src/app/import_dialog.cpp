#include "import_dialog.h"

#include <algorithm>
#include <vector>

#include "app.h"
#include "html_table.h"
#include "text_encoding.h"

using namespace sheetpad;

namespace app {

namespace {

constexpr std::size_t kPreviewBytes = 256 * 1024;
constexpr std::size_t kPreviewRows = 50;
constexpr std::size_t kPreviewColumns = 100;
constexpr std::size_t kPreviewCellChars = 200;
constexpr int kCustomSeparator = 6;
constexpr int kCustomQuote = 3;

std::vector<std::wstring> Split(const std::wstring& text, wchar_t separator) {
    std::vector<std::wstring> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find(separator, start);
        parts.push_back(text.substr(start, end == std::wstring::npos ? end : end - start));
        if (end == std::wstring::npos) {
            return parts;
        }
        start = end + 1;
    }
}

void FillCombo(HWND combo, UINT listId) {
    for (const std::wstring& item : Split(Res(listId), L'~')) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    }
}

std::wstring TextOf(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    text.resize(static_cast<std::size_t>(GetWindowTextW(control, text.data(), length + 1)));
    return text;
}

std::wstring Detected(const std::wstring& what) {
    std::wstring text = Res(IDS_IMP_DETECTED);
    text.replace(text.find(L"%1"), 2, what);
    return text;
}

// A custom separator or quote must be a single printable ASCII character; anything else falls back.
char CustomChar(const std::wstring& text, char fallback) {
    if (text.size() == 1 && text[0] >= 0x20 && text[0] < 0x7F) {
        return static_cast<char>(text[0]);
    }
    return fallback;
}

char QuoteChar(const CsvImportOptions& options) {
    switch (options.quote) {
    case 1: return '\'';
    case 2: return 0;
    case kCustomQuote: return CustomChar(options.customQuote, '"');
    default: return '"';
    }
}

// Newlines inside a cell would break the one-line preview rows, so they show as a return arrow.
std::wstring PreviewText(const std::string& cell) {
    std::wstring text = Utf8ToWide(cell);
    if (text.size() > kPreviewCellChars) {
        text.resize(kPreviewCellChars);
        text.push_back(L'\u2026');
    }
    std::wstring result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\r' || text[i] == L'\n') {
            if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') {
                ++i;
            }
            result.push_back(L'\u21B5');
        } else {
            result.push_back(text[i]);
        }
    }
    return result;
}

}  // namespace

ImportDialog::ImportDialog(App& app, Kind kind, Source source, std::string data)
    : app_(app), kind_(kind), source_(source), data_(std::move(data)) {}

bool ImportDialog::Run(ImportResult& result) {
    const int templateId = kind_ == Kind::Csv ? IDD_IMPORT_CSV : IDD_IMPORT_HTML;
    const INT_PTR outcome = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(templateId), app_.frame,
                                            DialogProc, reinterpret_cast<LPARAM>(this));
    if (outcome != IDOK) {
        return false;
    }
    result = std::move(result_);
    return true;
}

INT_PTR CALLBACK ImportDialog::DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto* self = reinterpret_cast<ImportDialog*>(lParam);
        SetWindowLongPtrW(dialog, DWLP_USER, static_cast<LONG_PTR>(reinterpret_cast<INT_PTR>(self)));
        self->dialog_ = dialog;
        self->Initialize();
        return TRUE;
    }
    auto* self = reinterpret_cast<ImportDialog*>(GetWindowLongPtrW(dialog, DWLP_USER));
    return self ? self->Handle(message, wParam, lParam) : FALSE;
}

INT_PTR ImportDialog::Handle(UINT message, WPARAM wParam, LPARAM) {
    if (message != WM_COMMAND) {
        return FALSE;
    }
    const int id = LOWORD(wParam);
    const int code = HIWORD(wParam);
    if (id == IDOK) {
        Accept();
    } else if (id == IDCANCEL) {
        EndDialog(dialog_, IDCANCEL);
    } else if (id >= IDC_IMP_SEPARATOR && id <= IDC_IMP_SPAN_IGNORE &&
               (code == CBN_SELCHANGE || code == EN_CHANGE || code == BN_CLICKED)) {
        Refresh();
    }
    return TRUE;
}

Encoding ImportDialog::EffectiveEncoding(int selection) const {
    return selection <= 0 ? detected_ : static_cast<Encoding>(selection - 1);
}

void ImportDialog::Initialize() {
    const bool csv = kind_ == Kind::Csv;
    const bool paste = source_ == Source::Clipboard;
    UINT titleId = IDS_IMP_TITLE_IMPORT_CSV;
    if (paste) {
        titleId = csv ? IDS_IMP_TITLE_PASTE_CSV : IDS_IMP_TITLE_PASTE_HTML;
    } else if (!csv) {
        titleId = IDS_IMP_TITLE_IMPORT_HTML;
    }
    SetWindowTextW(dialog_, Res(titleId).c_str());
    SetWindowTextW(Item(IDOK), Res(paste ? IDS_IMP_BUTTON_PASTE : IDS_IMP_BUTTON_IMPORT).c_str());

    FillCombo(Item(IDC_IMP_ENCODING), IDS_IMP_ENCODINGS);
    if (paste) {
        // Clipboard text is already decoded.
        EnableWindow(Item(IDC_IMP_ENCODING), FALSE);
    } else {
        detected_ = DetectEncoding(data_);
        const auto names = Split(Res(IDS_IMP_ENCODINGS), L'~');
        SetWindowTextW(Item(IDC_IMP_ENC_DETECTED),
                       Detected(names[static_cast<std::size_t>(detected_) + 1]).c_str());
    }

    if (csv) {
        const CsvImportOptions& options = app_.csvImport;
        FillCombo(Item(IDC_IMP_SEPARATOR), IDS_IMP_SEPARATORS);
        FillCombo(Item(IDC_IMP_QUOTE), IDS_IMP_QUOTES);
        SendMessageW(Item(IDC_IMP_SEPARATOR), CB_SETCURSEL, static_cast<WPARAM>(options.separator), 0);
        SendMessageW(Item(IDC_IMP_QUOTE), CB_SETCURSEL, static_cast<WPARAM>(options.quote), 0);
        SendMessageW(Item(IDC_IMP_ENCODING), CB_SETCURSEL, static_cast<WPARAM>(options.encoding), 0);
        SendMessageW(Item(IDC_IMP_SEP_CUSTOM), EM_SETLIMITTEXT, 1, 0);
        SendMessageW(Item(IDC_IMP_QUOTE_CUSTOM), EM_SETLIMITTEXT, 1, 0);
        SetWindowTextW(Item(IDC_IMP_SEP_CUSTOM), options.customSeparator.c_str());
        SetWindowTextW(Item(IDC_IMP_QUOTE_CUSTOM), options.customQuote.c_str());
    } else {
        const HtmlImportOptions& options = app_.htmlImport;
        SendMessageW(Item(IDC_IMP_ENCODING), CB_SETCURSEL, static_cast<WPARAM>(options.encoding), 0);
        CheckRadioButton(dialog_, IDC_IMP_SPAN_DUPLICATE, IDC_IMP_SPAN_IGNORE,
                         IDC_IMP_SPAN_DUPLICATE + options.spans);
    }

    constexpr DWORD style = LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES;
    SendMessageW(Item(IDC_IMP_PREVIEW), LVM_SETEXTENDEDLISTVIEWSTYLE, style, style);
    initializing_ = false;
    Refresh();
}

void ImportDialog::ReadControls() {
    const auto selection = [&](int id) {
        return std::max(0, static_cast<int>(SendMessageW(Item(id), CB_GETCURSEL, 0, 0)));
    };
    if (kind_ == Kind::Csv) {
        CsvImportOptions& options = app_.csvImport;
        options.separator = selection(IDC_IMP_SEPARATOR);
        options.quote = selection(IDC_IMP_QUOTE);
        options.encoding = selection(IDC_IMP_ENCODING);
        options.customSeparator = TextOf(Item(IDC_IMP_SEP_CUSTOM));
        options.customQuote = TextOf(Item(IDC_IMP_QUOTE_CUSTOM));
    } else {
        HtmlImportOptions& options = app_.htmlImport;
        options.encoding = selection(IDC_IMP_ENCODING);
        for (int i = 0; i < 3; ++i) {
            if (IsDlgButtonChecked(dialog_, IDC_IMP_SPAN_DUPLICATE + i) == BST_CHECKED) {
                options.spans = i;
            }
        }
    }
}

bool ImportDialog::Parse(bool full, Parsed& out) const {
    const bool csv = kind_ == Kind::Csv;
    const int encodingSelection = csv ? app_.csvImport.encoding : app_.htmlImport.encoding;

    std::string text;
    if (source_ == Source::Clipboard) {
        text = data_;
        if (!full && text.size() > kPreviewBytes) {
            text.resize(kPreviewBytes);
            out.truncated = true;
        }
    } else {
        std::string_view bytes = data_;
        if (!full && csv && bytes.size() > kPreviewBytes) {
            bytes = bytes.substr(0, kPreviewBytes);
            out.truncated = true;
        }
        text = DecodeBytes(bytes, EffectiveEncoding(encodingSelection));
        out.result.encoding = csv ? EffectiveEncoding(encodingSelection) : Encoding::Utf8;
    }

    if (!csv) {
        SpanFill fill = SpanFill::Blank;
        if (app_.htmlImport.spans == 0) {
            fill = SpanFill::Repeat;
        } else if (app_.htmlImport.spans == 2) {
            fill = SpanFill::Ignore;
        }
        ParseHtml(text, out.result.table, fill);
        for (const Row& row : out.result.table) {
            out.result.columnCount = std::max(out.result.columnCount, row.size());
        }
        return true;
    }

    const CsvImportOptions& options = app_.csvImport;
    CsvParseOptions parse;
    parse.quote = QuoteChar(options);
    switch (options.separator) {
    case 1: parse.delimiter = ','; break;
    case 2: parse.delimiter = ';'; break;
    case 3: parse.delimiter = '\t'; break;
    case 4: parse.delimiter = '|'; break;
    case 5:
        parse.delimiter = ' ';
        parse.collapseDelimiters = true;
        break;
    case kCustomSeparator: parse.delimiter = CustomChar(options.customSeparator, ','); break;
    default:
        parse.delimiter = source_ == Source::File ? DetectDelimiter(text, parse.quote) : DetectPasteDelimiter(text);
        out.detectedDelimiter = parse.delimiter;
        break;
    }

    ParsedCsv parsed = ParseCsv(text, parse);
    out.unterminatedQuote = parsed.unterminatedQuote;
    out.result.table = std::move(parsed.rows);
    out.result.columnCount = parsed.columnCount;
    out.result.crlf = parsed.crlf;
    out.result.trailingNewline = parsed.trailingNewline;
    // Only the delimiters the writer and the sidecar logic know are kept; others save as comma.
    const char delimiter = parsed.delimiter;
    out.result.delimiter =
        delimiter == ',' || delimiter == ';' || delimiter == '\t' || delimiter == '|' ? delimiter : ',';
    return true;
}

void ImportDialog::FillPreview(const Table& table, std::size_t columnCount) {
    const HWND list = Item(IDC_IMP_PREVIEW);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
    while (SendMessageW(list, LVM_DELETECOLUMN, 0, 0)) {
    }

    const std::size_t columns = std::min(columnCount, kPreviewColumns);
    for (std::size_t c = 0; c < columns; ++c) {
        std::wstring name = ColumnName(static_cast<int>(c));
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.cx = app_.Scale(90);
        column.pszText = name.data();
        SendMessageW(list, LVM_INSERTCOLUMNW, c, reinterpret_cast<LPARAM>(&column));
    }
    const std::size_t rows = std::min(table.size(), kPreviewRows);
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t c = 0; c < columns && c < table[r].size(); ++c) {
            std::wstring text = PreviewText(table[r][c]);
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(r);
            item.iSubItem = static_cast<int>(c);
            item.pszText = text.data();
            SendMessageW(list, c == 0 ? LVM_INSERTITEMW : LVM_SETITEMTEXTW, c == 0 ? 0 : r,
                         reinterpret_cast<LPARAM>(&item));
        }
        if (table[r].empty()) {
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(r);
            wchar_t empty[] = L"";
            item.pszText = empty;
            SendMessageW(list, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
        }
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
}

void ImportDialog::Refresh() {
    if (initializing_) {
        return;
    }
    ReadControls();
    const bool csv = kind_ == Kind::Csv;
    if (csv) {
        EnableWindow(Item(IDC_IMP_SEP_CUSTOM), app_.csvImport.separator == kCustomSeparator);
        EnableWindow(Item(IDC_IMP_QUOTE_CUSTOM), app_.csvImport.quote == kCustomQuote);
    }

    const WaitCursor wait;
    Parsed parsed;
    Parse(false, parsed);

    if (csv) {
        std::wstring label;
        if (parsed.detectedDelimiter != 0) {
            const auto names = Split(Res(IDS_IMP_DELIMITER_NAMES), L'~');
            const std::wstring_view delimiters = L",;\t|";
            const std::size_t index = delimiters.find(static_cast<wchar_t>(parsed.detectedDelimiter));
            label = Detected(index == std::wstring_view::npos ? std::wstring(1, parsed.detectedDelimiter)
                                                              : names[index]);
        }
        SetWindowTextW(Item(IDC_IMP_SEP_DETECTED), label.c_str());
    }

    std::wstring message;
    if (parsed.unterminatedQuote && !parsed.truncated) {
        message = Res(IDS_IMP_ERR_QUOTE);
    } else if (parsed.result.table.empty()) {
        message = Res(IDS_IMP_ERR_EMPTY);
    }
    SetWindowTextW(Item(IDC_IMP_MESSAGE), message.c_str());
    EnableWindow(Item(IDOK), message.empty());
    FillPreview(parsed.result.table, parsed.result.columnCount);
}

void ImportDialog::Accept() {
    ReadControls();
    Parsed parsed;
    {
        const WaitCursor wait;
        Parse(true, parsed);
    }
    if (parsed.unterminatedQuote) {
        MessageBoxW(dialog_, Res(IDS_ERR_UNTERMINATED_QUOTE).c_str(), Res(IDS_APP_TITLE).c_str(),
                    MB_OK | MB_ICONERROR);
        return;
    }
    if (parsed.result.table.empty()) {
        MessageBoxW(dialog_, Res(IDS_IMP_ERR_EMPTY).c_str(), Res(IDS_APP_TITLE).c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }
    result_ = std::move(parsed.result);
    EndDialog(dialog_, IDOK);
}

}  // namespace app
