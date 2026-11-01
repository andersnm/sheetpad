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
        MessageBoxW(grid, Res(IDS_ERR_READ_FILE).c_str(), Res(IDS_APP_TITLE).c_str(), MB_OK | MB_ICONERROR);
        return false;
    }
    Encoding encoding = Encoding::Utf8;
    const std::string text = DecodeFileBytes(bytes, encoding);
    ParsedCsv parsed = ParseCsv(text, HasExtension(filePath, L".tsv") ? '\t' : DetectDelimiter(text));
    if (parsed.unterminatedQuote) {
        MessageBoxW(grid, Res(IDS_ERR_UNTERMINATED_QUOTE).c_str(), Res(IDS_APP_TITLE).c_str(),
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
        MessageBoxW(grid, Res(IDS_WARN_FORMULA_FILE).c_str(), Res(IDS_APP_TITLE).c_str(), MB_OK | MB_ICONWARNING);
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
        MessageBoxW(grid, Res(IDS_ERR_SAVE_FILE).c_str(), Res(IDS_APP_TITLE).c_str(), MB_OK | MB_ICONERROR);
        return false;
    }

    const std::string formulaJson = BuildFormulaJson(doc, &view);
    const std::wstring formulaPath = targetPath + kFormulaSuffix;
    const bool samePath = targetPath == path;
    if (!formulaJson.empty()) {
        if (!WriteFileAtomic(formulaPath, formulaJson)) {
            MessageBoxW(grid, Res(IDS_ERR_SAVE_FORMULAS).c_str(), Res(IDS_APP_TITLE).c_str(),
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
    // The filter is stored with '|' separators because string table entries cannot contain NULs.
    std::wstring filter = Res(IDS_FILE_FILTER);
    std::replace(filter.begin(), filter.end(), L'|', L'\0');
    filter.push_back(L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = frame;
    dialog.lpstrFilter = filter.c_str();
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
    std::wstring prompt = Res(IDS_CONFIRM_SAVE);
    prompt.replace(prompt.find(L"%1"), 2, path.empty() ? Res(IDS_UNTITLED) : BaseName(path));
    switch (MessageBoxW(grid, prompt.c_str(), Res(IDS_APP_TITLE).c_str(), MB_YESNOCANCEL | MB_ICONWARNING)) {
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
