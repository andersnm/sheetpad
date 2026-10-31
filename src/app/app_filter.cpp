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

struct ConditionChoice {
    FilterOp op;
    const wchar_t* label;
};

constexpr std::array<ConditionChoice, 10> kConditions{{
    {FilterOp::Equals, L"equals"},
    {FilterOp::NotEquals, L"does not equal"},
    {FilterOp::Greater, L"is greater than"},
    {FilterOp::GreaterEqual, L"is greater than or equal to"},
    {FilterOp::Less, L"is less than"},
    {FilterOp::LessEqual, L"is less than or equal to"},
    {FilterOp::Contains, L"contains"},
    {FilterOp::NotContains, L"does not contain"},
    {FilterOp::BeginsWith, L"begins with"},
    {FilterOp::EndsWith, L"ends with"},
}};

constexpr int kFilterSearchId = 4001;
constexpr int kFilterListId = 4002;
constexpr int kFilterComboId = 4003;
constexpr int kFilterOperandId = 4004;
constexpr int kFilterClearId = 4005;
constexpr int kFilterSortAscId = 4006;
constexpr int kFilterSortDescId = 4007;

enum class FilterResult { Cancel, Ok, Clear, SortAsc, SortDesc, AddAsc, AddDesc };

struct FilterDialog {
    HWND window = nullptr;
    HWND search = nullptr;
    HWND list = nullptr;
    HWND combo = nullptr;
    HWND operand = nullptr;
    std::vector<ValueCount> values;
    std::vector<std::wstring> labels;
    std::vector<std::wstring> lowerLabels;
    std::vector<char> checked;
    std::vector<int> visible;
    FilterResult result = FilterResult::Cancel;
    bool done = false;
    WNDPROC listDefaultProc = nullptr;
};

FilterDialog* filterDialog = nullptr;

std::wstring WindowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

int SelectAllState(const FilterDialog& d) {
    std::size_t on = 0;
    for (const int index : d.visible) {
        on += d.checked[static_cast<std::size_t>(index)] ? 1 : 0;
    }
    if (d.visible.empty() || on == 0) {
        return 0;
    }
    return on == d.visible.size() ? 1 : 2;
}

void RefreshFilterList(FilterDialog& d) {
    std::wstring query = WindowText(d.search);
    CharLowerBuffW(query.data(), static_cast<DWORD>(query.size()));
    d.visible.clear();
    for (std::size_t i = 0; i < d.values.size(); ++i) {
        if (query.empty() || d.lowerLabels[i].find(query) != std::wstring::npos) {
            d.visible.push_back(static_cast<int>(i));
        }
    }
    SendMessageW(d.list, LB_SETCOUNT, d.visible.size() + 1, 0);
    InvalidateRect(d.list, nullptr, FALSE);
}

void ToggleFilterItem(FilterDialog& d, int index) {
    if (index < 0 || index > static_cast<int>(d.visible.size())) {
        return;
    }
    if (index == 0) {
        const char value = SelectAllState(d) != 1 ? 1 : 0;
        for (const int v : d.visible) {
            d.checked[static_cast<std::size_t>(v)] = value;
        }
    } else {
        char& box = d.checked[static_cast<std::size_t>(d.visible[static_cast<std::size_t>(index) - 1])];
        box = box ? 0 : 1;
    }
    InvalidateRect(d.list, nullptr, FALSE);
}

