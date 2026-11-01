#pragma once

#include <cstddef>
#include <string>

#include "platform.h"
#include "resource.h"

// Constants and small types shared by every component of the application.
namespace app {

// Typed names for the command identifiers of resource.h.
enum Command : UINT {
    kCmdNew = IDM_FILE_NEW,
    kCmdOpen = IDM_FILE_OPEN,
    kCmdSave = IDM_FILE_SAVE,
    kCmdSaveAs = IDM_FILE_SAVEAS,
    kCmdExit = IDM_FILE_EXIT,
    kCmdUndo = IDM_EDIT_UNDO,
    kCmdRedo = IDM_EDIT_REDO,
    kCmdCut = IDM_EDIT_CUT,
    kCmdCopy = IDM_EDIT_COPY,
    kCmdPaste = IDM_EDIT_PASTE,
    kCmdDelete = IDM_EDIT_DELETE,
    kCmdSelectAll = IDM_EDIT_SELECTALL,
    kCmdInsertRowAbove = IDM_TABLE_INSERTROWABOVE,
    kCmdInsertRowBelow = IDM_TABLE_INSERTROWBELOW,
    kCmdDeleteRows = IDM_TABLE_DELETEROWS,
    kCmdDuplicateRow = IDM_TABLE_DUPLICATEROW,
    kCmdInsertColumnLeft = IDM_TABLE_INSERTCOLUMNLEFT,
    kCmdInsertColumnRight = IDM_TABLE_INSERTCOLUMNRIGHT,
    kCmdDeleteColumns = IDM_TABLE_DELETECOLUMNS,
    kCmdAutofit = IDM_TABLE_AUTOFIT,
    kCmdEncodingAnsi = IDM_ENCODING_ANSI,
    kCmdEncodingUtf8 = IDM_ENCODING_UTF8,
    kCmdEncodingUtf8Bom = IDM_ENCODING_UTF8BOM,
    kCmdEncodingUtf16Be = IDM_ENCODING_UTF16BE,
    kCmdEncodingUtf16Le = IDM_ENCODING_UTF16LE,
    kCmdFillDown = IDM_EDIT_FILLDOWN,
    kCmdFillRight = IDM_EDIT_FILLRIGHT,
    kCmdFind = IDM_EDIT_FIND,
    kCmdFindNext = IDM_EDIT_FINDNEXT,
    kCmdFindPrev = IDM_EDIT_FINDPREV,
    kCmdFindClose = IDM_EDIT_FINDCLOSE,
    kCmdHeaderRow = IDM_DATA_HEADERROW,
    kCmdFilterColumn = IDM_DATA_FILTERCOLUMN,
    kCmdClearFilters = IDM_DATA_CLEARFILTERS,
    kCmdSortAsc = IDM_DATA_SORTASC,
    kCmdSortDesc = IDM_DATA_SORTDESC,
    kCmdSortAddAsc = IDM_DATA_SORTADDASC,
    kCmdSortAddDesc = IDM_DATA_SORTADDDESC,
    kCmdClearSort = IDM_DATA_CLEARSORT,
    kCmdApplySort = IDM_DATA_APPLYSORT,
    kCmdReapply = IDM_DATA_REAPPLY,
    kCmdInspector = IDM_VIEW_INSPECTOR,
};

constexpr wchar_t kWindowClass[] = L"SheetpadGrid";
constexpr wchar_t kFrameClass[] = L"SheetpadWindow";
constexpr wchar_t kFilterClass[] = L"SheetpadFilter";
constexpr std::size_t kBlankRows = 20;
constexpr std::size_t kBlankColumns = 8;
constexpr wchar_t kFormulaSuffix[] = L".formulas.json";

constexpr int kEditControlId = 2000;

constexpr int kInspectorId = 5000;

constexpr UINT kMsgEditKey = WM_APP + 1;
constexpr UINT kMsgEditFocusLost = WM_APP + 2;

struct CellRange {
    int top;
    int left;
    int bottom;
    int right;
};

enum class DragMode { None, Cells, Columns, Rows, ResizeColumn };

// Default and limits of column widths, in device-independent pixels (96 dpi).
constexpr int kDefaultColumnWidth = 140;
constexpr int kMinColumnWidth = 24;
constexpr int kMaxColumnWidth = 1600;

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

inline bool KeyDown(int vk) {
    return (GetKeyState(vk) & 0x8000) != 0;
}

// The string table entry `id` of the resource file.
std::wstring Res(UINT id);
std::wstring ColumnName(int col);
std::wstring BaseName(const std::wstring& path);
int FloorDiv(int value, int divisor);

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

class App;

}  // namespace app
