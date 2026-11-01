#pragma once

#include <array>
#include <string>

#include "types.h"
#include "window_handle.h"

namespace app {

// The top-level window: menus, status bar and the layout of its children.
class FrameWindow : public WindowHandle {
public:
    explicit FrameWindow(App& app) : app_(app) {}

    bool Create(HINSTANCE instance);
    void Layout();
    void UpdateStatus();

protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
    bool OnCreate(HINSTANCE instance);
    void CreateMenus();
    void UpdateMenu(HMENU menu);

    App& app_;
    WindowHandle status_;
    std::array<std::wstring, 4> statusParts_;
};

}  // namespace app
