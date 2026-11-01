#include "grid_window.h"

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "app.h"
#include "text_encoding.h"

using namespace sheetpad;

namespace app {

namespace {

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

// The checkbox list of distinct values. Clicks and Space toggle items; item 0 is "Select All".
class FilterListBox : public WindowHandle {
public:
    std::function<void(int)> onToggle;

protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override {
        switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            SetFocus(*this);
            const LRESULT hit = SendMessageW(*this, LB_ITEMFROMPOINT, 0, lParam);
            if (HIWORD(hit) == 0) {
                SendMessageW(*this, LB_SETCURSEL, LOWORD(hit), 0);
                onToggle(LOWORD(hit));
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wParam == VK_SPACE) {
                onToggle(static_cast<int>(SendMessageW(*this, LB_GETCURSEL, 0, 0)));
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
        return DefaultProc(message, wParam, lParam);
    }
};

// The modal-like popup under a column header: sort buttons, value checklist and a condition filter.
class FilterPopup : public WindowHandle {
public:
    explicit FilterPopup(App& app) : app_(app) {
        list.onToggle = [this](int index) { Toggle(index); };
    }

    HWND search = nullptr;
    HWND combo = nullptr;
    HWND operand = nullptr;
    FilterListBox list;
    std::vector<ValueCount> values;
    std::vector<std::wstring> labels;
    std::vector<std::wstring> lowerLabels;
    std::vector<char> checked;
    std::vector<int> visible;
    FilterResult result = FilterResult::Cancel;
    bool done = false;

    void Refresh() {
        std::wstring query = WindowText(search);
        CharLowerBuffW(query.data(), static_cast<DWORD>(query.size()));
        visible.clear();
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (query.empty() || lowerLabels[i].find(query) != std::wstring::npos) {
                visible.push_back(static_cast<int>(i));
            }
        }
        SendMessageW(list, LB_SETCOUNT, visible.size() + 1, 0);
        InvalidateRect(list, nullptr, FALSE);
    }

protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override {
        switch (message) {
        case WM_MEASUREITEM: {
            auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            item->itemHeight = static_cast<UINT>(app_.m.rowHeight);
            return TRUE;
        }
        case WM_DRAWITEM:
            DrawItem(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case DM_GETDEFID:
            return MAKELONG(IDOK, DC_HASDEFID);
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case kFilterSearchId:
                if (HIWORD(wParam) == EN_CHANGE) {
                    Refresh();
                }
                break;
            case IDOK:
                result = FilterResult::Ok;
                done = true;
                break;
            case IDCANCEL:
                done = true;
                break;
            case kFilterClearId:
                result = FilterResult::Clear;
                done = true;
                break;
            case kFilterSortAscId:
                result = KeyDown(VK_SHIFT) ? FilterResult::AddAsc : FilterResult::SortAsc;
                done = true;
                break;
            case kFilterSortDescId:
                result = KeyDown(VK_SHIFT) ? FilterResult::AddDesc : FilterResult::SortDesc;
                done = true;
                break;
            default:
                break;
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                done = true;
            }
            return 0;
        case WM_CLOSE:
            done = true;
            return 0;
        default:
            break;
        }
        return DefaultProc(message, wParam, lParam);
    }

private:
    // 0 none, 1 all, 2 some of the visible values are checked.
    int SelectAllState() const {
        std::size_t on = 0;
        for (const int index : visible) {
            on += checked[static_cast<std::size_t>(index)] ? 1 : 0;
        }
        if (visible.empty() || on == 0) {
            return 0;
        }
        return on == visible.size() ? 1 : 2;
    }

    void Toggle(int index) {
        if (index < 0 || index > static_cast<int>(visible.size())) {
            return;
        }
        if (index == 0) {
            const char value = SelectAllState() != 1 ? 1 : 0;
            for (const int v : visible) {
                checked[static_cast<std::size_t>(v)] = value;
            }
        } else {
            char& box = checked[static_cast<std::size_t>(visible[static_cast<std::size_t>(index) - 1])];
            box = box ? 0 : 1;
        }
        InvalidateRect(list, nullptr, FALSE);
    }

