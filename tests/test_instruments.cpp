// Instruments task 6.4 / 6.5 -- Instrument list reducer: creation, channels, membership, route
// enable, mute, pattern add/remove hooks, command folding and core0/core1 mask agreement.
#include "framework.h"
#include "helpers.h"
#include "stubs/command_stub.h"
#include "common/InstrumentRouting.h"
#include "ui/state/Instruments.h"
#include "ui/state/Reducer.h"
#include "ui/state/UIState.h"

using namespace ui;
using commands::Command;
using state::InstrumentNotice;

namespace {

state::UIState threePatterns() {
    state::UIState s;
    s.patternCount = 3;
    s.activePatterns = 0b111;
    return s;
}

// Last INSTRUMENT_SET sent for `slot`, or nullptr.
const commands::CommandMessage* lastSet(uint8_t slot) {
    const auto& v = commands::testing::sentCommands();
    for (size_t i = v.size(); i-- > 0;) {
        if (v[i].cmd == Command::INSTRUMENT_SET && v[i].param1 == slot) return &v[i];
    }
    return nullptr;
}

struct Lcg {
    uint32_t x = 12345;
    uint32_t next(uint32_t n) { x = x * 1664525u + 1013904223u; return (x >> 8) % n; }
};

}  // namespace

TEST(default_state_has_one_instrument_on_channel_1_with_pattern_0) {
    state::UIState s;
    CHECK(state::validateInstruments(s));
    CHECK_EQ(s.instrumentOccupied, 0x0001);
    CHECK_EQ(s.instruments[0].channel, 1);
    CHECK_EQ(s.instruments[0].members, 0x0001);
    CHECK_EQ(s.selectedInstrument, 0);
}

TEST(create_takes_lowest_free_slot_and_channel_and_selects_it) {
    state::UIState s = threePatterns();
    commands::testing::reset();
    state::createInstrument(s);
    CHECK_EQ(s.selectedInstrument, 1);
    CHECK_EQ(s.instruments[1].channel, 2);
    CHECK_EQ(s.instruments[1].members, 0);
    CHECK(!s.instruments[1].muted);
    CHECK(state::validateInstruments(s));
    const auto* m = lastSet(1);
    CHECK(m != nullptr);
    if (m) { CHECK_EQ(m->param2, 1); CHECK_EQ(m->patternMask, 0); }
}

TEST(create_refused_when_all_sixteen_slots_are_taken) {
    state::UIState s = threePatterns();
    for (int i = 1; i < 16; ++i) state::createInstrument(s);
    CHECK_EQ(s.instrumentOccupied, 0xFFFF);
    commands::testing::reset();
    const state::UIState before = s;
    state::createInstrument(s);
    CHECK(s.instrumentNotice == InstrumentNotice::FULL);
    CHECK_EQ(s.instrumentOccupied, before.instrumentOccupied);
    CHECK_EQ(commands::testing::sentCommands().size(), 0);
}

TEST(delete_last_instrument_refused_and_other_delete_frees_slot_and_empties_route) {
    state::UIState s = threePatterns();
    commands::testing::reset();
    state::deleteInstrument(s, 0);
    CHECK(s.instrumentNotice == InstrumentNotice::LAST_INSTRUMENT);
    CHECK_EQ(s.instrumentOccupied, 0x0001);
    CHECK_EQ(commands::testing::sentCommands().size(), 0);

    state::createInstrument(s);  // slot 1
    state::toggleInstrumentPattern(s, 1, 2);
    commands::testing::reset();
    state::deleteInstrument(s, 1);  // has a pattern: allowed (R1.7)
    CHECK_EQ(s.instrumentOccupied, 0x0001);
    CHECK_EQ(s.selectedInstrument, 0);
    const auto* m = lastSet(1);
    CHECK(m != nullptr);
    if (m) CHECK_EQ(m->patternMask, 0);
    CHECK(state::validateInstruments(s));
}

