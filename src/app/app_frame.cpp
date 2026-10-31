#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "csv.h"
#include "document.h"
#include "formula_store.h"
#include "text_encoding.h"
#include "view.h"
#include "app.h"

using namespace sheetpad;

namespace app {

    // ---------------------------------------------------------------- window

void CreateMenus(HWND window) {
    (void)window;
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, kCmdNew, L"&New\tCtrl+N");
    AppendMenuW(file, MF_STRING, kCmdOpen, L"&Open...\tCtrl+O");
    AppendMenuW(file, MF_STRING, kCmdSave, L"&Save\tCtrl+S");
    AppendMenuW(file, MF_STRING, kCmdSaveAs, L"Save &As...\tCtrl+Shift+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, kCmdExit, L"E&xit\tAlt+F4");
    HMENU edit = CreatePopupMenu();
    AppendMenuW(edit, MF_STRING, kCmdUndo, L"&Undo\tCtrl+Z");
    AppendMenuW(edit, MF_STRING, kCmdRedo, L"&Redo\tCtrl+Y / Ctrl+Shift+Z");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, kCmdCut, L"Cu&t\tCtrl+X");
    AppendMenuW(edit, MF_STRING, kCmdCopy, L"&Copy\tCtrl+C");
    AppendMenuW(edit, MF_STRING, kCmdPaste, L"&Paste\tCtrl+V");
    AppendMenuW(edit, MF_STRING, kCmdDelete, L"&Delete\tDel");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, kCmdFillDown, L"Fill D&own\tCtrl+D");
    AppendMenuW(edit, MF_STRING, kCmdFillRight, L"Fill Ri&ght\tCtrl+R");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, kCmdFind, L"&Find...\tCtrl+F");
    AppendMenuW(edit, MF_STRING, kCmdFindNext, L"Find &Next\tF3");
    AppendMenuW(edit, MF_STRING, kCmdFindPrev, L"Find Pre&vious\tShift+F3");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, kCmdSelectAll, L"Select &All\tCtrl+A");
    HMENU data = CreatePopupMenu();
    AppendMenuW(data, MF_STRING, kCmdHeaderRow, L"First Row Is &Header");
    AppendMenuW(data, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(data, MF_STRING, kCmdFilterColumn, L"&Filter Column...\tAlt+Down");
    AppendMenuW(data, MF_STRING, kCmdClearFilters, L"&Clear All Filters");
    AppendMenuW(data, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(data, MF_STRING, kCmdSortAsc, L"Sort &Ascending");
    AppendMenuW(data, MF_STRING, kCmdSortDesc, L"Sort &Descending");
    AppendMenuW(data, MF_STRING, kCmdSortAddAsc, L"Add Ascending Sort &Level");
    AppendMenuW(data, MF_STRING, kCmdSortAddDesc, L"Add Descending Sort L&evel");
    AppendMenuW(data, MF_STRING, kCmdClearSort, L"Clear &Sort");
    AppendMenuW(data, MF_STRING, kCmdApplySort, L"Apply Sort to &Data");
    AppendMenuW(data, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(data, MF_STRING, kCmdReapply, L"&Reapply Filter and Sort");
    HMENU table = CreatePopupMenu();
    AppendMenuW(table, MF_STRING, kCmdInsertRowAbove, L"Insert Row &Above\tCtrl+Plus");
    AppendMenuW(table, MF_STRING, kCmdInsertRowBelow, L"Insert Row &Below");
    AppendMenuW(table, MF_STRING, kCmdDeleteRows, L"&Delete Row(s)\tCtrl+Minus");
    AppendMenuW(table, MF_STRING, kCmdDuplicateRow, L"D&uplicate Row\tCtrl+Shift+D");
    AppendMenuW(table, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(table, MF_STRING, kCmdInsertColumnLeft, L"Insert Column &Left\tCtrl+Shift+Plus");
    AppendMenuW(table, MF_STRING, kCmdInsertColumnRight, L"Insert Column &Right");
    AppendMenuW(table, MF_STRING, kCmdDeleteColumns, L"Delete &Column(s)\tCtrl+Shift+Minus");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Edit");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(table), L"&Table");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(data), L"&Data");
    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, kCmdInspector, L"Formula &Inspector\tCtrl+Shift+I");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");
    SetMenu(g.frame, menu);
}

LRESULT CALLBACK FrameProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        g.frame = window;
        const HINSTANCE instance = reinterpret_cast<const CREATESTRUCTW*>(lParam)->hInstance;
        g.grid = CreateWindowExW(0, kWindowClass, L"", WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL,
                                 0, 0, 0, 0, window, nullptr, instance, nullptr);
        g.status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0,
                                   window, nullptr, instance, nullptr);
        if (!g.grid || !g.status) {
            return -1;
        }
        SendMessageW(g.status, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
        CreateMenus(window);
        CreateFindBar(window, instance);
        CreateInspector(window, instance);
        UpdateTitle(g.grid);
        UpdateStatus();
        return 0;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            LayoutFrame();
        }
        return 0;
    case WM_SETFOCUS:
    case WM_ACTIVATE:
        if ((message == WM_SETFOCUS || LOWORD(wParam) != WA_INACTIVE) && !(g.findVisible && GetFocus() == g.findEdit) &&
            !(g.inspectorVisible && GetFocus() == g.inspector)) {
            SetFocus(g.grid);
        }
        return 0;
    case WM_COMMAND:
        if (lParam == 0) {
            SendMessageW(g.grid, WM_COMMAND, wParam, lParam);
        } else {
            HandleFindCommand(LOWORD(wParam), HIWORD(wParam));
        }
        return 0;
    case WM_INITMENUPOPUP:
        return SendMessageW(g.grid, message, wParam, lParam);
    case WM_CLOSE:
        if (ConfirmDiscard(g.grid)) {
            DestroyWindow(window);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}


void LayoutFrame() {
    if (!g.frame || !g.grid || !g.status) {
        return;
    }
    SendMessageW(g.status, WM_SIZE, 0, 0);
    RECT client{};
    GetClientRect(g.frame, &client);
    RECT statusRect{};
    GetWindowRect(g.status, &statusRect);
    const int statusHeight = statusRect.bottom - statusRect.top;
    const std::array<int, 4> edges{Scale(170), Scale(440), Scale(530), -1};
    SendMessageW(g.status, SB_SETPARTS, edges.size(), reinterpret_cast<LPARAM>(edges.data()));
    for (auto& part : g.statusParts) {
        part.clear();
    }
    UpdateStatus();

    const int paneWidth = std::min<int>(InspectorWidth(), std::max<int>(0, client.right - Scale(200)));
    const int barHeight = g.findVisible ? g.m.rowHeight + Scale(8) : 0;
    const int gridHeight = std::max<int>(0, client.bottom - statusHeight - barHeight);
    MoveWindow(g.grid, 0, 0, client.right - paneWidth, gridHeight, TRUE);
    if (g.inspector) {
        MoveWindow(g.inspector, client.right - paneWidth, 0, paneWidth, gridHeight, TRUE);
    }
    if (!g.findVisible) {
        return;
    }
    const int top = gridHeight + Scale(4);
    const int h = g.m.rowHeight;
    int x = Scale(6);
    const auto place = [&](int id, int width) {
        MoveWindow(GetDlgItem(g.frame, id), x, top, width, h, TRUE);
        x += width + Scale(6);
    };
    place(kFindLabelId, Scale(40));
    place(kFindEditId, Scale(220));
    place(kFindPrevId, Scale(56));
    place(kFindNextId, Scale(56));
    place(kFindCaseId, Scale(96));
    place(kFindWholeId, Scale(92));
    place(kFindRegexId, Scale(64));
    place(kFindColumnId, Scale(104));
    place(kFindResultId, Scale(110));
    place(kFindCloseId, Scale(60));
}

// Status bar parts: selection, size, state, formula (stretches).
void UpdateStatus() {
    if (!g.status) {
        return;
    }
    const CellRange sel = Selection();
    std::array<std::wstring, 4> parts;
    if (sel.top == sel.bottom && sel.left == sel.right) {
        parts[0] = CellName(g.active);
    } else {
        parts[0] = CellName({sel.top, sel.left}) + L":" + CellName({sel.bottom, sel.right}) + L"  (" +
                   std::to_wstring(sel.bottom - sel.top + 1) + L" \u00D7 " +
                   std::to_wstring(sel.right - sel.left + 1) + L")";
    }
    if (g.view.Active()) {
        const std::size_t headerRows = std::min(g.view.headerRows, g.view.Rows().size());
        parts[1] = L"Showing " + std::to_wstring(g.view.Rows().size() - headerRows) + L" of " +
                   std::to_wstring(g.doc.RowCount() - std::min(g.view.headerRows, g.doc.RowCount())) +
                   L" rows \u00D7 " + std::to_wstring(g.doc.ColumnCount()) + L" columns";
    } else {
        parts[1] = std::to_wstring(g.doc.RowCount()) + L" rows \u00D7 " + std::to_wstring(g.doc.ColumnCount()) +
                   L" columns";
    }
    parts[2] = IsDirty() ? L"Modified" : L"Saved";
    if (const std::string& formula = g.doc.Formula(DocRowOf(g.active.row), static_cast<std::size_t>(g.active.col));
        !formula.empty()) {
        parts[3] = L"Formula: " + Utf8ToWide(formula);
        if (const FormulaInspection* inspection = ActiveInspection(); inspection && !inspection->diagnostic.empty()) {
            parts[3] += L"  \u2014  " + Utf8ToWide(inspection->diagnostic);
        }
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (parts[i] != g.statusParts[i]) {
            g.statusParts[i] = parts[i];
            SendMessageW(g.status, SB_SETTEXTW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(parts[i].c_str()));
        }
    }
    UpdateInspector();
}

}
