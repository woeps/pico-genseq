// ui-integration task T11 — F4 navigation to the MODULATION global view
// (FR3, AC1).
//
// Validates the Reducer's function-key binding table:
//   - a pressed F4 (no modifiers) sets currentView to MODULATION from ANY
//     starting view (MODULATION is a global view, reached like PATTERNS —
//     no SETTINGS navigation involved);
//   - the FUNCTION_KEY_VIEWS cycle grew from 3 to 4 entries: cycling
//     F1 -> F2 -> F3 -> F4 walks INIT -> SETTINGS -> PATTERNS -> MODULATION
//     in order (the pre-existing F1/F2/F3 bindings are preserved);
//   - F4 stays a globally consumed reserved key: it is never routed to the
//     active view, and held repeats / modified presses (SHIFT) navigate
//     nowhere while still being consumed;
//   - keys past the table (F5) remain reserved: consumed without a view
//     change;
//   - F4 leaves through setCurrentView, so a dirty GATE_SET draft is
//     restored exactly like the pre-existing F1/F2/F3 navigation
//     (the reducer's draft-restore contract);
//   - a plain F4 press posts NO command: navigation is pure UI state, so
//     nothing crosses the command queue (FR17 — the cores only talk via
//     the queue, and the queue stays silent when there is nothing to say).
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_reducer_f4_navigation".
//
// Like genseq_example_save_combo / property7_s_routing, this test exercises
// ui::state::reduce (src/ui/state/Reducer.cpp), which references the
// commands:: FIFO seam and common::GateSet::createEuclidean, so it is built
// as its OWN dedicated executable: the test TU supplies host-safe
// commands:: seam definitions and its own main(), and the real Reducer.cpp
// is compiled alongside it. common:: symbols arrive through genseq_emission,
// and pico/multicore.h comes from test/pico_stubs. NONE of the Pico SDK
// libs and NO RapidCheck.

#include "commands/command.h"
#include "ui/Event.h"
#include "ui/Types.h"
#include "ui/state/Reducer.h"
#include "ui/state/UIState.h"
#include "ui/views/IView.h"

#include <cstdint>
#include <cstdio>
#include <vector>

// ---------------------------------------------------------------------------
// Host-safe commands:: seams (definitions Reducer.cpp links against).
//
// Signatures MUST match command.h exactly. command.cpp (the multicore FIFO)
// is not compiled host-side. sendCommand is a RECORDING double so the tests
// can assert a plain navigation posts nothing; the set senders are no-ops
// (only the draft-restore path touches them).
// ---------------------------------------------------------------------------
namespace commands {

std::vector<std::vector<int>> g_commandCalls;  // {cmd, param1, param2}

void sendCommand(Command cmd, uint8_t param1, uint8_t param2) {
    g_commandCalls.push_back({static_cast<int>(cmd), param1, param2});
}

void sendGateSet(uint8_t /*patternIndex*/, const std::vector<bool>& /*gates*/) {
    // No-op host double.
}

void sendPitchSet(uint8_t /*patternIndex*/, uint8_t /*count*/,
                  common::PlayingOrder /*order*/,
                  const std::vector<uint8_t>& /*pitches*/) {
    // No-op host double.
}

void sendVelocitySet(uint8_t /*patternIndex*/, uint8_t /*count*/,
                     common::PlayingOrder /*order*/,
                     const std::vector<uint8_t>& /*velocities*/) {
    // No-op host double.
}

void sendScaleCommand(uint8_t /*root*/, uint8_t /*templateIndex*/) {
    // No-op host double (Reducer::setScale posts the global SCALE_SET
    // through this seam; not exercised here).
}

}  // namespace commands

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// A view that records whether handleEvent was invoked and returns the state
// unchanged. render() is a required-override no-op.
class RecordingView : public ui::IView {
public:
    bool handleEventCalled = false;

    ui::state::UIState handleEvent(const ui::state::UIState& state,
                                   const ui::events::Event& /*event*/) override {
        handleEventCalled = true;
        return state;
    }

    void render(const ui::state::UIState& /*state*/) override {
        // No-op host double.
    }
};

ui::events::Event press(ui::KeyId id, uint8_t mods = ui::mod::NONE) {
    return ui::events::Event::keyPressed(id, mods, 0);
}

// --- 1. F4 navigates to MODULATION from any starting view (FR3, AC1) ----------------

void test_f4_navigates_to_modulation() {
    const ui::state::ViewId starts[] = {ui::state::ViewId::INIT, ui::state::ViewId::SETTINGS,
                                         ui::state::ViewId::PATTERNS, ui::state::ViewId::MODULATION};
    for (ui::state::ViewId start : starts) {
        RecordingView view;
        ui::state::UIState state;
        state.currentView = start;

        commands::g_commandCalls.clear();
        const ui::state::UIState next =
            ui::state::reduce(state, press(ui::KeyId::F4), &view);

        check(next.currentView == ui::state::ViewId::MODULATION,
              "a pressed F4 sets currentView to MODULATION");
        check(view.handleEventCalled == false,
              "F4 is consumed globally and never routed to the active view");
        check(commands::g_commandCalls.empty(),
              "a plain F4 press posts no command (navigation is pure UI state)");
    }
}

