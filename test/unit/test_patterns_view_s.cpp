// ui-integration task T15 — S key navigation from PATTERNS to the
// SCALE_SELECTION view.
//
// Validates the PatternsView's S drill-in:
//   - a pressed S with a real pattern selected sets currentView to
//     SCALE_SELECTION and posts NO command — navigation is pure UI state,
//     nothing crosses the command queue (FR17). The selected pattern
//     travels with the state untouched (ScaleSelectionView reads the
//     pattern it edits from UIState.selectedPattern) and ESCAPE returns
//     here (FR11);
//   - the S press is REFUSED when no pattern is selected: the new-slot
//     cursor (selectedPattern == patternCount, the same guard the ENTER
//     add-path and the sibling M drill-in use) leaves currentView at
//     PATTERNS — there is no pattern whose scale to select;
//   - a held S repeat does not navigate (one-shot gesture, matching the
//     ENTER / M drill-ins); a released S is not handled at all;
//   - the roundtrip guard: S navigates from a later pattern
//     (selectedPattern == 1 with two patterns), so the guard is the
//     pattern-existence check, not an index == 0 check;
//   - plain S and the global Ctrl+S Save_Combo never collide: a
//     CTRL-modified S is NOT a drill-in (the reducer consumes Ctrl+S
//     globally as the Save_Combo before any view sees it, so the view's
//     combo() with the CTRL bit set never fires here — asserted anyway
//     as a local guarantee);
//   - regression: the pre-existing PatternsView bindings survive — the
//     sibling M drill-in still reaches MUTATION, a plain ENTER on the
//     selected pattern still drills into GATE_SET, and the pattern-set
//     navigation (DOWN) still works without navigating away.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in
// the host test build under the ctest name "test_patterns_view_s".
//
// PatternsView.cpp + Reducer.cpp (the real, SDK-free dispatch path that
// setCurrentView rides) are compiled directly into this target. The render
// path targets the real LedMatrix driver (Pico SDK PIO) and the state
// setters target the commands:: FIFO seam (command.cpp is not compiled
// host-side), so — like test_patterns_view_m — this TU supplies
// host-safe definitions: no-op LedMatrix member functions, a RECORDING
// commands::sendCommand double (so the "navigation posts nothing" claim is
// observable) and no-op sendGateSet / sendPitchSet / sendVelocitySet /
// sendScaleCommand doubles. pico/multicore.h comes from test/pico_stubs
// (EMISSION_INCLUDE_DIRS). NONE of the Pico SDK libs and NO RapidCheck.

#include "commands/command.h"
#include "ui/Event.h"
#include "ui/Types.h"
#include "ui/hardware/LedMatrix.h"
#include "ui/state/Reducer.h"
#include "ui/state/UIState.h"
#include "ui/views/PatternsView.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using ui::PatternsView;
using ui::events::Event;
using ui::state::UIState;
using ui::state::ViewId;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

}  // namespace

// --- host-safe LedMatrix seam (see the file comment) --------------------------
// Defined in the REAL hardware namespace (outside the anonymous one) so they
// replace the SDK-dependent LedMatrix.cpp definitions at link time.

namespace hardware {
LedMatrix::LedMatrix(uint8_t pin) : pin(pin), buffer{}, dirty(false) {}
void LedMatrix::update() {}
void LedMatrix::clear() { dirty = true; }
void LedMatrix::setPixel(uint8_t, uint8_t, uint32_t) { dirty = true; }
void LedMatrix::fill(uint32_t) { dirty = true; }
void LedMatrix::drawNumber(int, uint32_t) { dirty = true; }
void LedMatrix::drawLabel(const char (&)[4], uint32_t) { dirty = true; }
void LedMatrix::drawNote(const char (&)[4], uint32_t) { dirty = true; }
}  // namespace hardware

// --- host-safe commands:: seam -------------------------------------------------
// command.cpp (the multicore FIFO) is not compiled host-side, so this TU
// links against these definitions instead. sendCommand is a RECORDING double
// (the S navigation must post NOTHING — pure UI state); the set senders are
// no-ops (no edit in this test reaches them).

namespace commands {

std::vector<std::vector<int>> g_commandCalls;  // {cmd, param1, param2}

void sendCommand(Command cmd, uint8_t param1, uint8_t param2) {
    g_commandCalls.push_back({static_cast<int>(cmd), param1, param2});
}

void sendGateSet(uint8_t, const std::vector<bool>&) {
    // No-op host double.
}

void sendPitchSet(uint8_t, uint8_t, common::PlayingOrder,
                  const std::vector<uint8_t>&) {
    // No-op host double.
}

void sendVelocitySet(uint8_t, uint8_t, common::PlayingOrder,
                  const std::vector<uint8_t>&) {
    // No-op host double.
}

void sendScaleCommand(uint8_t, uint8_t) {
    // No-op host double.
}

}  // namespace commands

