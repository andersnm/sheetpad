#pragma once

#include <string>
#include <vector>

#include "hint_window.h"
#include "autocomplete.h"
#include "types.h"
#include "window_handle.h"

namespace app {

// The in-cell EDIT control, a child of the grid.
class CellEditor : public WindowHandle {
public:
    explicit CellEditor(App& app) : app_(app), completionList_(app, true), signatureTip_(app, false) {}

    void Create(HWND grid);
    bool Editing() const { return editing_; }
    // Starts editing the active cell. Null `initialText` starts from the cell's current content.
    void Begin(const std::wstring* initialText);
    void Commit();
    void Cancel();
    void Position();
    void HandleKey(WPARAM key, bool shift);

protected:
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
    void End();

    // Formula Autocomplete.
    std::string FormulaText(std::size_t& caret) const;
    POINT ScreenPointAt(std::size_t charIndex, bool bottom) const;
    void OpenCompletion();
    void RefreshCompletion();
    void AcceptCompletion(std::size_t index);
    void CloseCompletion();
    void UpdateSignatureTip();
    void HideHints();
    void AfterEdit();

    App& app_;
    bool editing_ = false;
    Pos cell_;
    std::string original_;
    HintWindow completionList_;
    HintWindow signatureTip_;
    std::vector<const sheetpad::FunctionInfo*> candidates_;
    bool completionOpen_ = false;
    std::size_t tokenStart_ = 0;
    std::size_t tokenLength_ = 0;
};

}  // namespace app