void DrawFilterItem(const DRAWITEMSTRUCT& item) {
    const FilterDialog& d = *filterDialog;
    if (static_cast<int>(item.itemID) < 0) {
        return;
    }
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    HDC dc = item.hDC;
    RECT row = item.rcItem;
    FillRect(dc, &row, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));

    int state = 0;
    std::wstring label;
    std::wstring count;
    if (item.itemID == 0) {
        state = SelectAllState(d);
        label = L"(Select All)";
    } else {
        const auto v = static_cast<std::size_t>(d.visible[item.itemID - 1]);
        state = d.checked[v] ? 1 : 0;
        label = d.labels[v];
        count = std::to_wstring(d.values[v].count);
    }
    const int box = Scale(14);
    RECT checkbox{row.left + Scale(4), row.top + (row.bottom - row.top - box) / 2, 0, 0};
    checkbox.right = checkbox.left + box;
    checkbox.bottom = checkbox.top + box;
    UINT style = DFCS_BUTTONCHECK;
    if (state == 1) {
        style |= DFCS_CHECKED;
    } else if (state == 2) {
        style |= DFCS_BUTTON3STATE | DFCS_CHECKED;
    }
    DrawFrameControl(dc, &checkbox, DFC_BUTTON, style);

    RECT countRect{row.right - Scale(56), row.top, row.right - Scale(4), row.bottom};
    DrawTextW(dc, count.c_str(), static_cast<int>(count.size()), &countRect,
              DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    RECT labelRect{checkbox.right + Scale(6), row.top, countRect.left, row.bottom};
    DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &labelRect,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
    if ((item.itemState & ODS_FOCUS) != 0) {
        DrawFocusRect(dc, &row);
    }
}

LRESULT CALLBACK FilterListProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    FilterDialog& d = *filterDialog;
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        SetFocus(window);
        const LRESULT hit = SendMessageW(window, LB_ITEMFROMPOINT, 0, lParam);
        if (HIWORD(hit) == 0) {
            SendMessageW(window, LB_SETCURSEL, LOWORD(hit), 0);
            ToggleFilterItem(d, LOWORD(hit));
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_SPACE) {
            ToggleFilterItem(d, static_cast<int>(SendMessageW(window, LB_GETCURSEL, 0, 0)));
            return 0;
        }
        break;
    case WM_CHAR:
        if (wParam == VK_SPACE) {
            return 0;
        }
        break;
    default:
        break;
    }
    return CallWindowProcW(d.listDefaultProc, window, message, wParam, lParam);
}

