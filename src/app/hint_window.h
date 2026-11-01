#pragma once

#include <string>
#include <vector>

#include "types.h"
#include "window_handle.h"

namespace app {

// A small borderless popup that never takes focus. In list mode it shows completion candidates; in tip mode it shows
// a function signature with one argument in bold.
class HintWindow : public WindowHandle {
public:
    struct Item {
        std::wstring name;
        std::wstring description;
    };

    HintWindow(App& app, bool isList) : app_(app), isList_(isList) {}
    ~HintWindow() override;

    // Called when an item is double-clicked.
    void SetAcceptHandler(void (*handler)(void* context, std::size_t index), void* context) {
        acceptHandler_ = handler;
        acceptContext_ = context;
    }

    // `anchor` is the screen position of the top-left corner.
    void ShowList(std::vector<Item> items, POINT anchor);
    // `anchor` is the screen position of the bottom-left corner.
    void ShowTip(const std::wstring& text, std::size_t boldStart, std::size_t boldLength, POINT anchor);
    void Hide();
    bool Visible() const { return hwnd_ && IsWindowVisible(hwnd_); }

    void MoveSelection(int delta);
    std::size_t Selected() const { return selected_; }

protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
    bool EnsureCreated();
    void Place(int width, int height, POINT anchor, bool anchorIsBottom);
    void EnsureVisible();
    void Paint();

    App& app_;
    bool isList_;
    HFONT boldFont_ = nullptr;
    std::vector<Item> items_;
    std::size_t selected_ = 0;
    std::size_t top_ = 0;
    std::wstring text_;
    std::size_t boldStart_ = 0;
    std::size_t boldLength_ = 0;
    void (*acceptHandler_)(void*, std::size_t) = nullptr;
    void* acceptContext_ = nullptr;
};

}  // namespace app
