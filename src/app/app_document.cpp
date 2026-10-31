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
 
// ---------------------------------------------------------------- document state

void UpdateTitle(HWND window) {
    (void)window;
    const std::wstring title = std::wstring(IsDirty() ? L"*" : L"") +
                               (g.path.empty() ? std::wstring(L"Untitled") : BaseName(g.path)) + L" - Sheetpad";
    SetWindowTextW(g.frame, title.c_str());
}

// Rebuilds the display-to-document row map and keeps the selection on the same document rows where possible.
// `anchorDoc` and `activeDoc` are the selection's document rows from before the view was changed.
void RebuildView(std::size_t anchorDoc, std::size_t activeDoc) {
    const WaitCursor wait;
    g.view.Rebuild(g.doc);
    g.viewStale = false;
    const auto remap = [&](int row, std::size_t docRow) {
        const auto display = g.view.DisplayRow(docRow, g.doc);
        return display ? static_cast<int>(*display) : std::min(row, GridRows() - 1);
    };
    g.anchor.row = remap(g.anchor.row, anchorDoc);
    g.active.row = remap(g.active.row, activeDoc);
}

// Brings the row map up to date after edits: undo/redo and row-count changes rebuild it, plain cell edits do not,
// so an edited row never jumps or disappears under the user.
void SyncView() {
    if (!g.view.Active()) {
        g.viewStale = false;
        return;
    }
    const bool rowsChanged = g.doc.RowCount() != g.view.BuiltRowCount();
    if (!g.viewStale && !rowsChanged) {
        return;
    }
    const std::size_t anchorDoc = DocRowOf(g.anchor.row);
    const std::size_t activeDoc = DocRowOf(g.active.row);
    if (g.viewStale && g.doc.ColumnCount() != g.view.BuiltColumnCount()) {
        g.view.filters.clear();
        g.view.sorts.clear();
    }
    RebuildView(anchorDoc, activeDoc);
}

void AfterDocumentChanged(HWND window) {
    SyncView();
    ClampSelection();
    UpdateScrollbars(window);
    UpdateTitle(window);
    Redraw(window);
}

void SetSelection(HWND window, Pos anchor, Pos active) {
    g.anchor = anchor;
    g.active = active;
    ClampSelection();
    EnsureVisible(window, g.active);
    Redraw(window);
}

void MoveActive(HWND window, int rowDelta, int colDelta, bool extend) {
    const Pos next{std::clamp(g.active.row + rowDelta, 0, GridRows() - 1),
                   std::clamp(g.active.col + colDelta, 0, GridCols() - 1)};
    SetSelection(window, extend ? g.anchor : next, next);
}


int Scale(int value) {
    return MulDiv(value, g.m.dpi, 96);
}

// Rows that hold data in the current view (all rows, or the filtered rows).
int DataRows() {
    return static_cast<int>(g.view.RowCount(g.doc));
}

int GridRows() {
    return DataRows() + 1;
}

int GridCols() {
    return static_cast<int>(g.doc.ColumnCount()) + 1;
}

std::size_t DocRowOf(int displayRow) {
    return g.view.DocRow(static_cast<std::size_t>(std::max(displayRow, 0)), g.doc);
}

bool IsDirty() {
    return g.doc.IsDirty() || g.viewDirty;
}

// ---

std::wstring ColumnName(int col) {
    std::wstring name;
    for (int n = col + 1; n > 0; n = (n - 1) / 26) {
        name.insert(name.begin(), static_cast<wchar_t>(L'A' + (n - 1) % 26));
    }
    return name;
}

std::wstring CellName(Pos p) {
    return ColumnName(p.col) + std::to_wstring(DocRowOf(p.row) + 1);
}

std::wstring BaseName(const std::wstring& path) {
    const std::size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

CellRange Selection() {
    return {std::min(g.anchor.row, g.active.row), std::min(g.anchor.col, g.active.col),
            std::max(g.anchor.row, g.active.row), std::max(g.anchor.col, g.active.col)};
}

void ClampSelection() {
    const auto clamp = [](Pos& p) {
        p.row = std::clamp(p.row, 0, GridRows() - 1);
        p.col = std::clamp(p.col, 0, GridCols() - 1);
    };
    clamp(g.anchor);
    clamp(g.active);
}

int FloorDiv(int value, int divisor) {
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

}
