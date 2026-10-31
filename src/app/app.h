#pragma once

namespace app {
    
enum Command : UINT {
    kCmdNew = 1001,
    kCmdOpen,
    kCmdSave,
    kCmdSaveAs,
    kCmdExit,
    kCmdUndo,
    kCmdRedo,
    kCmdCut,
    kCmdCopy,
    kCmdPaste,
    kCmdDelete,
    kCmdSelectAll,
    kCmdInsertRowAbove,
    kCmdInsertRowBelow,
    kCmdDeleteRows,
    kCmdDuplicateRow,
    kCmdInsertColumnLeft,
    kCmdInsertColumnRight,
    kCmdDeleteColumns,
    kCmdFillDown,
    kCmdFillRight,
    kCmdFind,
    kCmdFindNext,
    kCmdFindPrev,
    kCmdFindClose,
    kCmdHeaderRow,
    kCmdFilterColumn,
    kCmdClearFilters,
    kCmdSortAsc,
    kCmdSortDesc,
    kCmdSortAddAsc,
    kCmdSortAddDesc,
    kCmdClearSort,
    kCmdApplySort,
    kCmdReapply,
    kCmdInspector,
};

constexpr wchar_t kWindowClass[] = L"SheetpadGrid";
constexpr wchar_t kFrameClass[] = L"SheetpadWindow";
constexpr wchar_t kFilterClass[] = L"SheetpadFilter";
constexpr std::size_t kBlankRows = 20;
constexpr std::size_t kBlankColumns = 8;
constexpr wchar_t kFormulaSuffix[] = L".formulas.json";

constexpr int kEditControlId = 2000;

// Find bar controls.
constexpr int kFindLabelId = 3000;
constexpr int kFindEditId = 3001;
constexpr int kFindNextId = 3002;
constexpr int kFindPrevId = 3003;
constexpr int kFindCaseId = 3004;
constexpr int kFindWholeId = 3005;
constexpr int kFindRegexId = 3006;
constexpr int kFindColumnId = 3007;
constexpr int kFindCloseId = 3008;
constexpr int kFindResultId = 3009;

constexpr UINT kMsgEditKey = WM_APP + 1;
constexpr UINT kMsgEditFocusLost = WM_APP + 2;

struct CellRange {
    int top;
    int left;
    int bottom;
    int right;
};

enum class DragMode { None, Cells, Columns, Rows };

struct Pos {
    int row = 0;
    int col = 0;
    bool operator==(const Pos&) const = default;
};

struct Metrics {
    int dpi = 96;
    int rowNumberWidth = 0;
    int headerHeight = 0;
    int rowHeight = 0;
    int columnWidth = 0;
    int padding = 0;
    int textHeight = 0;
};

struct App {
    sheetpad::Document doc = sheetpad::Document::Blank(kBlankRows, kBlankColumns);
    // Filter and sort state. Pos rows are display rows; `view` maps them to document rows.
    sheetpad::RowView view;
    bool viewDirty = false;
    bool viewStale = false;
    std::wstring path;
    Pos anchor;
    Pos active;
    int firstRow = 0;
    int firstCol = 0;
    HFONT font = nullptr;
    DWORD copySequence = 0;
    // Top-left of the copied range, in document coordinates.
    Pos copyOrigin;
    bool copyMoving = false;
    HWND frame = nullptr;
    HWND grid = nullptr;
    HWND status = nullptr;
    std::array<std::wstring, 4> statusParts;
    HWND findLabel = nullptr;
    HWND findEdit = nullptr;
    HWND findResult = nullptr;
    WNDPROC findEditDefaultProc = nullptr;
    bool findVisible = false;
    std::optional<sheetpad::CellMatcher> matcher;
    bool findColumnOnly = false;
    HWND inspector = nullptr;
    bool inspectorVisible = false;
    HWND edit = nullptr;
    WNDPROC editDefaultProc = nullptr;
    bool editing = false;
    Pos editCell;
    std::string editOriginal;
    DragMode dragMode = DragMode::None;
    bool sidecarInvalid = false;
    HBITMAP backBitmap = nullptr;
    SIZE backSize{};
    Metrics m;
    int wheelRemainder = 0;
    int hwheelRemainder = 0;
};

extern App g;

std::wstring ColumnName(int col);
bool KeyDown(int vk);

// template <typename Mutate>
// void ChangeView(HWND window, Mutate&& mutate);

void CommitEdit(HWND window);
void SetSelection(HWND window, Pos anchor, Pos active);
void UpdateStatus();
RECT FilterButtonRect(int col);
void SortByColumn(HWND window, int col, bool descending, bool add);
std::wstring WindowText(HWND control);

void RebuildView(std::size_t anchorDoc, std::size_t activeDoc);
void ClampSelection();
void UpdateScrollbars(HWND window);
void UpdateTitle(HWND window);
void Redraw(HWND window);

// filter_ui.cpp
void ShowFilterPopup(HWND window, int col);
LRESULT CALLBACK FilterProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
bool FindNext(HWND window, bool forward, bool includeCurrent);
void HandleFindCommand(int id, int code);
void CreateFindBar(HWND frame, HINSTANCE instance);
void ShowFindBar(bool show);

// app_inspector.cpp
// Null when the active cell is not a formula.
const sheetpad::FormulaInspection* ActiveInspection();
void CreateInspector(HWND frame, HINSTANCE instance);
int InspectorWidth();
void UpdateInspector();
void ShowInspector(bool show);

// app_layout.cpp
void CreateFonts(HWND window);
int VisibleRows(const RECT& client);
int VisibleCols(const RECT& client);
RECT CellRect(Pos p);
void EnsureVisible(HWND window, Pos p);
void UpdateScrollbars(HWND window);
void Redraw(HWND window);

// app_document.cpp
void UpdateTitle(HWND window);
void AfterDocumentChanged(HWND window);
void SetSelection(HWND window, Pos anchor, Pos active);
void MoveActive(HWND window, int rowDelta, int colDelta, bool extend);

int Scale(int value);
int DataRows();
int GridCols();
int GridRows();
std::size_t DocRowOf(int displayRow);
bool IsDirty();

// app_frame.cpp
LRESULT CALLBACK FrameProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
void LayoutFrame();
void UpdateStatus();


// Applies a change to the filters or sort levels and rebuilds the view.
template <typename Mutate>
void ChangeView(HWND window, Mutate&& mutate) {
    const std::size_t anchorDoc = DocRowOf(g.anchor.row);
    const std::size_t activeDoc = DocRowOf(g.active.row);
    mutate();
    RebuildView(anchorDoc, activeDoc);
    ClampSelection();
    UpdateScrollbars(window);
    UpdateTitle(window);
    Redraw(window);
}

std::wstring ColumnName(int col);
std::wstring CellName(Pos p);
std::wstring BaseName(const std::wstring& path);
CellRange Selection();
void ClampSelection();
int FloorDiv(int value, int divisor);

// app_clipboard.cpp
bool CopySelection(HWND window, bool moving = false);
void ClearSelection(HWND window);
void FillSelection(HWND window, bool down);
void PasteClipboard(HWND window);

// app_file.cpp
bool LoadFile(HWND window, const std::wstring& path);
bool SaveAs(HWND window);
bool Save(HWND window);
bool ConfirmDiscard(HWND window);
void NewDocument(HWND window);
void OpenDocument(HWND window);

// app_drawing.cpp
RECT FilterButtonRect(int col);
void PaintWindow(HWND window);

// app_cell_editor.cpp
void CreateEditControl(HWND window);
void PositionEdit();
void BeginEdit(HWND window, const std::wstring* initialText);
void CommitEdit(HWND window);
// LRESULT CALLBACK EditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
void HandleEditKey(HWND window, WPARAM key, bool shift);

// Shows the busy cursor while a long operation (loading, re-sorting a large file) blocks the UI thread.
class WaitCursor {
public:
    WaitCursor() : previous_(SetCursor(LoadCursorW(nullptr, IDC_WAIT))) {}
    ~WaitCursor() { SetCursor(previous_); }
    WaitCursor(const WaitCursor&) = delete;
    WaitCursor& operator=(const WaitCursor&) = delete;

private:
    HCURSOR previous_;
};

}
