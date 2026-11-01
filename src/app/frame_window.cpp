#include "frame_window.h"

#include <algorithm>
#include <string>

#include "app.h"
#include "text_encoding.h"

using namespace sheetpad;

namespace app {

// ---------------------------------------------------------------- frame window

bool FrameWindow::Create(HINSTANCE instance) {
    return WindowHandle::Create(0, kFrameClass, Res(IDS_APP_TITLE).c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, app_.Scale(1000), app_.Scale(680), nullptr,
                                LoadMenuW(instance, MAKEINTRESOURCEW(IDR_MAINMENU)), instance);
}

bool FrameWindow::OnCreate(HINSTANCE instance) {
    if (!app_.grid.Create(*this, instance)) {
        return false;
    }
    status_.Attach(CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                                   *this, nullptr, instance, nullptr));
    if (!status_) {
        return false;
    }
    SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(app_.font), TRUE);
    app_.findBar.Create(*this, instance);
    app_.inspector.Create(*this, instance);
    app_.UpdateTitle();
    UpdateStatus();
    return true;
}

LRESULT FrameWindow::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        return OnCreate(reinterpret_cast<const CREATESTRUCTW*>(lParam)->hInstance) ? 0 : -1;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            Layout();
        }
        return 0;
    case WM_SETFOCUS:
    case WM_ACTIVATE:
        if ((message == WM_SETFOCUS || LOWORD(wParam) != WA_INACTIVE) && !app_.TextControlFocused()) {
            SetFocus(app_.grid);
        }
        return 0;
    case WM_COMMAND:
        if (lParam == 0) {
            app_.RunCommand(LOWORD(wParam));
        }
        return 0;
    case WM_INITMENUPOPUP:
        UpdateMenu(reinterpret_cast<HMENU>(wParam));
        return 0;
    case WM_CLOSE:
        if (app_.ConfirmDiscard()) {
            DestroyWindow(*this);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefaultProc(message, wParam, lParam);
}

void FrameWindow::UpdateMenu(HMENU menu) {
    const bool editing = app_.grid.editor.Editing();
    EnableMenuItem(menu, kCmdUndo, MF_BYCOMMAND | (editing || app_.doc.CanUndo() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, kCmdRedo, MF_BYCOMMAND | (!editing && app_.doc.CanRedo() ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(menu, kCmdHeaderRow, MF_BYCOMMAND | (app_.view.headerRows ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, kCmdInspector, MF_BYCOMMAND | (app_.inspector.Visible() ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(menu, kCmdClearFilters, MF_BYCOMMAND | (app_.view.filters.empty() ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(menu, kCmdClearSort, MF_BYCOMMAND | (app_.view.sorts.empty() ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(menu, kCmdApplySort, MF_BYCOMMAND | (app_.view.sorts.empty() ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(menu, kCmdReapply, MF_BYCOMMAND | (app_.view.Active() ? MF_ENABLED : MF_GRAYED));
}

void FrameWindow::Layout() {
    if (!hwnd_ || !app_.grid || !status_) {
        return;
    }
    SendMessageW(status_, WM_SIZE, 0, 0);
    RECT client{};
    GetClientRect(*this, &client);
    RECT statusRect{};
    GetWindowRect(status_, &statusRect);
    const int statusHeight = statusRect.bottom - statusRect.top;
    const std::array<int, 4> edges{app_.Scale(170), app_.Scale(440), app_.Scale(530), -1};
    SendMessageW(status_, SB_SETPARTS, edges.size(), reinterpret_cast<LPARAM>(edges.data()));
    for (auto& part : statusParts_) {
        part.clear();
    }
    UpdateStatus();

    const int paneWidth = std::min<int>(app_.inspector.Width(), std::max<int>(0, client.right - app_.Scale(200)));
    const int barHeight = app_.findBar.Visible() ? app_.findBar.Height() : 0;
    const int gridHeight = std::max<int>(0, client.bottom - statusHeight - barHeight);
    MoveWindow(app_.grid, 0, 0, client.right - paneWidth, gridHeight, TRUE);
    if (app_.inspector) {
        MoveWindow(app_.inspector, client.right - paneWidth, 0, paneWidth, gridHeight, TRUE);
    }
    if (app_.findBar.Visible()) {
        app_.findBar.Layout(gridHeight, client.right);
    }
}

// Status bar parts: selection, size, state, formula (stretches).
void FrameWindow::UpdateStatus() {
    if (!status_) {
        return;
    }
    const Document& doc = app_.doc;
    const RowView& view = app_.view;
    const Pos active = app_.active;
    const CellRange sel = app_.Selection();
    std::array<std::wstring, 4> parts;
    if (sel.top == sel.bottom && sel.left == sel.right) {
        parts[0] = app_.CellName(active);
    } else {
        parts[0] = app_.CellName({sel.top, sel.left}) + L":" + app_.CellName({sel.bottom, sel.right}) + L"  (" +
                   std::to_wstring(sel.bottom - sel.top + 1) + L" \u00D7 " +
                   std::to_wstring(sel.right - sel.left + 1) + L")";
    }
    if (view.Active()) {
        const std::size_t headerRows = std::min(view.headerRows, view.Rows().size());
        parts[1] = L"Showing " + std::to_wstring(view.Rows().size() - headerRows) + L" of " +
                   std::to_wstring(doc.RowCount() - std::min(view.headerRows, doc.RowCount())) + L" rows \u00D7 " +
                   std::to_wstring(doc.ColumnCount()) + L" columns";
    } else {
        parts[1] = std::to_wstring(doc.RowCount()) + L" rows \u00D7 " + std::to_wstring(doc.ColumnCount()) +
                   L" columns";
    }
    parts[2] = Res(app_.IsDirty() ? IDS_STATUS_MODIFIED : IDS_STATUS_SAVED);
    if (const std::string& formula = doc.Formula(app_.DocRowOf(active.row), static_cast<std::size_t>(active.col));
        !formula.empty()) {
        parts[3] = Res(IDS_STATUS_FORMULA) + Utf8ToWide(formula);
        if (const FormulaInspection* inspection = app_.inspector.ActiveInspection();
            inspection && !inspection->diagnostic.empty()) {
            parts[3] += L"  \u2014  " + Utf8ToWide(inspection->diagnostic);
        }
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i] != statusParts_[i]) {
            statusParts_[i] = parts[i];
            SendMessageW(status_, SB_SETTEXTW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(parts[i].c_str()));
        }
    }
    app_.inspector.Update();
}

}  // namespace app
