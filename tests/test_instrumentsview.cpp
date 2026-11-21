// Instruments task 8.4 -- Instruments view rendering and keys, plus the Patterns view markers.
#include "framework.h"
#include "helpers.h"
#include "stubs/command_stub.h"
#include "stubs/hardware_stub.h"
#include "ui/views/InstrumentsView.h"
#include "ui/views/PatternsView.h"
#include "ui/state/Instruments.h"
#include "ui/state/UIState.h"
#include "ui/Event.h"

#include <cstring>

using namespace ui;
using hardware::testing::pixelAt;

namespace {

struct Fixture {
    hardware::LedMatrix matrix{0};
    InstrumentsView view{matrix};
};

events::Event press(KeyId id, uint8_t mods = mod::NONE) {
    return events::Event::keyPressed(id, mods, 0);
}

state::UIState sample() {
    state::UIState s;
    s.patternCount = 4;
    s.activePatterns = 0b1011;                // pattern 2 inactive
    s.instruments[0].members = 0b0111;        // patterns 0,1,2 assigned
    s.instruments[0].enabled = 0b0101;        // route of pattern 1 off
    return s;
}

}  // namespace

TEST(instrumentsview_label_shows_the_selected_channel) {
    Fixture f;
    hardware::testing::resetMatrix();
    state::UIState s = sample();
    s.instruments[0].channel = 7;
    f.view.render(s);
    CHECK(std::strcmp(hardware::testing::lastLabel(), "c07") == 0);
}

TEST(instrumentsview_draws_the_five_pattern_cell_states) {
    Fixture f;
    hardware::testing::resetMatrix();
    f.view.render(sample());

    // pattern 0: assigned, route on, active -> green (top-left is the cursor)
    CHECK_EQ(pixelAt(1, 5), InstrumentsView::COLOR_SOUNDING);
    CHECK_EQ(pixelAt(0, 6), InstrumentsView::COLOR_SOUNDING);
    CHECK_EQ(pixelAt(1, 6), InstrumentsView::COLOR_SOUNDING);
    // pattern 1: assigned, route off -> red 2x2
    for (uint8_t y = 5; y <= 6; ++y)
        for (uint8_t x = 3; x <= 4; ++x) CHECK_EQ(pixelAt(x, y), InstrumentsView::COLOR_ROUTE_OFF);
    // pattern 2: assigned, route on, pattern inactive -> blue
    CHECK_EQ(pixelAt(6, 5), InstrumentsView::COLOR_PATTERN_OFF);
    CHECK_EQ(pixelAt(7, 6), InstrumentsView::COLOR_PATTERN_OFF);
    // pattern 3: not assigned -> faint grey
    CHECK_EQ(pixelAt(9, 5), InstrumentsView::COLOR_NOT_MEMBER);
    // pattern 4 does not exist -> dark
    CHECK_EQ(pixelAt(12, 5), 0);
}

TEST(instrumentsview_cursor_is_the_white_top_left_pixel_of_the_cell) {
    Fixture f;
    hardware::testing::resetMatrix();
    state::UIState s = sample();
    s.instrumentCursor = 1;            // pattern 2, route off (red)
    f.view.render(s);
    CHECK_EQ(pixelAt(3, 5), InstrumentsView::COLOR_CURSOR);
    CHECK_EQ(pixelAt(4, 5), InstrumentsView::COLOR_ROUTE_OFF);   // the state stays visible
    CHECK_EQ(pixelAt(3, 6), InstrumentsView::COLOR_ROUTE_OFF);
    CHECK_EQ(pixelAt(0, 5), InstrumentsView::COLOR_SOUNDING);    // pattern 1 without cursor
    for (uint8_t x = 0; x < 15; ++x) CHECK_EQ(pixelAt(x, 7), 0);  // gap row stays dark

    s.instrumentCursor = 7;            // row 1, column 2: cell at x 6..7, y 8..9 (no pattern)
    f.view.render(s);
    CHECK_EQ(pixelAt(3, 5), InstrumentsView::COLOR_ROUTE_OFF);   // the old cursor is erased
    CHECK_EQ(pixelAt(6, 8), InstrumentsView::COLOR_CURSOR);       // shown on an empty cell too
}

TEST(instrumentsview_strip_shows_free_occupied_muted_and_selected_slots) {
    Fixture f;
    hardware::testing::resetMatrix();
    state::UIState s = sample();
    s.instrumentOccupied = 0b111;
    s.selectedInstrument = 1;
    s.instruments[2].muted = true;
    f.view.render(s);
    CHECK_EQ(pixelAt(0, 15), InstrumentsView::COLOR_STRIP_OCCUPIED);
    CHECK_EQ(pixelAt(1, 15), InstrumentsView::COLOR_STRIP_SELECTED);
    CHECK_EQ(pixelAt(2, 15), InstrumentsView::COLOR_STRIP_MUTED);
    CHECK_EQ(pixelAt(3, 15), 0);
}

TEST(instrumentsview_muted_instrument_is_drawn_at_half_brightness) {
    Fixture f;
    hardware::testing::resetMatrix();
    state::UIState s = sample();
    s.instruments[0].muted = true;
    f.view.render(s);
    CHECK_EQ(pixelAt(1, 5), 0x007F0000);
    CHECK_EQ(pixelAt(0, 5), InstrumentsView::COLOR_CURSOR);  // the cursor is never dimmed
}

