#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "csv.h"
#include "document.h"
#include "formula_store.h"
#include "text_encoding.h"
#include "view.h"
#include "app.h"

using namespace sheetpad;

namespace app {

// ---------------------------------------------------------------- files

bool ReadFileBytes(const std::wstring& path, std::string& bytes) {
    std::ifstream input{std::filesystem::path(path), std::ios::binary | std::ios::ate};
    if (!input) {
        return false;
    }
    const std::streamsize size = input.tellg();
    input.seekg(0);
    bytes.resize(static_cast<std::size_t>(size));
    return size == 0 || static_cast<bool>(input.read(bytes.data(), size));
}

bool WriteFileAtomic(const std::wstring& path, const std::string& bytes) {
    const std::wstring temporary = path + L".tmp";
    {
        std::ofstream output{std::filesystem::path(temporary), std::ios::binary | std::ios::trunc};
        if (!output) {
            return false;
        }
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            output.close();
            DeleteFileW(temporary.c_str());
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

bool HasExtension(const std::wstring& path, const wchar_t* extension) {
    return _wcsicmp(std::filesystem::path(path).extension().c_str(), extension) == 0;
}

void ResetView() {
    g.anchor = {};
    g.active = {};
    g.firstRow = 0;
    g.firstCol = 0;
}

bool LoadFile(HWND window, const std::wstring& path) {
    const WaitCursor wait;
    std::string bytes;
    if (!ReadFileBytes(path, bytes)) {
        MessageBoxW(window, L"The file could not be read.", L"Sheetpad", MB_OK | MB_ICONERROR);
        return false;
    }
    Encoding encoding = Encoding::Utf8;
    const std::string text = DecodeFileBytes(bytes, encoding);
    ParsedCsv parsed = ParseCsv(text, HasExtension(path, L".tsv") ? '\t' : DetectDelimiter(text));
    if (parsed.unterminatedQuote) {
        MessageBoxW(window, L"The file ends inside a quoted field, so it is not valid CSV.", L"Sheetpad",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    g.doc = Document::FromParsed(std::move(parsed));
    g.doc.encoding = encoding;
    g.sidecarInvalid = false;
    g.view = RowView{};
    g.viewDirty = false;
    std::string sidecar;
    if (ReadFileBytes(path + kFormulaSuffix, sidecar) && !ApplyFormulaJson(g.doc, sidecar, &g.view)) {
        g.sidecarInvalid = true;
        g.view = RowView{};
        MessageBoxW(window,
                    L"The formula file next to this CSV could not be read, so the formulas were not loaded. "
                    L"It will be left untouched unless you save formulas of your own.",
                    L"Sheetpad", MB_OK | MB_ICONWARNING);
    }
    g.path = path;
    ResetView();
    g.view.Rebuild(g.doc);
    g.viewStale = false;
    AfterDocumentChanged(window);
    return true;
}

bool SaveTo(HWND window, const std::wstring& path) {
    CommitEdit(window);
    if (HasExtension(path, L".tsv")) {
        g.doc.delimiter = '\t';
    } else if (HasExtension(path, L".csv") && g.doc.delimiter == '\t') {
        g.doc.delimiter = ',';
    }
    const std::string bytes = EncodeFileText(g.doc.Serialize(), g.doc.encoding);
    if (!WriteFileAtomic(path, bytes)) {
        MessageBoxW(window, L"The file could not be saved.", L"Sheetpad", MB_OK | MB_ICONERROR);
        return false;
    }

    const std::string formulaJson = BuildFormulaJson(g.doc, &g.view);
    const std::wstring formulaPath = path + kFormulaSuffix;
    const bool samePath = path == g.path;
    if (!formulaJson.empty()) {
        if (!WriteFileAtomic(formulaPath, formulaJson)) {
            MessageBoxW(window, L"The CSV was saved, but the formula file could not be written.", L"Sheetpad",
                        MB_OK | MB_ICONERROR);
            return false;
        }
        g.sidecarInvalid = false;
    } else if (!(g.sidecarInvalid && samePath)) {
        // No formulas: remove a stale metadata file so it cannot be applied to this CSV later.
        DeleteFileW(formulaPath.c_str());
        g.sidecarInvalid = false;
    }
    g.path = path;
    g.doc.MarkSaved();
    g.viewDirty = false;
    UpdateTitle(window);
    return true;
}

bool PickPath(HWND window, bool save, std::wstring& result) {
    (void)window;
    std::array<wchar_t, 32768> buffer{};
    if (save && !g.path.empty()) {
        wcsncpy(buffer.data(), g.path.c_str(), buffer.size() - 1);
    }
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g.frame;
    dialog.lpstrFilter =
        L"CSV and tab-separated files (*.csv;*.tsv)\0*.csv;*.tsv\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrDefExt = L"csv";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
                   (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) {
        return false;
    }
    result = buffer.data();
    return true;
}

bool SaveAs(HWND window) {
    CommitEdit(window);
    std::wstring path;
    return PickPath(window, true, path) && SaveTo(window, path);
}

bool Save(HWND window) {
    CommitEdit(window);
    return g.path.empty() ? SaveAs(window) : SaveTo(window, g.path);
}

bool ConfirmDiscard(HWND window) {
    CommitEdit(window);
    if (!IsDirty()) {
        return true;
    }
    const std::wstring prompt =
        L"Do you want to save changes to " + (g.path.empty() ? std::wstring(L"Untitled") : BaseName(g.path)) + L"?";
    switch (MessageBoxW(window, prompt.c_str(), L"Sheetpad", MB_YESNOCANCEL | MB_ICONWARNING)) {
    case IDYES: return Save(window);
    case IDNO: return true;
    default: return false;
    }
}

void NewDocument(HWND window) {
    if (!ConfirmDiscard(window)) {
        return;
    }
    g.doc = Document::Blank(kBlankRows, kBlankColumns);
    g.view = RowView{};
    g.viewDirty = false;
    g.viewStale = false;
    g.path.clear();
    g.sidecarInvalid = false;
    ResetView();
    AfterDocumentChanged(window);
}

void OpenDocument(HWND window) {
    if (!ConfirmDiscard(window)) {
        return;
    }
    std::wstring path;
    if (PickPath(window, false, path)) {
        LoadFile(window, path);
    }
}

}
