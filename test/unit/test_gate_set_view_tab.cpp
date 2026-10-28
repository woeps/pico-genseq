// ui-integration task T12 — TAB navigation from GATE_SET to
// GENERATOR_CONFIG (FR5, AC5).
//
// Validates the GateSetView's TAB drill-in and the navigation roundtrip:
//   - a pressed TAB in GateSetView sets currentView to GENERATOR_CONFIG
//     (FR5: TAB from GATE_SET navigates to GENERATOR_CONFIG for gate
//     generator selection) and posts NO command — navigation is pure UI
//     state, nothing crosses the command queue (FR17);
//   - TAB leaves through setCurrentView, so a dirty GATE_SET draft is
//     restored exactly like the pre-existing F1..F4 navigation (the
//     reducer's draft-restore contract: the draft reverts to the committed
//     config and the restore posts the one sendGateSet sync);
//   - a held TAB repeat does not navigate (the sibling views ignore held
//     repeats for one-shot gestures);
//   - the roundtrip closes: after TAB, a pressed ESCAPE handled by the
//     GeneratorConfigView returns currentView to GATE_SET (FR11: ESCAPE
//     returns to the appropriate parent view — the ENTER drill-in path is
//     GATE_SET -> TAB -> GENERATOR_CONFIG; ESCAPE walks straight back);
//   - regression: the pre-existing GateSetView bindings survive — a plain
//     ESCAPE (clean draft) still returns to PATTERNS and an UP press still
//     adjusts the draft without navigating anywhere.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_gate_set_view_tab".
//
// GateSetView.cpp + GeneratorConfigView.cpp + Reducer.cpp (the real,
// SDK-free dispatch path) are compiled directly into this target. Their
// render paths target the real LedMatrix driver (Pico SDK PIO) and their
// writes target the commands:: FIFO seam (command.cpp is not compiled
// host-side), so — like test_generator_config_view — this TU supplies
// host-safe definitions: no-op LedMatrix member functions, a RECORDING
// commands::sendGateSet double (so the draft-restore sync is observable)
// and no-op sendCommand / sendGenParamCommand / sendPitchSet /
// sendVelocitySet / sendScaleCommand doubles. common/ symbols (the built-in
// generator list behind registerGenerators) arrive through genseq_emission.
// NONE of the Pico SDK libs and NO RapidCheck.

#include "commands/command.h"
#include "sequencer/GeneratorRegistry.h"
#include "ui/Event.h"
#include "ui/Types.h"
#include "ui/hardware/LedMatrix.h"
#include "ui/state/Reducer.h"
#include "ui/state/UIState.h"
#include "ui/views/GateSetView.h"
#include "ui/views/GeneratorConfigView.h"

#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

using ui::GateSetView;
using ui::GeneratorConfigView;
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
// links against these definitions instead. sendGateSet is a RECORDING double
// (the draft-restore sync posts through it); the rest are no-ops (no edit in
// this test reaches them).