TEST(channel_step_skips_used_channels_and_stops_at_the_bounds) {
    state::UIState s = threePatterns();
    state::createInstrument(s);  // slot 1, channel 2
    state::createInstrument(s);  // slot 2, channel 3
    state::changeInstrumentChannel(s, 0, +1);  // 1 -> next free above, 2 and 3 are used -> 4
    CHECK_EQ(s.instruments[0].channel, 4);
    state::changeInstrumentChannel(s, 0, -1);  // back down: 3, 2 used -> 1
    CHECK_EQ(s.instruments[0].channel, 1);
    commands::testing::reset();
    state::changeInstrumentChannel(s, 0, -1);  // below 1: refused, no wrap
    CHECK_EQ(s.instruments[0].channel, 1);
    CHECK(s.instrumentNotice == InstrumentNotice::NO_FREE_CHANNEL);
    CHECK_EQ(commands::testing::sentCommands().size(), 0);

    s.instruments[2].channel = 16;
    state::changeInstrumentChannel(s, 2, +1);
    CHECK_EQ(s.instruments[2].channel, 16);  // no wrap to 1
    CHECK(state::validateInstruments(s));
}

TEST(channel_change_sends_the_new_protocol_channel_with_the_folded_mask) {
    state::UIState s = threePatterns();
    commands::testing::reset();
    state::changeInstrumentChannel(s, 0, +1);
    const auto* m = lastSet(0);
    CHECK(m != nullptr);
    if (m) { CHECK_EQ(m->param2, 1); CHECK_EQ(m->patternMask, 0x0001); }
}

TEST(member_toggle_assigns_and_unassigns_one_pattern) {
    state::UIState s = threePatterns();
    commands::testing::reset();
    state::toggleInstrumentPattern(s, 0, 2);
    CHECK_EQ(s.instruments[0].members, 0b101);
    const auto* m = lastSet(0);
    CHECK(m != nullptr);
    if (m) CHECK_EQ(m->patternMask, 0b101);
    state::toggleInstrumentPattern(s, 0, 2);
    CHECK_EQ(s.instruments[0].members, 0b001);
    CHECK_EQ(s.activePatterns, 0b111);  // unassigning never touches the active flag
    // A pattern may be a member of several Instruments.
    state::createInstrument(s);
    state::toggleInstrumentPattern(s, 1, 0);
    CHECK_EQ(state::instrumentsOf(s, 0), 0b11);
    CHECK(state::validateInstruments(s));
}

TEST(mute_sends_an_empty_mask_and_keeps_every_other_switch) {
    state::UIState s = threePatterns();
    state::toggleInstrumentPattern(s, 0, 1);
    commands::testing::reset();
    state::toggleInstrumentMute(s, 0);
    CHECK(s.instruments[0].muted);
    CHECK_EQ(s.instruments[0].members, 0b011);
    CHECK_EQ(s.activePatterns, 0b111);
    const auto* m = lastSet(0);
    CHECK(m != nullptr);
    if (m) CHECK_EQ(m->patternMask, 0);
    state::toggleInstrumentMute(s, 0);
    m = lastSet(0);
    if (m) CHECK_EQ(m->patternMask, 0b011);
}

TEST(effective_mask_is_members_unless_muted) {
    state::UIState s = threePatterns();
    state::createInstrument(s);                 // slot 1
    state::toggleInstrumentPattern(s, 1, 0);
    CHECK_EQ(state::effectiveMask(s, 1), 0b001);
    state::toggleInstrumentMute(s, 1);
    CHECK_EQ(state::effectiveMask(s, 1), 0);
    // The pattern active flag is not part of the mask: core1 owns it.
    s.activePatterns = 0;
    state::toggleInstrumentMute(s, 1);
    CHECK_EQ(state::effectiveMask(s, 1), 0b001);
}

