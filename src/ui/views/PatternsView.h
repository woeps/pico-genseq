#pragma once

#include "IView.h"
#include "../hardware/LedMatrix.h"

namespace ui {

class PatternsView : public IView {
public:
    PatternsView(hardware::LedMatrix& ledMatrix);

    void onEnter() override;
    state::UIState handleEvent(const state::UIState& state, const events::Event& event) override;
    void render(const state::UIState& state) override;

private:
    hardware::LedMatrix& ledMatrix;

    static constexpr uint8_t GRID_COLS = 5;
    static constexpr uint8_t GRID_ROWS = 3;
    static constexpr uint8_t SQUARE_SIZE = 2;
    static constexpr uint8_t GRID_GAP = 1;
    static constexpr uint8_t GRID_START_X = 0;
    static constexpr uint8_t GRID_START_Y = 5;
    static constexpr uint8_t MAX_PATTERNS = GRID_COLS * GRID_ROWS;
//                                             0xXXGGRRBB
    static constexpr uint32_t COLOR_ACTIVE   = 0x0000FF00;
    static constexpr uint32_t COLOR_INACTIVE = 0x000000FF;
    static constexpr uint32_t COLOR_SELECTED = 0x00FFFFFF;
    static constexpr uint32_t COLOR_NEW_SLOT = 0x00FFFF00;
};

} // namespace ui
