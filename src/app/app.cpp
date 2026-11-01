#include "app.h"

#include <algorithm>
#include <cwchar>
#include <string>

using namespace sheetpad;

namespace app {

// ---------------------------------------------------------------- document state

App::App() : frame(*this), grid(*this), findBar(*this), inspector(*this) {}

void App::CreateFonts(HWND window) {
    HDC dc = GetDC(window);
    m.dpi = GetDeviceCaps(dc, LOGPIXELSY);

    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    LOGFONTW logFont{};
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        logFont = metrics.lfMessageFont;
    } else {
        logFont.lfHeight = -MulDiv(9, m.dpi, 72);
        wcscpy(logFont.lfFaceName, L"Segoe UI");
    }
    font = CreateFontIndirectW(&logFont);

    HGDIOBJ old = SelectObject(dc, font);
    TEXTMETRICW text{};
    GetTextMetricsW(dc, &text);
    SelectObject(dc, old);
    ReleaseDC(window, dc);

    m.textHeight = text.tmHeight;
    m.padding = Scale(6);
    m.rowHeight = text.tmHeight + Scale(10);
    m.headerHeight = m.rowHeight;
    m.rowNumberWidth = Scale(56);
    m.columnWidth = Scale(140);
}

void App::UpdateTitle() {
    const std::wstring title =
        std::wstring(IsDirty() ? L"*" : L"") + (path.empty() ? std::wstring(L"Untitled") : BaseName(path)) +
        L" - Sheetpad";
    SetWindowTextW(frame, title.c_str());
}

// Rebuilds the display-to-document row map and keeps the selection on the same document rows where possible.
// `anchorDoc` and `activeDoc` are the selection's document rows from before the view was changed.
void App::RebuildView(std::size_t anchorDoc, std::size_t activeDoc) {
    const WaitCursor wait;
    view.Rebuild(doc);
    viewStale = false;
    const auto remap = [&](int row, std::size_t docRow) {
        const auto display = view.DisplayRow(docRow, doc);
        return display ? static_cast<int>(*display) : std::min(row, GridRows() - 1);
    };
    anchor.row = remap(anchor.row, anchorDoc);
    active.row = remap(active.row, activeDoc);
}

// Brings the row map up to date after edits: undo/redo and row-count changes rebuild it, plain cell edits do not,
// so an edited row never jumps or disappears under the user.
void App::SyncView() {
    if (!view.Active()) {
        viewStale = false;
        return;
    }
    const bool rowsChanged = doc.RowCount() != view.BuiltRowCount();
    if (!viewStale && !rowsChanged) {
        return;
    }
    const std::size_t anchorDoc = DocRowOf(anchor.row);
    const std::size_t activeDoc = DocRowOf(active.row);
    if (viewStale && doc.ColumnCount() != view.BuiltColumnCount()) {
        view.filters.clear();
        view.sorts.clear();
    }
    RebuildView(anchorDoc, activeDoc);
}

void App::AfterDocumentChanged() {
    SyncView();
    ClampSelection();
    grid.UpdateScrollbars();
    UpdateTitle();
    grid.Redraw();
}

void App::SetSelection(Pos newAnchor, Pos newActive) {
    anchor = newAnchor;
    active = newActive;
    ClampSelection();
    grid.EnsureVisible(active);
    grid.Redraw();
}

void App::MoveActive(int rowDelta, int colDelta, bool extend) {
    const Pos next{std::clamp(active.row + rowDelta, 0, GridRows() - 1),
                   std::clamp(active.col + colDelta, 0, GridCols() - 1)};
    SetSelection(extend ? anchor : next, next);
}

int App::Scale(int value) const {
    return MulDiv(value, m.dpi, 96);
}

int App::DataRows() const {
    return static_cast<int>(view.RowCount(doc));
}

int App::GridRows() const {
    return DataRows() + 1;
}

int App::GridCols() const {
    return static_cast<int>(doc.ColumnCount()) + 1;
}

std::size_t App::DocRowOf(int displayRow) const {
    return view.DocRow(static_cast<std::size_t>(std::max(displayRow, 0)), doc);
}

bool App::IsDirty() const {
    return doc.IsDirty() || viewDirty;
}

std::wstring App::CellName(Pos p) const {
    return ColumnName(p.col) + std::to_wstring(DocRowOf(p.row) + 1);
}

CellRange App::Selection() const {
    return {std::min(anchor.row, active.row), std::min(anchor.col, active.col), std::max(anchor.row, active.row),
            std::max(anchor.col, active.col)};
}

void App::ClampSelection() {
    const auto clamp = [&](Pos& p) {
        p.row = std::clamp(p.row, 0, GridRows() - 1);
        p.col = std::clamp(p.col, 0, GridCols() - 1);
    };
    clamp(anchor);
    clamp(active);
}

bool App::TextControlFocused() const {
    const HWND focus = GetFocus();
    return (findBar.Visible() && focus == findBar.Edit()) || (inspector.Visible() && focus == inspector);
}

// ---------------------------------------------------------------- helpers

std::wstring ColumnName(int col) {
    std::wstring name;
    for (int n = col + 1; n > 0; n = (n - 1) / 26) {
        name.insert(name.begin(), static_cast<wchar_t>(L'A' + (n - 1) % 26));
    }
    return name;
}

std::wstring BaseName(const std::wstring& path) {
    const std::size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? path : path.substr(separator + 1);
}

int FloorDiv(int value, int divisor) {
    return value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
}

}  // namespace app