TEST(add_pattern_joins_the_selected_instrument_and_sends_its_route_last) {
    state::UIState s;
    state::createInstrument(s);  // slot 1 selected
    commands::testing::reset();
    state::addPattern(s);
    CHECK_EQ(s.patternCount, 2);
    CHECK_EQ(s.instruments[1].members, 0b10);
    CHECK_EQ(s.instruments[0].members, 0b01);
    const auto& v = commands::testing::sentCommands();
    CHECK(!v.empty());
    if (!v.empty()) {
        CHECK(v.back().cmd == Command::INSTRUMENT_SET);
        CHECK_EQ(v.back().patternMask, 0b10);
    }
    CHECK(state::validateInstruments(s));
}

TEST(remove_pattern_shifts_every_instrument_mask) {
    state::UIState s = threePatterns();
    state::createInstrument(s);
    state::toggleInstrumentPattern(s, 0, 1);
    state::toggleInstrumentPattern(s, 0, 2);   // slot 0: patterns 0,1,2
    state::toggleInstrumentPattern(s, 1, 2);   // slot 1: pattern 2
    state::removePattern(s, 1);
    CHECK_EQ(s.patternCount, 2);
    CHECK_EQ(s.instruments[0].members, 0b11);
    CHECK_EQ(s.instruments[1].members, 0b10);
    CHECK(state::validateInstruments(s));
}

TEST(restore_sends_all_sixteen_slots) {
    state::UIState s = threePatterns();
    state::createInstrument(s);
    state::toggleInstrumentPattern(s, 1, 2);
    commands::testing::reset();
    state::restoreSequencerFromState(s);
    CHECK_EQ(commands::testing::countOf(Command::INSTRUMENT_SET), 16);
    const auto* m = lastSet(1);
    CHECK(m != nullptr);
    if (m) CHECK_EQ(m->patternMask, 0b100);
    m = lastSet(9);  // a free slot is emptied
    CHECK(m != nullptr);
    if (m) CHECK_EQ(m->patternMask, 0);
}

TEST(cursor_moves_over_the_five_by_three_grid_and_clamps) {
    state::UIState s;
    s.patternCount = 15;
    state::moveInstrumentCursor(s, +1, 0);
    CHECK_EQ(s.instrumentCursor, 1);
    state::moveInstrumentCursor(s, 0, +1);
    CHECK_EQ(s.instrumentCursor, 6);
    state::moveInstrumentCursor(s, +9, +9);
    CHECK_EQ(s.instrumentCursor, 14);
    state::moveInstrumentCursor(s, -9, -9);
    CHECK_EQ(s.instrumentCursor, 0);
}

TEST(cursor_stays_on_existing_patterns) {
    state::UIState s;
    s.patternCount = 7;                       // row 0: patterns 0-4, row 1: patterns 5-6
    state::moveInstrumentCursor(s, +9, 0);
    CHECK_EQ(s.instrumentCursor, 4);          // end of the full first row
    state::moveInstrumentCursor(s, 0, +1);    // below 4 is empty: go to the last pattern
    CHECK_EQ(s.instrumentCursor, 6);
    state::moveInstrumentCursor(s, +1, 0);    // right of the last pattern: stay
    CHECK_EQ(s.instrumentCursor, 6);
    state::moveInstrumentCursor(s, 0, +1);    // row 2 is empty: stay
    CHECK_EQ(s.instrumentCursor, 6);
    state::moveInstrumentCursor(s, -1, 0);
    CHECK_EQ(s.instrumentCursor, 5);
    state::moveInstrumentCursor(s, 0, -1);
    CHECK_EQ(s.instrumentCursor, 0);

    state::UIState row;                       // 3 patterns, all in the first row
    row.patternCount = 3;
    state::moveInstrumentCursor(row, 0, +1);  // Down: no pattern in the next row -> stay
    CHECK_EQ(row.instrumentCursor, 0);
    row.instrumentCursor = 2;
    state::moveInstrumentCursor(row, 0, -1);  // Up from the top row -> stay
    CHECK_EQ(row.instrumentCursor, 2);
    state::moveInstrumentCursor(row, +1, 0);  // Right of the last pattern -> stay
    CHECK_EQ(row.instrumentCursor, 2);
    state::moveInstrumentCursor(row, -9, 0);
    CHECK_EQ(row.instrumentCursor, 0);

    state::UIState one;                       // a single pattern: the cursor never moves
    state::moveInstrumentCursor(one, +1, 0);
    state::moveInstrumentCursor(one, 0, +1);
    CHECK_EQ(one.instrumentCursor, 0);
}