// --- 2. The FUNCTION_KEY_VIEWS cycle includes MODULATION (3 -> 4 entries) ----------

void test_function_key_cycle_includes_modulation() {
    const struct {
        ui::KeyId key;
        ui::state::ViewId view;
    } cycle[] = {
        {ui::KeyId::F1, ui::state::ViewId::INIT},
        {ui::KeyId::F2, ui::state::ViewId::SETTINGS},
        {ui::KeyId::F3, ui::state::ViewId::PATTERNS},
        {ui::KeyId::F4, ui::state::ViewId::MODULATION},
    };
    // The four-entry cycle: pressing each bound function key in order walks
    // the table INIT -> SETTINGS -> PATTERNS -> MODULATION. F4 is the fourth
    // entry (the cycle count grew from 3 to 4 with MODULATION appended).
    for (const auto& entry : cycle) {
        RecordingView view;
        const ui::state::UIState state{};  // default currentView: INIT

        const ui::state::UIState next =
            ui::state::reduce(state, press(entry.key), &view);

        check(next.currentView == entry.view,
              "the function-key cycle maps F1..F4 onto INIT, SETTINGS, PATTERNS, MODULATION");
        check(view.handleEventCalled == false,
              "every bound function key is consumed globally");
    }
}

// --- 3. F4 stays a reserved global key: held/modified presses navigate nowhere -------

void test_f4_reserved_consumption() {
    // A held F4 repeat is consumed (reserved) but does not navigate.
    {
        RecordingView view;
        ui::state::UIState state;
        state.currentView = ui::state::ViewId::PATTERNS;

        const ui::state::UIState next = ui::state::reduce(
            state, ui::events::Event::keyHeld(ui::KeyId::F4, ui::mod::NONE, 0), &view);
        check(next.currentView == ui::state::ViewId::PATTERNS,
              "a held F4 repeat does not navigate");
        check(view.handleEventCalled == false,
              "a held F4 is still consumed globally (never reaches the view)");
    }

    // A modified F4 (SHIFT) is swallowed by the reservation and does not
    // navigate — modified function keys stay available for future bindings.
    {
        RecordingView view;
        ui::state::UIState state;
        state.currentView = ui::state::ViewId::SETTINGS;

        const ui::state::UIState next =
            ui::state::reduce(state, press(ui::KeyId::F4, ui::mod::SHIFT), &view);
        check(next.currentView == ui::state::ViewId::SETTINGS,
              "SHIFT+F4 does not navigate (reserved by key, not by binding)");
        check(view.handleEventCalled == false,
              "SHIFT+F4 is consumed globally, never routed to the view");
    }

    // A released F4 is consumed too: the reservation is by key id.
    {
        RecordingView view;
        ui::state::UIState state;
        state.currentView = ui::state::ViewId::INIT;

        const ui::state::UIState next = ui::state::reduce(
            state, ui::events::Event::keyReleased(ui::KeyId::F4, ui::mod::NONE, 0), &view);
        check(next.currentView == ui::state::ViewId::INIT,
              "a released F4 does not navigate");
        check(view.handleEventCalled == false,
              "a released F4 is consumed globally, never routed to the view");
    }
}

// --- 4. Keys past the table (F5) stay reserved: consumed, no view change -------------

void test_f5_beyond_table_stays_reserved() {
    RecordingView view;
    ui::state::UIState state;
    state.currentView = ui::state::ViewId::MODULATION;

    const ui::state::UIState next = ui::state::reduce(state, press(ui::KeyId::F5), &view);
    check(next.currentView == ui::state::ViewId::MODULATION,
          "F5 (past the binding table) does not change the view");
    check(view.handleEventCalled == false,
          "F5 is consumed globally (reserved), never routed to the view");
}

// --- 5. F4 leaves through setCurrentView: a dirty GATE_SET draft is restored ---------

void test_f4_restores_gate_set_draft() {
    RecordingView view;
    ui::state::UIState state;
    state.currentView = ui::state::ViewId::GATE_SET;

    // A dirty draft that diverges from the committed config.
    state.gateSetDraft.steps = 32;
    state.gateSetDraft.pulses = 8;
    state.gateSetDirty = true;

    commands::g_commandCalls.clear();
    const ui::state::UIState next = ui::state::reduce(state, press(ui::KeyId::F4), &view);

    check(next.currentView == ui::state::ViewId::MODULATION,
          "F4 from GATE_SET navigates to MODULATION");
    check(next.gateSetDirty == false,
          "leaving GATE_SET via F4 restores (discards) the dirty draft");
    check(next.gateSetDraft == next.gateSetConfigs[next.selectedPattern],
          "the draft is back to the committed config after the F4 navigation");
}

}  // namespace

int main() {
    test_f4_navigates_to_modulation();
    test_function_key_cycle_includes_modulation();
    test_f4_reserved_consumption();
    test_f5_beyond_table_stays_reserved();
    test_f4_restores_gate_set_draft();

    if (failures == 0) {
        std::printf("test_reducer_f4_navigation: all checks passed\n");
        return 0;
    }
    std::printf("test_reducer_f4_navigation: %d check(s) failed\n", failures);
    return 1;
}