TEST(instrumentsview_notice_turns_the_strip_red) {
    Fixture f;
    hardware::testing::resetMatrix();
    state::UIState s = sample();
    s.instrumentNotice = state::InstrumentNotice::NO_FREE_CHANNEL;
    f.view.render(s);
    for (uint8_t x = 0; x < 16; ++x) CHECK_EQ(pixelAt(x, 15), InstrumentsView::COLOR_NOTICE);
}

TEST(instrumentsview_arrows_move_the_cursor_and_ctrl_arrows_select_instruments) {
    Fixture f;
    state::UIState s = sample();
    state::createInstrument(s);          // slot 1, selected
    s.selectedInstrument = 0;
    CHECK_EQ(test::handle(f.view, s, press(KeyId::RIGHT)).instrumentCursor, 1);
    CHECK_EQ(test::handle(f.view, s, press(KeyId::DOWN)).instrumentCursor, 5);
    CHECK_EQ(test::handle(f.view, s, press(KeyId::RIGHT, mod::CTRL)).selectedInstrument, 1);
    s.selectedInstrument = 1;
    CHECK_EQ(test::handle(f.view, s, press(KeyId::LEFT, mod::CTRL)).selectedInstrument, 0);
}

TEST(instrumentsview_enter_e_and_m_edit_the_selected_instrument) {
    Fixture f;
    commands::testing::reset();
    state::UIState s = sample();
    s.instrumentCursor = 3;  // pattern 3: not assigned

    auto a = test::handle(f.view, s, press(KeyId::ENTER));
    CHECK_EQ(a.instruments[0].members, 0b1111);
    CHECK_EQ(a.instruments[0].enabled, 0b1101);   // route of pattern 3 on, pattern 1 still off

    auto b = test::handle(f.view, a, press(KeyId::E));
    CHECK_EQ(b.instruments[0].enabled, 0b0101);   // route of pattern 3 off

    a.instrumentCursor = 0;
    auto c = test::handle(f.view, a, press(KeyId::E));
    CHECK_EQ(c.instruments[0].enabled, 0b1100);

    auto d = test::handle(f.view, s, press(KeyId::M));
    CHECK(d.instruments[0].muted);
    CHECK(commands::testing::countOf(commands::Command::INSTRUMENT_SET) >= 4);

    // E on a pattern that is not assigned is refused with a notice.
    s.instrumentCursor = 3;
    auto e = test::handle(f.view, s, press(KeyId::E));
    CHECK(e.instrumentNotice == state::InstrumentNotice::NOT_MEMBER);
}

TEST(instrumentsview_shift_arrows_change_the_channel) {
    Fixture f;
    state::UIState s = sample();
    CHECK_EQ(test::handle(f.view, s, press(KeyId::RIGHT, mod::SHIFT)).instruments[0].channel, 2);
    s.instruments[0].channel = 5;
    CHECK_EQ(test::handle(f.view, s, press(KeyId::LEFT, mod::SHIFT)).instruments[0].channel, 4);
}

TEST(instrumentsview_ctrl_n_creates_and_delete_removes_an_instrument) {
    Fixture f;
    state::UIState s = sample();
    auto a = test::handle(f.view, s, press(KeyId::N, mod::CTRL));
    CHECK_EQ(a.instrumentOccupied, 0b11);
    CHECK_EQ(a.selectedInstrument, 1);
    auto b = test::handle(f.view, a, press(KeyId::DELETE_KEY));
    CHECK_EQ(b.instrumentOccupied, 0b01);
    auto c = test::handle(f.view, b, press(KeyId::DELETE_KEY));  // last Instrument: refused
    CHECK_EQ(c.instrumentOccupied, 0b01);
    CHECK(c.instrumentNotice == state::InstrumentNotice::LAST_INSTRUMENT);
}

TEST(instrumentsview_ignores_key_release_and_unbound_keys) {
    Fixture f;
    state::UIState s = sample();
    const auto released = events::Event::keyReleased(KeyId::ENTER, mod::NONE, 0);
    CHECK_EQ(test::handle(f.view, s, released).instruments[0].members, s.instruments[0].members);
    CHECK_EQ(test::handle(f.view, s, press(KeyId::Q)).instrumentCursor, 0);
}

TEST(patternsview_marks_patterns_outside_the_selected_instrument_and_unassigned_ones) {
    hardware::LedMatrix matrix{0};
    PatternsView view{matrix};
    hardware::testing::resetMatrix();
    state::UIState s;
    s.patternCount = 3;
    s.activePatterns = 0b111;
    s.selectedPattern = 0;
    s.instruments[0].members = s.instruments[0].enabled = 0b001;   // selected Instrument: pattern 0
    state::createInstrument(s);                                   // slot 1
    s.instruments[1].members = s.instruments[1].enabled = 0b010;   // pattern 1 is in Instrument 1
    s.selectedInstrument = 0;                                     // pattern 2 is in none

    view.render(s);

    // pattern 0: member -> full cell
    CHECK_EQ(pixelAt(1, 5), 0x0000FF00);   // (0,5) is the white selection marker
    CHECK_EQ(pixelAt(1, 6), 0x0000FF00);
    // pattern 1: assigned elsewhere -> only the top-left pixel, no magenta
    CHECK_EQ(pixelAt(3, 5), 0x0000FF00);
    CHECK_EQ(pixelAt(4, 5), 0);
    CHECK_EQ(pixelAt(3, 6), 0);
    // pattern 2: in no Instrument -> top-left pixel plus magenta top-right
    CHECK_EQ(pixelAt(6, 5), 0x0000FF00);
    CHECK_EQ(pixelAt(7, 5), 0x0000FFFF);
}