namespace {

// --- shared fixture -----------------------------------------------------------

hardware::LedMatrix matrix{16};

// A state sitting in the patterns browser with pattern 0 selected (the
// fresh-boot default: one pattern, cursor on it).
UIState patternsState() {
    UIState state;
    state.currentView = ViewId::PATTERNS;
    state.selectedPattern = 0;
    state.patternCount = 1;
    return state;
}

// --- 1. A pressed S navigates to SCALE_SELECTION -------------------------------

void test_s_navigates_to_scale_selection() {
    PatternsView view(matrix);
    UIState state = patternsState();

    commands::g_commandCalls.clear();
    const UIState next = view.handleEvent(
        state, Event::keyPressed(ui::KeyId::S, ui::mod::NONE, 0));

    check(next.currentView == ViewId::SCALE_SELECTION,
          "a pressed S with a pattern selected sets currentView to SCALE_SELECTION");
    check(state.currentView == ViewId::PATTERNS,
          "the handled state is never mutated (pure reducer)");
    check(next.selectedPattern == 0 && next.patternCount == 1,
          "the selected pattern travels with the state untouched");
    check(commands::g_commandCalls.empty(),
          "a plain S press posts no command (navigation is pure UI state)");
}

// --- 2. S is refused when no pattern is selected -------------------------------

void test_s_refused_without_selected_pattern() {
    PatternsView view(matrix);

    // The new-slot cursor: selectedPattern == patternCount, the exact guard
    // the ENTER add-path uses (pressing ENTER here would add a pattern, so
    // there is no pattern whose scale to select — S must be refused).
    UIState state = patternsState();
    state.selectedPattern = 1;  // == patternCount (the + slot)

    commands::g_commandCalls.clear();
    const UIState next = view.handleEvent(
        state, Event::keyPressed(ui::KeyId::S, ui::mod::NONE, 0));

    check(next.currentView == ViewId::PATTERNS,
          "a pressed S is refused when the cursor is on the new slot (no pattern selected)");
    check(next.selectedPattern == 1,
          "the refused S leaves the cursor where it was");
    check(commands::g_commandCalls.empty(),
          "the refused S posts no command either");

    // A cursor past the end (state carried over from a removal) is refused
    // the same way — the guard is the pattern-existence check.
    UIState beyond = patternsState();
    beyond.selectedPattern = 5;  // >= patternCount with one pattern

    const UIState still = view.handleEvent(
        beyond, Event::keyPressed(ui::KeyId::S, ui::mod::NONE, 0));
    check(still.currentView == ViewId::PATTERNS,
          "a pressed S is refused when selectedPattern is past the patterns");
}

// --- 3. A held S repeat does not navigate --------------------------------------

void test_s_held_ignored() {
    PatternsView view(matrix);
    UIState state = patternsState();

    const UIState next = view.handleEvent(
        state, Event::keyHeld(ui::KeyId::S, ui::mod::NONE, 0));
    check(next.currentView == ViewId::PATTERNS,
          "a held S repeat does not navigate");

    // A released S is not handled at all (the view filters to pressed/held).
    const UIState rel = view.handleEvent(
        state, Event::keyReleased(ui::KeyId::S, ui::mod::NONE, 0));
    check(rel.currentView == ViewId::PATTERNS,
          "a released S does not navigate");
}

// --- 4. S works from a later pattern (the guard is existence, not index 0) -----

void test_s_from_second_pattern() {
    PatternsView view(matrix);
    UIState state = patternsState();
    state.patternCount = 2;
    state.selectedPattern = 1;

    const UIState next = view.handleEvent(
        state, Event::keyPressed(ui::KeyId::S, ui::mod::NONE, 0));
    check(next.currentView == ViewId::SCALE_SELECTION,
          "S navigates to SCALE_SELECTION from pattern 1 (any real pattern)");
    check(next.selectedPattern == 1,
          "S leaves selectedPattern on the pattern whose scale it will edit");
}

// --- 5. Plain S vs the global Ctrl+S Save_Combo never collide -------------------

void test_ctrl_s_is_not_a_drill_in() {
    PatternsView view(matrix);
    UIState state = patternsState();

    commands::g_commandCalls.clear();
    const UIState next = view.handleEvent(
        state, Event::keyPressed(ui::KeyId::S, ui::mod::CTRL, 0));

    check(next.currentView == ViewId::PATTERNS,
          "a CTRL-modified S does not drill into SCALE_SELECTION "
          "(the reducer consumes Ctrl+S globally as the Save_Combo)");
    check(commands::g_commandCalls.empty(),
          "a CTRL-modified S posts no command from the view either");
}

// --- 6. Regression: the pre-existing PatternsView bindings survive -------------

void test_existing_bindings_regression() {
    PatternsView view(matrix);

    // The sibling M drill-in still reaches MUTATION.
    {
        UIState state = patternsState();
        const UIState next = view.handleEvent(
            state, Event::keyPressed(ui::KeyId::M, ui::mod::NONE, 0));
        check(next.currentView == ViewId::MUTATION,
              "a pressed M still drills into MUTATION");
    }

    // A plain ENTER on the selected pattern still drills into GATE_SET.
    {
        UIState state = patternsState();
        const UIState next = view.handleEvent(
            state, Event::keyPressed(ui::KeyId::ENTER, ui::mod::NONE, 0));
        check(next.currentView == ViewId::GATE_SET,
              "a plain ENTER still drills into GATE_SET");
    }

    // DOWN still switches the pattern-set lane without navigating anywhere.
    {
        UIState state = patternsState();
        const UIState next = view.handleEvent(
            state, Event::keyPressed(ui::KeyId::DOWN, ui::mod::NONE, 0));
        check(next.currentView == ViewId::PATTERNS,
              "a DOWN press does not navigate away from PATTERNS");
        check(next.selectedPatternSet == ui::state::PatternSet::VELOCITY,
              "a DOWN press still switches the cursor to the VELOCITY lane");
    }
}

}  // namespace

int main() {
    test_s_navigates_to_scale_selection();
    test_s_refused_without_selected_pattern();
    test_s_held_ignored();
    test_s_from_second_pattern();
    test_ctrl_s_is_not_a_drill_in();
    test_existing_bindings_regression();

    if (failures == 0) {
        std::printf("test_patterns_view_s: all checks passed\n");
        return 0;
    }
    std::printf("test_patterns_view_s: %d check(s) failed\n", failures);
    return 1;
}
