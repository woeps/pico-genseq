#pragma once

#include "IView.h"
#include "../hardware/LedMatrix.h"

namespace ui {

// Instruments view (F2). Shows and edits the selected Instrument: its MIDI channel, mute, and
// which patterns it plays and whether each route is on.
//
// Matrix layout (16 x 16):
//   rows 0-4   label "cNN": the selected Instrument's MIDI channel (red while muted)
//   rows 5-13  pattern grid, 5 x 3 cells of 2 x 2 pixels (same geometry as the Patterns view);
//              each cell has one colour; the cursor turns the cell's top-left pixel white
//   row 15     Instrument strip, one pixel per slot
//
// Keys: arrows move the pattern cursor, Ctrl+Left/Right select the previous/next Instrument,
// Enter assigns or unassigns the pattern under the cursor, E switches its route on or off,
// M mutes or unmutes the Instrument, Shift+Left/Right change the MIDI channel, Ctrl+N creates an
// Instrument, Delete deletes the selected one.
class InstrumentsView : public IView {
public:
    InstrumentsView(hardware::LedMatrix& ledMatrix);

    void onEnter() override;
    void handleEvent(state::UIState& state, const events::Event& event) override;
    void render(const state::UIState& state) override;

    static constexpr uint8_t GRID_COLS = 5;
    static constexpr uint8_t GRID_ROWS = 3;
    static constexpr uint8_t SQUARE_SIZE = 2;
    static constexpr uint8_t GRID_GAP = 1;
    static constexpr uint8_t GRID_START_Y = 5;
    static constexpr uint8_t STRIP_ROW = 15;

    //                                                 0xXXGGRRBB
    static constexpr uint32_t COLOR_LABEL          = 0xFFFFEE00;  // yellow
    static constexpr uint32_t COLOR_LABEL_MUTED    = 0x0000FF00;  // red
    static constexpr uint32_t COLOR_SOUNDING       = 0x00FF0000;  // green: member, route on, pattern active
    static constexpr uint32_t COLOR_PATTERN_OFF    = 0x000000FF;  // blue: member, route on, pattern inactive
    static constexpr uint32_t COLOR_ROUTE_OFF      = 0x0000FF00;  // red: member, route off
    static constexpr uint32_t COLOR_NOT_MEMBER     = 0x00101010;  // faint grey
    static constexpr uint32_t COLOR_CURSOR         = 0x00FFFFFF;  // white
    static constexpr uint32_t COLOR_STRIP_OCCUPIED = 0x00000040;  // dim blue
    static constexpr uint32_t COLOR_STRIP_MUTED    = 0x00004000;  // dim red
    static constexpr uint32_t COLOR_STRIP_SELECTED = 0x00FFFFFF;  // white
    static constexpr uint32_t COLOR_NOTICE         = 0x0000FF00;  // red strip: refused action

private:
    hardware::LedMatrix& ledMatrix;
};

} // namespace ui
