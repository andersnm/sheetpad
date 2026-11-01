#pragma once

#include <string>

#include "types.h"
#include "window_handle.h"

namespace app {

// The in-cell EDIT control, a child of the grid.
class CellEditor : public WindowHandle {
public:
    explicit CellEditor(App& app) : app_(app) {}

    void Create(HWND grid);
    bool Editing() const { return editing_; }
    // Starts editing the active cell. Null `initialText` starts from the cell's current content.
    void Begin(const std::wstring* initialText);
    void Commit();
    void Cancel();
    void Position();
    void HandleKey(WPARAM key, bool shift);

protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
    void End();

    App& app_;
    bool editing_ = false;
    Pos cell_;
    std::string original_;
};

}  // namespace app
