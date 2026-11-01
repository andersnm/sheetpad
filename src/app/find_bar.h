#pragma once

#include <optional>

#include "types.h"
#include "view.h"
#include "window_handle.h"

namespace app {

// The subclassed EDIT control of the find bar.
class FindEdit : public WindowHandle {
public:
    explicit FindEdit(App& app) : app_(app) {}

protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
    App& app_;
};

// The find controls, children of the frame.
class FindBar {
public:
    explicit FindBar(App& app) : app_(app), edit_(app) {}

    void Create(HWND frame, HINSTANCE instance);
    bool Visible() const { return visible_; }
    HWND Edit() const { return edit_; }
    const std::optional<sheetpad::CellMatcher>& Matcher() const { return matcher_; }
    void Show(bool show);
    void Layout(int top);
    bool FindNext(bool forward, bool includeCurrent);
    void HandleCommand(int id, int code);

private:
    void UpdateMatcher();
    bool Checked(int id) const;
    void SetResult(const wchar_t* text);

    App& app_;
    HWND frame_ = nullptr;
    FindEdit edit_;
    WindowHandle result_;
    bool visible_ = false;
    bool columnOnly_ = false;
    std::optional<sheetpad::CellMatcher> matcher_;
};

}  // namespace app
