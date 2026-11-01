#include <algorithm>
#include <string>
#include <vector>

#include "app.h"

using namespace sheetpad;

namespace app {

// ---------------------------------------------------------------- sort and filter

void App::SortByColumn(int col, bool descending, bool add) {
    if (col < 0 || col >= static_cast<int>(doc.ColumnCount())) {
        return;
    }
    ChangeView([&] {
        if (!add) {
            view.sorts.clear();
        }
        std::erase_if(view.sorts, [&](const SortLevel& level) { return level.column == static_cast<std::size_t>(col); });
        view.sorts.push_back({static_cast<std::size_t>(col), descending});
    });
}

void App::ApplySortToData() {
    if (view.sorts.empty()) {
        return;
    }
    ChangeView([&] {
        view.Rebuild(doc);
        const std::vector<std::size_t> source = view.SortedPermutation(doc);
        doc.PermuteRows(source);
        view.sorts.clear();
    });
}

// ---------------------------------------------------------------- rows and columns

void App::RunStructuralCommand(UINT command) {
    const CellRange sel = Selection();
    const std::size_t rows = static_cast<std::size_t>(sel.bottom - sel.top + 1);
    const std::size_t columns = static_cast<std::size_t>(sel.right - sel.left + 1);
    const int dataRows = DataRows();
    const int dataCols = static_cast<int>(doc.ColumnCount());

    switch (command) {
    case kCmdInsertRowAbove: {
        const std::size_t docAt = DocRowOf(sel.top);
        if (doc.InsertRows(docAt, rows)) {
            if (view.Active()) {
                view.RowsInserted(docAt, rows, static_cast<std::size_t>(sel.top));
            }
            AfterDocumentChanged();
        }
        break;
    }
    case kCmdInsertRowBelow: {
        const int at = std::min(sel.bottom + 1, dataRows);
        const std::size_t docAt = DocRowOf(at);
        if (doc.InsertRows(docAt, rows)) {
            if (view.Active()) {
                view.RowsInserted(docAt, rows, static_cast<std::size_t>(at));
            }
            AfterDocumentChanged();
            SetSelection({at, sel.left}, {at + static_cast<int>(rows) - 1, sel.right});
        }
        break;
    }
    case kCmdDeleteRows: {
        std::vector<std::size_t> docRows;
        for (int row = sel.top; row <= std::min(sel.bottom, dataRows - 1); ++row) {
            docRows.push_back(DocRowOf(row));
        }
        std::sort(docRows.begin(), docRows.end());
        docRows.erase(std::unique(docRows.begin(), docRows.end()), docRows.end());
        if (doc.DeleteRowSet(docRows)) {
            if (view.Active()) {
                view.RowsDeleted(docRows);
            }
            AfterDocumentChanged();
            const int row = std::min(sel.top, DataRows() - 1);
            SetSelection({row, sel.left}, {row, sel.left});
        }
        break;
    }
    case kCmdDuplicateRow: {
        const std::size_t docRow = DocRowOf(active.row);
        if (doc.DuplicateRow(docRow)) {
            if (view.Active()) {
                view.RowsInserted(docRow + 1, 1, static_cast<std::size_t>(active.row) + 1);
            }
            AfterDocumentChanged();
            SetSelection({active.row + 1, active.col}, {active.row + 1, active.col});
        }
        break;
    }
    case kCmdInsertColumnLeft:
        if (doc.InsertColumns(static_cast<std::size_t>(sel.left), columns)) {
            view.ColumnsInserted(static_cast<std::size_t>(sel.left), columns);
            AfterDocumentChanged();
        }
        break;
    case kCmdInsertColumnRight: {
        const int at = std::min(sel.right + 1, dataCols);
        if (doc.InsertColumns(static_cast<std::size_t>(at), columns)) {
            view.ColumnsInserted(static_cast<std::size_t>(at), columns);
            AfterDocumentChanged();
            SetSelection({sel.top, at}, {sel.bottom, at + static_cast<int>(columns) - 1});
        }
        break;
    }
    case kCmdDeleteColumns:
        if (doc.DeleteColumns(static_cast<std::size_t>(sel.left), columns)) {
            view.ColumnsDeleted(static_cast<std::size_t>(sel.left), columns);
            AfterDocumentChanged();
            const int col = std::min(sel.left, static_cast<int>(doc.ColumnCount()) - 1);
            SetSelection({sel.top, col}, {sel.top, col});
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- commands

void App::RunCommand(UINT command) {
    CellEditor& editor = grid.editor;
    if (editor.Editing()) {
        switch (command) {
        case kCmdUndo: SendMessageW(editor, EM_UNDO, 0, 0); return;
        case kCmdCut: SendMessageW(editor, WM_CUT, 0, 0); return;
        case kCmdCopy: SendMessageW(editor, WM_COPY, 0, 0); return;
        case kCmdPaste: SendMessageW(editor, WM_PASTE, 0, 0); return;
        case kCmdDelete: SendMessageW(editor, WM_CLEAR, 0, 0); return;
        case kCmdSelectAll: SendMessageW(editor, EM_SETSEL, 0, -1); return;
        case kCmdRedo: return;
        case kCmdInsertRowAbove:
        case kCmdInsertRowBelow:
        case kCmdDeleteRows:
        case kCmdDuplicateRow:
        case kCmdInsertColumnLeft:
        case kCmdInsertColumnRight:
        case kCmdDeleteColumns:
        case kCmdFillDown:
        case kCmdFillRight: return;
        default: editor.Commit(); break;
        }
    }

    switch (command) {
    case kCmdFillDown: FillSelection(true); return;
    case kCmdFillRight: FillSelection(false); return;
    case kCmdInsertRowAbove:
    case kCmdInsertRowBelow:
    case kCmdDeleteRows:
    case kCmdDuplicateRow:
    case kCmdInsertColumnLeft:
    case kCmdInsertColumnRight:
    case kCmdDeleteColumns: RunStructuralCommand(command); return;
    default: break;
    }
    switch (command) {
    case kCmdNew: NewDocument(); break;
    case kCmdOpen: OpenDocument(); break;
    case kCmdSave: Save(); break;
    case kCmdSaveAs: SaveAs(); break;
    case kCmdExit: SendMessageW(frame, WM_CLOSE, 0, 0); break;
    case kCmdUndo:
        if (doc.Undo()) {
            viewStale = true;
            AfterDocumentChanged();
        }
        break;
    case kCmdRedo:
        if (doc.Redo()) {
            viewStale = true;
            AfterDocumentChanged();
        }
        break;
    case kCmdCut:
        if (CopySelection(true)) {
            ClearSelection();
        }
        break;
    case kCmdCopy: CopySelection(); break;
    case kCmdPaste: PasteClipboard(); break;
    case kCmdDelete: ClearSelection(); break;
    case kCmdSelectAll: SetSelection({0, 0}, {DataRows() - 1, static_cast<int>(doc.ColumnCount()) - 1}); break;
    case kCmdFind: findBar.Show(true); break;
    case kCmdFindNext:
    case kCmdFindPrev:
        if (!findBar.Visible()) {
            findBar.Show(true);
        } else {
            findBar.FindNext(command == kCmdFindNext, false);
        }
        break;
    case kCmdFindClose: findBar.Show(false); break;
    case kCmdHeaderRow:
        ChangeView([&] {
            view.headerRows = view.headerRows ? 0 : 1;
            viewDirty = true;
        });
        break;
    case kCmdFilterColumn: grid.ShowFilterPopup(active.col); break;
    case kCmdClearFilters:
        if (!view.filters.empty()) {
            ChangeView([&] {
                view.filters.clear();
                viewDirty = true;
            });
        }
        break;
    case kCmdSortAsc: SortByColumn(active.col, false, false); break;
    case kCmdSortDesc: SortByColumn(active.col, true, false); break;
    case kCmdSortAddAsc: SortByColumn(active.col, false, true); break;
    case kCmdSortAddDesc: SortByColumn(active.col, true, true); break;
    case kCmdClearSort:
        if (!view.sorts.empty()) {
            ChangeView([&] { view.sorts.clear(); });
        }
        break;
    case kCmdApplySort: ApplySortToData(); break;
    case kCmdReapply: ChangeView([] {}); break;
    case kCmdInspector: inspector.Show(!inspector.Visible()); break;
    default: break;
    }
}

}  // namespace app
