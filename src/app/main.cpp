#include "app.h"

#include <array>
#include <string>

#include "text_encoding.h"

using namespace sheetpad;

namespace app {

namespace {

void InstallTextHooks() {
    TextHooks& hooks = GetTextHooks();
    hooks.sortKey = [](const std::string& text) -> std::string {
        const std::wstring wide = Utf8ToWide(text);
        if (wide.empty()) {
            return {};
        }
        const int size = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_SORTKEY | LINGUISTIC_IGNORECASE, wide.c_str(),
                                       static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr, 0);
        if (size <= 0) {
            return text;
        }
        std::string key(static_cast<std::size_t>(size), '\0');
        const int written = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_SORTKEY | LINGUISTIC_IGNORECASE,
                                          wide.c_str(), static_cast<int>(wide.size()),
                                          reinterpret_cast<LPWSTR>(key.data()), size, nullptr, nullptr, 0);
        if (written <= 0) {
            return text;
        }
        key.resize(static_cast<std::size_t>(written));
        return key;
    };
    hooks.lower = [](const std::string& text) -> std::string {
        std::wstring wide = Utf8ToWide(text);
        if (wide.empty()) {
            return text;
        }
        std::wstring lowered(wide.size(), L'\0');
        const int written = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING,
                                          wide.c_str(), static_cast<int>(wide.size()), lowered.data(),
                                          static_cast<int>(lowered.size()), nullptr, nullptr, 0);
        if (written <= 0) {
            return text;
        }
        lowered.resize(static_cast<std::size_t>(written));
        return WideToUtf8(lowered);
    };
}

}  // namespace

bool App::RegisterClasses(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;

    WNDCLASSEXW frameClass = windowClass;
    frameClass.style = 0;
    frameClass.lpszClassName = kFrameClass;
    frameClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    frameClass.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                                      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    frameClass.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                                        GetSystemMetrics(SM_CXSMICON),
                                                        GetSystemMetrics(SM_CYSMICON), 0));

    WNDCLASSEXW filterClass = windowClass;
    filterClass.lpszClassName = kFilterClass;
    filterClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);

    for (const WNDCLASSEXW* cls : {&windowClass, &frameClass, &filterClass}) {
        if (!WindowHandle::Register(*cls)) {
            MessageBoxW(nullptr, L"The application window could not be registered.", L"Sheetpad",
                        MB_OK | MB_ICONERROR);
            return false;
        }
    }
    return true;
}

int App::Run(HINSTANCE instance, int showCommand) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    HDC screen = GetDC(nullptr);
    m.dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);

    if (!RegisterClasses(instance)) {
        return 1;
    }
    InstallTextHooks();

    if (!frame.Create(instance)) {
        MessageBoxW(nullptr, L"The application window could not be created.", L"Sheetpad", MB_OK | MB_ICONERROR);
        return 1;
    }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1) {
            LoadFile(argv[1]);
        }
        LocalFree(argv);
    }

    ShowWindow(frame, showCommand);
    UpdateWindow(frame);

    const std::array<ACCEL, 23> accelerators{{
        {FVIRTKEY | FCONTROL | FSHIFT, 'I', kCmdInspector},
        {FVIRTKEY | FCONTROL, 'F', kCmdFind},
        {FVIRTKEY, VK_F3, kCmdFindNext},
        {FVIRTKEY | FSHIFT, VK_F3, kCmdFindPrev},
        {FVIRTKEY | FCONTROL, 'N', kCmdNew},
        {FVIRTKEY | FCONTROL, 'O', kCmdOpen},
        {FVIRTKEY | FCONTROL, 'S', kCmdSave},
        {FVIRTKEY | FCONTROL | FSHIFT, 'S', kCmdSaveAs},
        {FVIRTKEY | FCONTROL, 'Z', kCmdUndo},
        {FVIRTKEY | FCONTROL, 'Y', kCmdRedo},
        {FVIRTKEY | FCONTROL | FSHIFT, 'Z', kCmdRedo},
        {FVIRTKEY | FCONTROL, 'X', kCmdCut},
        {FVIRTKEY | FCONTROL, 'C', kCmdCopy},
        {FVIRTKEY | FCONTROL, 'V', kCmdPaste},
        {FVIRTKEY | FCONTROL, 'A', kCmdSelectAll},
        {FVIRTKEY | FCONTROL, 'D', kCmdFillDown},
        {FVIRTKEY | FCONTROL, 'R', kCmdFillRight},
        {FVIRTKEY | FCONTROL | FSHIFT, 'D', kCmdDuplicateRow},
        {FVIRTKEY | FCONTROL, VK_OEM_PLUS, kCmdInsertRowAbove},
        {FVIRTKEY | FCONTROL, VK_OEM_MINUS, kCmdDeleteRows},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_OEM_PLUS, kCmdInsertColumnLeft},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_OEM_MINUS, kCmdDeleteColumns},
        {FVIRTKEY | FCONTROL, VK_ADD, kCmdInsertRowAbove},
    }};
    HACCEL table = CreateAcceleratorTableW(const_cast<ACCEL*>(accelerators.data()),
                                           static_cast<int>(accelerators.size()));

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        const bool editShortcut = TextControlFocused() && message.message == WM_KEYDOWN && KeyDown(VK_CONTROL) &&
                                  (message.wParam == 'A' || message.wParam == 'C' || message.wParam == 'V' ||
                                   message.wParam == 'X' || message.wParam == 'Z' || message.wParam == 'Y');
        if (!editShortcut && TranslateAcceleratorW(frame, table, &message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DestroyAcceleratorTable(table);
    return static_cast<int>(message.wParam);
}

}  // namespace app

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    app::App application;
    return application.Run(instance, showCommand);
}
