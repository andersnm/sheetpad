#pragma once

#include <string>

#include "cell_editor.h"
#include "csv.h"
#include "document.h"
#include "find_bar.h"
#include "formula_store.h"
#include "frame_window.h"
#include "grid_window.h"
#include "import_dialog.h"
#include "inspector_pane.h"
#include "platform.h"
#include "types.h"
#include "view.h"

namespace app {

// Owns the document, the view state and the windows. The window classes call back into it through `App&`; their
// implementations include this header, while their own headers only forward-declare App.
class App {
public:
    App();

    int Run(HINSTANCE instance, int showCommand);

    // Document model. Pos rows are display rows; `view` maps them to document rows.
    sheetpad::Document doc = sheetpad::Document::Blank(kBlankRows, kBlankColumns);
    sheetpad::RowView view;
    bool viewDirty = false;
    bool viewStale = false;
    std::wstring path;
    Pos anchor;
    Pos active;
    DWORD copySequence = 0;
    // Top-left of the copied range, in document coordinates.
    Pos copyOrigin;
    bool copyMoving = false;
    bool sidecarInvalid = false;

    HFONT font = nullptr;
    Metrics m;

    FrameWindow frame;
    GridWindow grid;
    FindBar findBar;
    InspectorPane inspector;

    // Document state.
    void UpdateTitle();
    void RebuildView(std::size_t anchorDoc, std::size_t activeDoc);
    void SyncView();
    void AfterDocumentChanged();
    void SetSelection(Pos anchor, Pos active);
    void MoveActive(int rowDelta, int colDelta, bool extend);
    void ClampSelection();
    int Scale(int value) const;
    // Rows that hold data in the current view (all rows, or the filtered rows).
    int DataRows() const;
    int GridRows() const;
    int GridCols() const;
    std::size_t DocRowOf(int displayRow) const;
    bool IsDirty() const;
    std::wstring CellName(Pos p) const;
    CellRange Selection() const;
    void CreateFonts(HWND window);
    bool TextControlFocused() const;

    // Applies a change to the filters or sort levels and rebuilds the view.
    template <typename Mutate>
    void ChangeView(Mutate&& mutate) {
        const std::size_t anchorDoc = DocRowOf(anchor.row);
        const std::size_t activeDoc = DocRowOf(active.row);
        mutate();
        RebuildView(anchorDoc, activeDoc);
        ClampSelection();
        grid.UpdateScrollbars();
        UpdateTitle();
        grid.Redraw();
    }

    // Commands.
    void RunCommand(UINT command);
    void SortByColumn(int col, bool descending, bool add);

    // Clipboard.
    bool CopySelection(bool moving = false);
    void ClearSelection();
    void FillSelection(bool down);
    void PasteClipboard();
    // Pastes through the import dialog. With `htmlText`, the plain text on the clipboard is parsed as HTML markup.
    void PasteSpecial(bool htmlText);
    // Pastes at the selection as one undo step; a single value fills a larger selection.
    void PasteTable(sheetpad::Table table, std::size_t columnCount, bool internal);

    // Settings shared by Import and Paste Special, kept for the session.
    CsvImportOptions csvImport;
    HtmlImportOptions htmlImport;

    // Files.
    bool LoadFile(const std::wstring& path);
    bool SaveAs();
    bool Save();
    bool ConfirmDiscard();
    void NewDocument();
    void OpenDocument();
    void ImportFile(bool html);

private:
    bool RegisterClasses(HINSTANCE instance);
    void ApplySortToData();
    void RunStructuralCommand(UINT command);
    void ResetView();
    bool SaveTo(const std::wstring& path);
    bool PickPath(bool save, std::wstring& result, UINT filterId = IDS_FILE_FILTER);
    sheetpad::Table SelectionTable() const;
};

}  // namespace app
