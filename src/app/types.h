#pragma once

#include <cstddef>
#include <string>

#include "platform.h"

// Constants and small types shared by every component of the application.
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

constexpr int kInspectorId = 5000;

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

inline bool KeyDown(int vk) {
    return (GetKeyState(vk) & 0x8000) != 0;
}

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