TEST(removing_a_pattern_keeps_the_cursor_on_an_existing_pattern) {
    state::UIState s;
    for (int i = 0; i < 3; ++i) state::addPattern(s);   // 4 patterns
    s.instrumentCursor = 3;
    state::removePattern(s, 3);
    CHECK_EQ(s.patternCount, 3);
    CHECK_EQ(s.instrumentCursor, 2);
}

TEST(select_steps_over_occupied_slots_without_wrapping) {
    state::UIState s;
    state::createInstrument(s);
    state::createInstrument(s);
    s.instrumentOccupied = 0b101;  // slots 0 and 2
    s.selectedInstrument = 0;
    CHECK_EQ(state::selectInstrumentStep(s, +1), 2);
    CHECK_EQ(state::selectInstrumentStep(s, +1), 2);
    CHECK_EQ(state::selectInstrumentStep(s, -1), 0);
    CHECK_EQ(state::selectInstrumentStep(s, -1), 0);
}

// Random operation sequences keep the invariants of design section 2, and the UI masks stay equal
// to the core1 RouteTable when both see the same pattern removals (task 6.5).
TEST(random_operations_keep_invariants_and_ui_masks_match_the_route_table) {
    Lcg rng;
    for (int run = 0; run < 200; ++run) {
        state::UIState s;
        for (int i = 0; i < 6; ++i) state::addPattern(s);
        common::RouteTable table;
        for (int op = 0; op < 80; ++op) {
            const uint8_t slot = static_cast<uint8_t>(rng.next(16));
            const uint8_t p = static_cast<uint8_t>(rng.next(s.patternCount));
            switch (rng.next(8)) {
                case 0: state::createInstrument(s); break;
                case 1: state::deleteInstrument(s, slot); break;
                case 2: state::changeInstrumentChannel(s, slot, rng.next(2) ? 1 : -1); break;
                case 3: state::toggleInstrumentMute(s, slot); break;
                case 4: state::toggleInstrumentPattern(s, slot, p); break;
                case 5: state::selectInstrumentStep(s, rng.next(2) ? 1 : -1); break;
                case 6: if (s.patternCount < state::MAX_PATTERNS) state::addPattern(s); break;
                case 7:
                    if (s.patternCount > 1) {
                        // Bring the table to the current effective state, then remove on both sides.
                        for (uint8_t i = 0; i < state::MAX_INSTRUMENTS; ++i) {
                            table.set(i, state::protocolChannelOf(s, i), state::effectiveMask(s, i));
                        }
                        const uint8_t idx = static_cast<uint8_t>(rng.next(s.patternCount));
                        // Unmute so effectiveMask equals members & enabled for the comparison.
                        for (uint8_t i = 0; i < state::MAX_INSTRUMENTS; ++i) s.instruments[i].muted = false;
                        for (uint8_t i = 0; i < state::MAX_INSTRUMENTS; ++i) {
                            table.set(i, state::protocolChannelOf(s, i), state::effectiveMask(s, i));
                        }
                        table.removePattern(idx);
                        state::removePattern(s, idx);
                        for (uint8_t i = 0; i < state::MAX_INSTRUMENTS; ++i) {
                            CHECK_EQ(table.patterns[i], state::effectiveMask(s, i));
                        }
                    }
                    break;
            }
            if (!state::validateInstruments(s)) {
                CHECK(false);
                return;
            }
        }
    }
}
