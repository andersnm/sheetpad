#pragma once

#include <string_view>

#include "cell_editor.h"
#include "types.h"
#include "window_handle.h"

namespace app {

// The spreadsheet view: painting, scrolling, mouse, keyboard and the filter popup.
class GridWindow : public WindowHandle {
public:
    explicit GridWindow(App& app) : editor(app), app_(app) {}

    CellEditor editor;

    bool Create(HWND parent, HINSTANCE instance);
    int VisibleRows(const RECT& client) const;
    int VisibleCols(const RECT& client) const;
    // Pixel width of a column and the x of its left edge (relative to the first visible column).
    int ColWidth(int col) const;
    int ColLeft(int col) const;
    RECT CellRect(Pos p) const;
    RECT FilterButtonRect(int col) const;
    void UpdateScrollbars();
    void EnsureVisible(Pos p);
    // Repaints the grid and refreshes the status bar and inspector.
    void Redraw();
    void ResetScroll();
    // Fits the columns left..right (data columns only) to their content. With toggleCol >= 0, the whole range is
    // instead reset to the default width when that column is already at its fit width.
    void AutofitColumns(int left, int right, int toggleCol = -1);
    void ShowFilterPopup(int col);

protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
    void ScrollTo(int row, int col);
    void ScrollBar(int bar, int code);
    void Wheel(int delta, bool horizontal);
    Pos HitCell(int x, int y) const;
    int ColAtX(int x) const;
    int MaxFirstCol(const RECT& client) const;
    // The column whose right edge is at (x, y) in the header, or -1.
    int ResizeHandleAt(int x, int y) const;
    int FitWidth(HDC dc, int col, int rows) const;
    void SetColumnWidth(int col, int pixels);
    // Stores a clamped width; returns whether it changed. Does not repaint.
    bool StoreColumnWidth(int col, int pixels);
    void MouseDown(int x, int y, bool doubleClick);
    void MouseMoved(int x, int y);
    void KeyPressed(WPARAM key);
    void ShowContextMenu(int screenX, int screenY);
    void Paint();
    void DrawGrid(HDC dc, const RECT& client);
    void DrawUtf8(HDC dc, std::string_view text, RECT rect, UINT flags);

    App& app_;
    int firstRow_ = 0;
    int firstCol_ = 0;
    DragMode dragMode_ = DragMode::None;
    int resizeCol_ = -1;
    int resizeOriginX_ = 0;
    int resizeStartWidth_ = 0;
    int wheelRemainder_ = 0;
    int hwheelRemainder_ = 0;
    HBITMAP backBitmap_ = nullptr;
    SIZE backSize_{};
    // Clicking a column's dropdown button while its popup is open closes the popup; without this guard the same
    // click would reopen it immediately.
    ULONGLONG filterClosedTick_ = 0;
    int filterClosedColumn_ = -1;
};

}  // namespace app