LRESULT CALLBACK FilterProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    FilterDialog* d = filterDialog;
    switch (message) {
    case WM_MEASUREITEM: {
        auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        item->itemHeight = static_cast<UINT>(g.m.rowHeight);
        return TRUE;
    }
    case WM_DRAWITEM:
        if (d) {
            DrawFilterItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
        }
        return TRUE;
    case DM_GETDEFID:
        return MAKELONG(IDOK, DC_HASDEFID);
    case WM_COMMAND:
        if (!d) {
            return 0;
        }
        switch (LOWORD(wParam)) {
        case kFilterSearchId:
            if (HIWORD(wParam) == EN_CHANGE) {
                RefreshFilterList(*d);
            }
            break;
        case IDOK:
            d->result = FilterResult::Ok;
            d->done = true;
            break;
        case IDCANCEL:
            d->done = true;
            break;
        case kFilterClearId:
            d->result = FilterResult::Clear;
            d->done = true;
            break;
        case kFilterSortAscId:
            d->result = KeyDown(VK_SHIFT) ? FilterResult::AddAsc : FilterResult::SortAsc;
            d->done = true;
            break;
        case kFilterSortDescId:
            d->result = KeyDown(VK_SHIFT) ? FilterResult::AddDesc : FilterResult::SortDesc;
            d->done = true;
            break;
        default:
            break;
        }
        return 0;
    case WM_ACTIVATE:
        if (d && LOWORD(wParam) == WA_INACTIVE) {
            d->done = true;
        }
        return 0;
    case WM_CLOSE:
        if (d) {
            d->done = true;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// Clicking a column's dropdown button while its popup is open closes the popup; without this the same click
// would reopen it immediately.
struct FilterClose {
    ULONGLONG tick = 0;
    int column = -1;
};
FilterClose lastFilterClose;

void ShowFilterPopup(HWND window, int col) {
    CommitEdit(window);
    if (filterDialog || col < 0 || col >= static_cast<int>(g.doc.ColumnCount())) {
        return;
    }
    if (col == lastFilterClose.column && GetTickCount64() - lastFilterClose.tick < 400) {
        return;
    }
    const auto column = static_cast<std::size_t>(col);
    FilterDialog dialog;
    dialog.values = g.view.DistinctValues(g.doc, column, true);
    const std::vector<ValueCount> everything = g.view.DistinctValues(g.doc, column, false);
    const auto existing = g.view.filters.find(column);
    const ColumnFilter* current = existing == g.view.filters.end() ? nullptr : &existing->second;
    for (const ValueCount& value : dialog.values) {
        std::wstring label = value.value.empty() ? L"(Blanks)" : Utf8ToWide(value.value);
        std::wstring lower = label;
        CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
        dialog.labels.push_back(std::move(label));
        dialog.lowerLabels.push_back(std::move(lower));
        bool on = true;
        if (current) {
            on = current->isList ? current->allowed.count(value.value) != 0
                                 : ConditionMatches(current->op, current->operand, value.value);
        }
        dialog.checked.push_back(on ? 1 : 0);
    }

    const int margin = Scale(8);
    const int gap = Scale(6);
    const int rowH = g.m.rowHeight;
    const int width = Scale(300);
    const int inner = width - 2 * margin;
    const int listH = Scale(200);
    const int height = margin + rowH + gap + rowH + gap + rowH + gap + listH + gap + rowH + gap + rowH + gap + rowH +
                       margin;

    const RECT button = FilterButtonRect(col);
    POINT origin{button.left, button.bottom};
    ClientToScreen(window, &origin);
    constexpr DWORD style = WS_POPUP | WS_BORDER | WS_CLIPCHILDREN;
    constexpr DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT;
    RECT frame{0, 0, width, height};
    AdjustWindowRectEx(&frame, style, FALSE, exStyle);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST), &monitor);
    const int outerW = frame.right - frame.left;
    const int outerH = frame.bottom - frame.top;
    const int x = std::max<int>(monitor.rcWork.left, std::min<int>(origin.x, monitor.rcWork.right - outerW));
    const int y = std::max<int>(monitor.rcWork.top, std::min<int>(origin.y, monitor.rcWork.bottom - outerH));

    filterDialog = &dialog;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    dialog.window = CreateWindowExW(exStyle, kFilterClass, L"", style, x, y, outerW, outerH, g.frame, nullptr,
                                    instance, nullptr);
    if (!dialog.window) {
        filterDialog = nullptr;
        return;
    }
    const auto make = [&](const wchar_t* cls, const std::wstring& text, DWORD controlStyle, int cx, int cy, int cw,
                          int ch, int id, DWORD controlEx = 0) {
        HWND control = CreateWindowExW(controlEx, cls, text.c_str(), WS_CHILD | WS_VISIBLE | controlStyle, cx, cy, cw,
                                       ch, dialog.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                       instance, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
        return control;
    };

    std::wstring title = L"Column " + ColumnName(col);
    if (g.view.headerRows > 0 && !g.doc.Cell(0, column).empty()) {
        title += L": " + Utf8ToWide(g.doc.Cell(0, column));
    }
    int cy = margin;
    make(L"STATIC", title, SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, margin, cy, inner, rowH, -1);
    cy += rowH + gap;
    const int half = (inner - gap) / 2;
    make(L"BUTTON", L"Sort A to Z", BS_PUSHBUTTON | WS_TABSTOP, margin, cy, half, rowH, kFilterSortAscId);
    make(L"BUTTON", L"Sort Z to A", BS_PUSHBUTTON | WS_TABSTOP, margin + half + gap, cy, half, rowH,
         kFilterSortDescId);
    cy += rowH + gap;
    dialog.search = make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, margin, cy, inner, rowH, kFilterSearchId,
                         WS_EX_CLIENTEDGE);
    SendMessageW(dialog.search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search"));
    cy += rowH + gap;
    dialog.list = make(L"LISTBOX", L"", LBS_OWNERDRAWFIXED | LBS_NODATA | LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP,
                       margin, cy, inner, listH, kFilterListId, WS_EX_CLIENTEDGE);
    dialog.listDefaultProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(dialog.list, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(FilterListProc)));
    cy += listH + gap;
    dialog.combo = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, margin, cy, inner, Scale(220),
                        kFilterComboId);
    SendMessageW(dialog.combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), rowH - Scale(6));
    SendMessageW(dialog.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(no condition)"));
    for (const ConditionChoice& choice : kConditions) {
        SendMessageW(dialog.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label));
    }
    cy += rowH + gap;
    dialog.operand = make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, margin, cy, inner, rowH, kFilterOperandId,
                          WS_EX_CLIENTEDGE);
    cy += rowH + gap;
    const int third = (inner - 2 * gap) / 3;
    make(L"BUTTON", L"Clear Filter", BS_PUSHBUTTON | WS_TABSTOP, margin, cy, third, rowH, kFilterClearId);
    make(L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, margin + third + gap, cy, third, rowH, IDOK);
    make(L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, margin + 2 * (third + gap), cy, third, rowH, IDCANCEL);

    int comboSelection = 0;
    if (current && !current->isList) {
        for (std::size_t i = 0; i < kConditions.size(); ++i) {
            if (kConditions[i].op == current->op) {
                comboSelection = static_cast<int>(i) + 1;
            }
        }
        SetWindowTextW(dialog.operand, Utf8ToWide(current->operand).c_str());
    }
    SendMessageW(dialog.combo, CB_SETCURSEL, static_cast<WPARAM>(comboSelection), 0);
    RefreshFilterList(dialog);

    // The frame stays enabled so that clicking it deactivates the popup, which then closes (WM_ACTIVATE).
    ShowWindow(dialog.window, SW_SHOW);
    SetFocus(dialog.search);
    MSG message{};
    while (!dialog.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessage(dialog.window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (!dialog.done) {
        PostQuitMessage(static_cast<int>(message.wParam));
    }
    const int comboChoice = static_cast<int>(SendMessageW(dialog.combo, CB_GETCURSEL, 0, 0));
    const std::string operandText = WideToUtf8(WindowText(dialog.operand));
    DestroyWindow(dialog.window);
    filterDialog = nullptr;
    lastFilterClose = {GetTickCount64(), col};
    SetFocus(window);

    switch (dialog.result) {
    case FilterResult::Ok: {
        std::optional<ColumnFilter> replacement;
        if (comboChoice > 0 && !operandText.empty()) {
            ColumnFilter filter;
            filter.isList = false;
            filter.op = kConditions[static_cast<std::size_t>(comboChoice) - 1].op;
            filter.operand = operandText;
            replacement = std::move(filter);
        } else if (std::find(dialog.checked.begin(), dialog.checked.end(), 0) != dialog.checked.end()) {
            ColumnFilter filter;
            for (std::size_t i = 0; i < dialog.values.size(); ++i) {
                if (dialog.checked[i]) {
                    filter.allowed.insert(dialog.values[i].value);
                }
            }
            // Values hidden by other columns' filters are not shown, so this filter leaves them alone.
            for (const ValueCount& value : everything) {
                const bool shown = std::any_of(dialog.values.begin(), dialog.values.end(),
                                               [&](const ValueCount& v) { return v.value == value.value; });
                if (!shown) {
                    filter.allowed.insert(value.value);
                }
            }
            replacement = std::move(filter);
        }
        if (!replacement && !current) {
            break;
        }
        ChangeView(window, [&] {
            if (replacement) {
                g.view.filters[column] = std::move(*replacement);
            } else {
                g.view.filters.erase(column);
            }
            g.viewDirty = true;
        });
        break;
    }
    case FilterResult::Clear:
        if (current) {
            ChangeView(window, [&] {
                g.view.filters.erase(column);
                g.viewDirty = true;
            });
        }
        break;
    case FilterResult::SortAsc: SortByColumn(window, col, false, false); break;
    case FilterResult::SortDesc: SortByColumn(window, col, true, false); break;
    case FilterResult::AddAsc: SortByColumn(window, col, false, true); break;
    case FilterResult::AddDesc: SortByColumn(window, col, true, true); break;
    case FilterResult::Cancel: break;
    }
}

}
