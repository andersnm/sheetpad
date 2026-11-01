#include "window_handle.h"

ATOM WindowHandle::Register(WNDCLASSEXW windowClass) {
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = StaticProc;
    return RegisterClassExW(&windowClass);
}

bool WindowHandle::Create(DWORD exStyle, const wchar_t* className, const wchar_t* text, DWORD style, int x, int y,
                          int width, int height, HWND parent, HMENU menu, HINSTANCE instance) {
    return CreateWindowExW(exStyle, className, text, style, x, y, width, height, parent, menu, instance, this) !=
           nullptr;
}

void WindowHandle::Subclass(HWND hwnd) {
    hwnd_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    original_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(StaticProc)));
}

LRESULT WindowHandle::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
    return DefaultProc(message, wParam, lParam);
}

LRESULT WindowHandle::DefaultProc(UINT message, WPARAM wParam, LPARAM lParam) {
    if (original_) {
        return CallWindowProcW(original_, hwnd_, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

LRESULT CALLBACK WindowHandle::StaticProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    WindowHandle* self = nullptr;
    if (message == WM_NCCREATE) {
        self = static_cast<WindowHandle*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        if (self) {
            self->hwnd_ = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    } else {
        self = reinterpret_cast<WindowHandle*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    const LRESULT result = self->WindowProc(message, wParam, lParam);
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
    }
    return result;
}

std::wstring WindowText(HWND window) {
    const int length = GetWindowTextLengthW(window);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(window, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}