    void DrawItem(const DRAWITEMSTRUCT& item) {
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
            state = SelectAllState();
            label = L"(Select All)";
        } else {
            const auto v = static_cast<std::size_t>(visible[item.itemID - 1]);
            state = checked[v] ? 1 : 0;
            label = labels[v];
            count = std::to_wstring(values[v].count);
        }
        const int box = app_.Scale(14);
        RECT checkbox{row.left + app_.Scale(4), row.top + (row.bottom - row.top - box) / 2, 0, 0};
        checkbox.right = checkbox.left + box;
        checkbox.bottom = checkbox.top + box;
        UINT style = DFCS_BUTTONCHECK;
        if (state == 1) {
            style |= DFCS_CHECKED;
        } else if (state == 2) {
            style |= DFCS_BUTTON3STATE | DFCS_CHECKED;
        }
        DrawFrameControl(dc, &checkbox, DFC_BUTTON, style);

        RECT countRect{row.right - app_.Scale(56), row.top, row.right - app_.Scale(4), row.bottom};
        DrawTextW(dc, count.c_str(), static_cast<int>(count.size()), &countRect,
                  DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
        RECT labelRect{checkbox.right + app_.Scale(6), row.top, countRect.left, row.bottom};
        DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &labelRect,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
        if ((item.itemState & ODS_FOCUS) != 0) {
            DrawFocusRect(dc, &row);
        }
    }

