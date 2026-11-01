#pragma once

#include <string>

#include "platform.h"

// A minimal HWND wrapper in the spirit of MFC's CWnd or WTL's CWindow. It converts to HWND, so Win32 calls take it
// directly. The object's address is kept in GWLP_USERDATA, and the virtual WindowProc receives the messages.
// The object must outlive its window.
class WindowHandle {
public:
    WindowHandle() = default;
    virtual ~WindowHandle() = default;
    WindowHandle(const WindowHandle&) = delete;
    WindowHandle& operator=(const WindowHandle&) = delete;

    operator HWND() const { return hwnd_; }

    // Registers a window class whose window procedure routes to WindowHandle::WindowProc.
    static ATOM Register(WNDCLASSEXW windowClass);

    // Creates a window of a class registered with Register and binds it to this object.
    bool Create(DWORD exStyle, const wchar_t* className, const wchar_t* text, DWORD style, int x, int y, int width,
                int height, HWND parent, HMENU menu, HINSTANCE instance);

    // Wraps an existing window without touching its window procedure.
    void Attach(HWND hwnd) { hwnd_ = hwnd; }

    // Replaces the window procedure of an existing control. Messages left unhandled go to the original procedure.
    void Subclass(HWND hwnd);

protected:
    virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);

    // Forwards to the original procedure of a subclassed control, or to DefWindowProc.
    LRESULT DefaultProc(UINT message, WPARAM wParam, LPARAM lParam);

    HWND hwnd_ = nullptr;

private:
    static LRESULT CALLBACK StaticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    WNDPROC original_ = nullptr;
};

std::wstring WindowText(HWND window);
