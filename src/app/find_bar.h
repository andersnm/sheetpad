#pragma once

#include <optional>

#include "types.h"
#include "view.h"
#include "window_handle.h"

namespace app {

// The subclassed EDIT control of the find bar.
class FindEdit : public WindowHandle {
protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;
};

// The find bar: a child dialog of the frame (IDD_FIND) whose controls are looked up once and kept as members.
class FindBar {
public:
    explicit FindBar(App& app) : app_(app) {}

    void Create(HWND frame, HINSTANCE instance);
    bool Visible() const { return visible_; }
    HWND Edit() const { return edit_; }
    int Height() const;
    const std::optional<sheetpad::CellMatcher>& Matcher() const { return matcher_; }
    void Show(bool show);
    void Layout(int top, int width);
    bool FindNext(bool forward, bool includeCurrent);

    // Gives the dialog manager a keyboard message: Tab and arrows between the controls, Enter and Escape, and the
    // Alt mnemonics (also while the grid has the focus). Returns true if the message was consumed.
    bool PreTranslate(MSG& message);

private:
    static INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);
    void HandleCommand(int id, int code);
    void UpdateMatcher();
    void SetResult(const std::wstring& text);
    static bool Checked(const WindowHandle& checkbox);

    App& app_;
    WindowHandle dialog_;
    WindowHandle label_;
    FindEdit edit_;
    WindowHandle previous_;
    WindowHandle next_;
    WindowHandle matchCase_;
    WindowHandle wholeCell_;
    WindowHandle regex_;
    WindowHandle thisColumn_;
    WindowHandle result_;
    WindowHandle close_;
    bool visible_ = false;
    bool columnOnly_ = false;
    std::optional<sheetpad::CellMatcher> matcher_;
};

}  // namespace app