namespace commands {

std::vector<std::pair<uint8_t, size_t>> g_gateSetCalls;  // {patternIndex, size}

void sendGateSet(uint8_t patternIndex, const std::vector<bool>& gates) {
    g_gateSetCalls.push_back({patternIndex, gates.size()});
}

void sendCommand(Command, uint8_t, uint8_t) {
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

void sendGenParamCommand(uint8_t, uint8_t, int16_t) {
    // No-op host double (the roundtrip only navigates; no edit is made).
}

}  // namespace commands

namespace {

// --- shared fixture -----------------------------------------------------------

hardware::LedMatrix matrix{16};
sequencer::GeneratorRegistry registry;

// A fresh pair of the two views joined by the TAB roundtrip. The registry
// carries the four built-in plugins (RegisterGenerators.cpp is the ONLY
// built-in listing, AC1), so GeneratorConfigView enters with a usable gate
// generator exactly as on the device.
std::pair<GateSetView, GeneratorConfigView> makeViews() {
    sequencer::registerGenerators(registry);
    return {GateSetView(matrix), GeneratorConfigView(registry, matrix)};
}

// A state sitting in the gate-lane edit view, ready to drill deeper.
UIState gateSetState() {
    UIState state;
    state.currentView = ViewId::GATE_SET;
    state.selectedPattern = 0;
    return state;
}

// --- 1. TAB navigates to GENERATOR_CONFIG (FR5, AC5) ---------------------------

void test_tab_navigates_to_generator_config() {
    auto [gateView, configView] = makeViews();
    UIState state = gateSetState();

    commands::g_gateSetCalls.clear();
    const UIState next = gateView.handleEvent(
        state, Event::keyPressed(ui::KeyId::TAB, ui::mod::NONE, 0));

    check(next.currentView == ViewId::GENERATOR_CONFIG,
          "a pressed TAB sets currentView to GENERATOR_CONFIG");
    check(state.currentView == ViewId::GATE_SET,
          "the handled state is never mutated (pure reducer)");
    check(commands::g_gateSetCalls.empty(),
          "a plain TAB press posts no command (navigation is pure UI state)");
}

// --- 2. TAB restores a dirty draft (the setCurrentView contract) ---------------

void test_tab_restores_dirty_draft() {
    auto [gateView, configView] = makeViews();
    UIState state = gateSetState();

    // A dirty draft that diverges from the committed config.
    state.gateSetDraft.steps = 32;
    state.gateSetDraft.pulses = 8;
    state.gateSetDirty = true;

    commands::g_gateSetCalls.clear();
    const UIState next = gateView.handleEvent(
        state, Event::keyPressed(ui::KeyId::TAB, ui::mod::NONE, 0));

    check(next.currentView == ViewId::GENERATOR_CONFIG,
          "TAB navigates to GENERATOR_CONFIG even with a dirty draft");
    check(next.gateSetDirty == false,
          "leaving GATE_SET via TAB restores (discards) the dirty draft");
    check(next.gateSetDraft == next.gateSetConfigs[next.selectedPattern],
          "the draft is back to the committed config after the TAB navigation");
    check(commands::g_gateSetCalls.size() == 1 &&
              commands::g_gateSetCalls[0].first == 0,
          "the draft restore posts exactly one GATE_SET sync for pattern 0");
}

// --- 3. A held TAB repeat does not navigate ------------------------------------

void test_tab_held_ignored() {
    auto [gateView, configView] = makeViews();
    UIState state = gateSetState();

    const UIState next = gateView.handleEvent(
        state, Event::keyHeld(ui::KeyId::TAB, ui::mod::NONE, 0));
    check(next.currentView == ViewId::GATE_SET,
          "a held TAB repeat does not navigate");
}

// --- 4. The roundtrip: ESCAPE returns to GATE_SET (FR11, AC5) ------------------

void test_roundtrip_escape_returns_to_gate_set() {
    auto [gateView, configView] = makeViews();
    UIState state = gateSetState();

    // GATE_SET -> TAB -> GENERATOR_CONFIG.
    const UIState drilled = gateView.handleEvent(
        state, Event::keyPressed(ui::KeyId::TAB, ui::mod::NONE, 0));
    check(drilled.currentView == ViewId::GENERATOR_CONFIG,
          "the roundtrip starts from GENERATOR_CONFIG (TAB drilled in)");

    // GENERATOR_CONFIG -> ESCAPE -> GATE_SET: the config view walks
    // straight back to the gate-lane view it was drilled into from.
    const UIState back = configView.handleEvent(
        drilled, Event::keyPressed(ui::KeyId::ESCAPE, ui::mod::NONE, 0));
    check(back.currentView == ViewId::GATE_SET,
          "a pressed ESCAPE returns the state to GATE_SET (the roundtrip closes)");
    check(back.selectedPattern == state.selectedPattern,
          "the roundtrip leaves the rest of the state untouched");
}

// --- 5. Regression: the pre-existing GATE_SET bindings survive ------------------

void test_existing_bindings_regression() {
    auto [gateView, configView] = makeViews();

    // A clean ESCAPE still backs out to PATTERNS (the pre-existing parent).
    {
        UIState state = gateSetState();
        const UIState next = gateView.handleEvent(
            state, Event::keyPressed(ui::KeyId::ESCAPE, ui::mod::NONE, 0));
        check(next.currentView == ViewId::PATTERNS,
              "a plain ESCAPE (clean draft) still returns to PATTERNS");
    }

    // An UP press still adjusts the draft without navigating anywhere.
    {
        UIState state = gateSetState();
        state.selectedGateSetProperty = ui::state::GateSetProperty::STEPS;
        const UIState next = gateView.handleEvent(
            state, Event::keyPressed(ui::KeyId::UP, ui::mod::NONE, 0));
        check(next.currentView == ViewId::GATE_SET,
              "an UP press does not navigate away from GATE_SET");
        check(next.gateSetDraft.steps == 17,
              "an UP press still steps the STEPS draft (16 -> 17)");
    }
}

}  // namespace

int main() {
    test_tab_navigates_to_generator_config();
    test_tab_restores_dirty_draft();
    test_tab_held_ignored();
    test_roundtrip_escape_returns_to_gate_set();
    test_existing_bindings_regression();

    if (failures == 0) {
        std::printf("test_gate_set_view_tab: all checks passed\n");
        return 0;
    }
    std::printf("test_gate_set_view_tab: %d check(s) failed\n", failures);
    return 1;
}