    App& app_;
};

}  // namespace

void GridWindow::ShowFilterPopup(int col) {
    editor.Commit();
    static bool open = false;
    if (open || col < 0 || col >= static_cast<int>(app_.doc.ColumnCount())) {
        return;
    }
    if (col == filterClosedColumn_ && GetTickCount64() - filterClosedTick_ < 400) {
        return;
    }
    const auto column = static_cast<std::size_t>(col);
    FilterPopup popup(app_);
    popup.values = app_.view.DistinctValues(app_.doc, column, true);
    const std::vector<ValueCount> everything = app_.view.DistinctValues(app_.doc, column, false);
    const auto existing = app_.view.filters.find(column);
    const ColumnFilter* current = existing == app_.view.filters.end() ? nullptr : &existing->second;
    for (const ValueCount& value : popup.values) {
        std::wstring label = value.value.empty() ? L"(Blanks)" : Utf8ToWide(value.value);
        std::wstring lower = label;
        CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
        popup.labels.push_back(std::move(label));
        popup.lowerLabels.push_back(std::move(lower));
        bool on = true;
        if (current) {
            on = current->isList ? current->allowed.count(value.value) != 0
                                 : ConditionMatches(current->op, current->operand, value.value);
        }
        popup.checked.push_back(on ? 1 : 0);
    }

    const int margin = app_.Scale(8);
    const int gap = app_.Scale(6);
    const int rowH = app_.m.rowHeight;
    const int width = app_.Scale(300);
    const int inner = width - 2 * margin;
    const int listH = app_.Scale(200);
    const int height = margin + rowH + gap + rowH + gap + rowH + gap + listH + gap + rowH + gap + rowH + gap + rowH +
                       margin;

    const RECT button = FilterButtonRect(col);
    POINT origin{button.left, button.bottom};
    ClientToScreen(*this, &origin);
    constexpr DWORD style = WS_POPUP | WS_BORDER | WS_CLIPCHILDREN;
    constexpr DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT;
    RECT bounds{0, 0, width, height};
    AdjustWindowRectEx(&bounds, style, FALSE, exStyle);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST), &monitor);
    const int outerW = bounds.right - bounds.left;
    const int outerH = bounds.bottom - bounds.top;
    const int x = std::max<int>(monitor.rcWork.left, std::min<int>(origin.x, monitor.rcWork.right - outerW));
    const int y = std::max<int>(monitor.rcWork.top, std::min<int>(origin.y, monitor.rcWork.bottom - outerH));

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!popup.Create(exStyle, kFilterClass, L"", style, x, y, outerW, outerH, app_.frame, nullptr, instance)) {
        return;
    }
    open = true;
    const auto make = [&](const wchar_t* cls, const std::wstring& text, DWORD controlStyle, int cx, int cy, int cw,
                          int ch, int id, DWORD controlEx = 0) {
        HWND control = CreateWindowExW(controlEx, cls, text.c_str(), WS_CHILD | WS_VISIBLE | controlStyle, cx, cy, cw,
                                       ch, popup, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance,
                                       nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app_.font), TRUE);
        return control;
    };

    std::wstring title = L"Column " + ColumnName(col);
    if (app_.view.headerRows > 0 && !app_.doc.Cell(0, column).empty()) {
        title += L": " + Utf8ToWide(app_.doc.Cell(0, column));
    }
    int cy = margin;
    make(L"STATIC", title, SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, margin, cy, inner, rowH, -1);
    cy += rowH + gap;
    const int half = (inner - gap) / 2;
    make(L"BUTTON", L"Sort A to Z", BS_PUSHBUTTON | WS_TABSTOP, margin, cy, half, rowH, kFilterSortAscId);
    make(L"BUTTON", L"Sort Z to A", BS_PUSHBUTTON | WS_TABSTOP, margin + half + gap, cy, half, rowH,
         kFilterSortDescId);
    cy += rowH + gap;
    popup.search = make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, margin, cy, inner, rowH, kFilterSearchId,
                        WS_EX_CLIENTEDGE);
    SendMessageW(popup.search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search"));
    cy += rowH + gap;
    popup.list.Subclass(make(L"LISTBOX", L"",
                             LBS_OWNERDRAWFIXED | LBS_NODATA | LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP, margin, cy,
                             inner, listH, kFilterListId, WS_EX_CLIENTEDGE));
    cy += listH + gap;
    popup.combo = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, margin, cy, inner,
                       app_.Scale(220), kFilterComboId);
    SendMessageW(popup.combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), rowH - app_.Scale(6));
    SendMessageW(popup.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(no condition)"));
    for (const ConditionChoice& choice : kConditions) {
        SendMessageW(popup.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label));
    }
    cy += rowH + gap;
    popup.operand = make(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, margin, cy, inner, rowH, kFilterOperandId,
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
        SetWindowTextW(popup.operand, Utf8ToWide(current->operand).c_str());
    }
    SendMessageW(popup.combo, CB_SETCURSEL, static_cast<WPARAM>(comboSelection), 0);
    popup.Refresh();

    // The frame stays enabled so that clicking it deactivates the popup, which then closes (WM_ACTIVATE).
    ShowWindow(popup, SW_SHOW);
    SetFocus(popup.search);
    MSG message{};
    while (!popup.done && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessage(popup, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (!popup.done) {
        PostQuitMessage(static_cast<int>(message.wParam));
    }
    const int comboChoice = static_cast<int>(SendMessageW(popup.combo, CB_GETCURSEL, 0, 0));
    const std::string operandText = WideToUtf8(WindowText(popup.operand));
    DestroyWindow(popup);
    open = false;
    filterClosedTick_ = GetTickCount64();
    filterClosedColumn_ = col;
    SetFocus(*this);

    switch (popup.result) {
    case FilterResult::Ok: {
        std::optional<ColumnFilter> replacement;
        if (comboChoice > 0 && !operandText.empty()) {
            ColumnFilter filter;
            filter.isList = false;
            filter.op = kConditions[static_cast<std::size_t>(comboChoice) - 1].op;
            filter.operand = operandText;
            replacement = std::move(filter);
        } else if (std::find(popup.checked.begin(), popup.checked.end(), 0) != popup.checked.end()) {
            ColumnFilter filter;
            for (std::size_t i = 0; i < popup.values.size(); ++i) {
                if (popup.checked[i]) {
                    filter.allowed.insert(popup.values[i].value);
                }
            }
            // Values hidden by other columns' filters are not shown, so this filter leaves them alone.
            for (const ValueCount& value : everything) {
                const bool shown = std::any_of(popup.values.begin(), popup.values.end(),
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
        app_.ChangeView([&] {
            if (replacement) {
                app_.view.filters[column] = std::move(*replacement);
            } else {
                app_.view.filters.erase(column);
            }
            app_.viewDirty = true;
        });
        break;
    }
    case FilterResult::Clear:
        if (current) {
            app_.ChangeView([&] {
                app_.view.filters.erase(column);
                app_.viewDirty = true;
            });
        }
        break;
    case FilterResult::SortAsc: app_.SortByColumn(col, false, false); break;
    case FilterResult::SortDesc: app_.SortByColumn(col, true, false); break;
    case FilterResult::AddAsc: app_.SortByColumn(col, false, true); break;
    case FilterResult::AddDesc: app_.SortByColumn(col, true, true); break;
    case FilterResult::Cancel: break;
    }
}

}  // namespace app
