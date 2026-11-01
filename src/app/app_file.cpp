#include "app.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>

#include "text_encoding.h"

using namespace sheetpad;

namespace app {

namespace {

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

}  // namespace

// ---------------------------------------------------------------- files

void App::ResetView() {
    anchor = {};
    active = {};
    grid.ResetScroll();
}

bool App::LoadFile(const std::wstring& filePath) {
    const WaitCursor wait;
    std::string bytes;
    if (!ReadFileBytes(filePath, bytes)) {
        MessageBoxW(grid, L"The file could not be read.", L"Sheetpad", MB_OK | MB_ICONERROR);
        return false;
    }
    Encoding encoding = Encoding::Utf8;
    const std::string text = DecodeFileBytes(bytes, encoding);
    ParsedCsv parsed = ParseCsv(text, HasExtension(filePath, L".tsv") ? '\t' : DetectDelimiter(text));
    if (parsed.unterminatedQuote) {
        MessageBoxW(grid, L"The file ends inside a quoted field, so it is not valid CSV.", L"Sheetpad",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    doc = Document::FromParsed(std::move(parsed));
    doc.encoding = encoding;
    sidecarInvalid = false;
    view = RowView{};
    viewDirty = false;
    std::string sidecar;
    if (ReadFileBytes(filePath + kFormulaSuffix, sidecar) && !ApplyFormulaJson(doc, sidecar, &view)) {
        sidecarInvalid = true;
        view = RowView{};
        MessageBoxW(grid,
                    L"The formula file next to this CSV could not be read, so the formulas were not loaded. "
                    L"It will be left untouched unless you save formulas of your own.",
                    L"Sheetpad", MB_OK | MB_ICONWARNING);
    }
    path = filePath;
    ResetView();
    view.Rebuild(doc);
    viewStale = false;
    AfterDocumentChanged();
    return true;
}

bool App::SaveTo(const std::wstring& targetPath) {
    grid.editor.Commit();
    if (HasExtension(targetPath, L".tsv")) {
        doc.delimiter = '\t';
    } else if (HasExtension(targetPath, L".csv") && doc.delimiter == '\t') {
        doc.delimiter = ',';
    }
    const std::string bytes = EncodeFileText(doc.Serialize(), doc.encoding);
    if (!WriteFileAtomic(targetPath, bytes)) {
        MessageBoxW(grid, L"The file could not be saved.", L"Sheetpad", MB_OK | MB_ICONERROR);
        return false;
    }

    const std::string formulaJson = BuildFormulaJson(doc, &view);
    const std::wstring formulaPath = targetPath + kFormulaSuffix;
    const bool samePath = targetPath == path;
    if (!formulaJson.empty()) {
        if (!WriteFileAtomic(formulaPath, formulaJson)) {
            MessageBoxW(grid, L"The CSV was saved, but the formula file could not be written.", L"Sheetpad",
                        MB_OK | MB_ICONERROR);
            return false;
        }
        sidecarInvalid = false;
    } else if (!(sidecarInvalid && samePath)) {
        // No formulas: remove a stale metadata file so it cannot be applied to this CSV later.
        DeleteFileW(formulaPath.c_str());
        sidecarInvalid = false;
    }
    path = targetPath;
    doc.MarkSaved();
    viewDirty = false;
    UpdateTitle();
    return true;
}

bool App::PickPath(bool save, std::wstring& result) {
    std::array<wchar_t, 32768> buffer{};
    if (save && !path.empty()) {
        wcsncpy(buffer.data(), path.c_str(), buffer.size() - 1);
    }
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = frame;
    dialog.lpstrFilter =
        L"CSV and tab-separated files (*.csv;*.tsv)\0*.csv;*.tsv\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrDefExt = L"csv";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) {
        return false;
    }
    result = buffer.data();
    return true;
}

bool App::SaveAs() {
    grid.editor.Commit();
    std::wstring target;
    return PickPath(true, target) && SaveTo(target);
}

bool App::Save() {
    grid.editor.Commit();
    return path.empty() ? SaveAs() : SaveTo(path);
}

bool App::ConfirmDiscard() {
    grid.editor.Commit();
    if (!IsDirty()) {
        return true;
    }
    const std::wstring prompt =
        L"Do you want to save changes to " + (path.empty() ? std::wstring(L"Untitled") : BaseName(path)) + L"?";
    switch (MessageBoxW(grid, prompt.c_str(), L"Sheetpad", MB_YESNOCANCEL | MB_ICONWARNING)) {
    case IDYES: return Save();
    case IDNO: return true;
    default: return false;
    }
}

void App::NewDocument() {
    if (!ConfirmDiscard()) {
        return;
    }
    doc = Document::Blank(kBlankRows, kBlankColumns);
    view = RowView{};
    viewDirty = false;
    viewStale = false;
    path.clear();
    sidecarInvalid = false;
    ResetView();
    AfterDocumentChanged();
}

void App::OpenDocument() {
    if (!ConfirmDiscard()) {
        return;
    }
    std::wstring target;
    if (PickPath(false, target)) {
        LoadFile(target);
    }
}

}  // namespace app
