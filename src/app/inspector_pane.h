#pragma once

#include <cstdint>
#include <string>

#include "formula.h"
#include "types.h"
#include "window_handle.h"

namespace app {

// The read-only formula inspector EDIT control, a child of the frame.
class InspectorPane : public WindowHandle {
public:
    explicit InspectorPane(App& app) : app_(app) {}

    void Create(HWND frame, HINSTANCE instance);
    bool Visible() const { return visible_; }
    int Width() const;
    void Update();
    void Show(bool show);
    // Null when the active cell is not a formula.
    const sheetpad::FormulaInspection* ActiveInspection();

private:
    std::wstring Describe();

    // Inspecting a range scans it, so the result is reused until the document or the selection changes.
    struct Cache {
        bool valid = false;
        std::uint64_t revision = 0;
        std::size_t row = 0;
        std::size_t col = 0;
        std::string formula;
        sheetpad::FormulaInspection inspection;
    };

    App& app_;
    bool visible_ = false;
    Cache cache_;
};

}  // namespace app
