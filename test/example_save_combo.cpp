// Feature: persist-settings, Example: Ctrl+S recognition and debounce (Req 1.1, 1.2, 1.4)
//
// Validates: Requirements 1.1, 1.2, 1.4.
//
// This is an EXAMPLE test (not property-based). It exercises ui::state::reduce (Reducer.cpp)
// directly with a small RecordingView so it can observe both the resulting UIState and whether
// the event was routed to the active view.
//
// Cases:
//   A (Req 1.1, 1.2): reduce(defaultState, keyPressed(S, mod::CTRL, ts), &view) recognizes the
//       Save_Combo, sets saveRequested == true, and consumes the event GLOBALLY so it is never
//       routed to the active view (view.handleEventCalled stays false).
//   B (Req 1.3 sanity): reduce(defaultState, keyPressed(S, mod::NONE, ts), &view) does NOT set
//       saveRequested and DOES route to the active view (view.handleEventCalled == true). This
//       overlaps Property 7 but is included here as a contrasting example.
//   C (Req 1.4 debounce, reducer contract): starting from a state that ALREADY has
//       saveRequested == true (modeling a save already requested / in progress), a second
//       Ctrl+S leaves saveRequested == true (idempotent — re-setting the flag is harmless) and
//       is still consumed globally (view.handleEventCalled stays false).
//
//       IMPORTANT: the actual in-progress SUPPRESSION (not launching a *second* save while one
//       is running) is NOT enforced in the pure reducer. It lives in
//       UIController::drainSaveRequest, guarded by the saveInProgress flag (see
//       src/ui/UIController.cpp): drainSaveRequest early-returns when `saveInProgress` is set,
//       so a second Ctrl+S that merely re-sets saveRequested does not launch a concurrent save.
//       That path needs the UI loop + PersistenceManager and is covered by on-target /
//       integration checks, not this host example. This example verifies ONLY the reducer's
//       role: Ctrl+S is ALWAYS consumed globally and only ever SETS the saveRequested flag
//       (idempotently). The assertions here stay accurate to what the reducer actually does —
//       they do not assert any suppression behavior the reducer does not have.
//
// -------------------------------------------------------------------------------------------
// Standalone-target arrangement (mirrors property7_s_routing.cpp / task 6.4):
//
// ui::state::reduce lives in Reducer.cpp, which references commands::sendCommand / sendGateSet /
// sendPitchSet / sendVelocitySet (the multicore FIFO seam) and common:: symbols. To build it
// host-side WITHOUT pulling in command.cpp / the Pico SDK, this file provides host-safe no-op
// DEFINITIONS of the commands:: seam functions (matching command.h exactly); the common::
// symbols come from the linked genseq_emission library. This file is built as its own dedicated
// executable (genseq_example_save_combo) with its own main(), and is excluded from the shared
// property-test glob. It does NOT need RapidCheck (it is an example, not a property test).
// -------------------------------------------------------------------------------------------

#include "ui/state/Reducer.h"
#include "ui/state/UIState.h"
#include "ui/Event.h"
#include "ui/Types.h"
#include "ui/views/IView.h"
#include "commands/command.h"

#include <cstdint>
#include <cstdio>
#include <vector>

// ---------------------------------------------------------------------------
// Host-safe commands:: seams (definitions Reducer.cpp links against).
//
// Signatures MUST match command.h exactly. These are no-ops: the example only
// observes routing/saveRequested, never any inter-core effect, so the FIFO is
// not needed host-side and command.cpp is not compiled.
// ---------------------------------------------------------------------------
namespace commands {

void sendCommand(Command /*cmd*/, uint8_t /*param1*/, uint8_t /*param2*/) {
    // No-op host double.
}

void sendGateSet(uint8_t /*patternIndex*/, const std::vector<bool>& /*gates*/) {
    // No-op host double.
}

void sendPitchSet(uint8_t /*patternIndex*/, uint8_t /*count*/,
                  common::PlayingOrder /*order*/, const std::vector<uint8_t>& /*pitches*/) {
    // No-op host double.
}

void sendVelocitySet(uint8_t /*patternIndex*/, uint8_t /*count*/,
                     common::PlayingOrder /*order*/,
                     const std::vector<uint8_t>& /*velocities*/) {
    // No-op host double.
}

}  // namespace commands

namespace {

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

// Reports one failed check and flips the shared pass flag.
void expect(bool cond, const char* what, bool& ok) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ok = false;
    }
}

}  // namespace

int main() {
    using namespace ui;

    bool ok = true;

    // ---- Case A (Req 1.1, 1.2): Ctrl+S is recognized and consumed globally. ----
    {
        RecordingView view;
        const state::UIState state{};  // defaults: saveRequested == false
        const events::Event event =
            events::Event::keyPressed(KeyId::S, mod::CTRL, /*ts=*/1000);

        const state::UIState result = state::reduce(state, event, &view);

        // The Save_Combo sets the request flag...
        expect(result.saveRequested == true,
               "Case A: Ctrl+S did not set saveRequested", ok);
        // ...and is consumed globally, never routed to the active view.
        expect(view.handleEventCalled == false,
               "Case A: Ctrl+S was routed to the active view (should be consumed globally)", ok);
    }

    // ---- Case B (Req 1.3 sanity): plain S (no CTRL) routes to the view, no save. ----
    {
        RecordingView view;
        const state::UIState state{};  // defaults: saveRequested == false
        const events::Event event =
            events::Event::keyPressed(KeyId::S, mod::NONE, /*ts=*/2000);

        const state::UIState result = state::reduce(state, event, &view);

        // A plain S must never request a save...
        expect(result.saveRequested == false,
               "Case B: plain S (no CTRL) set saveRequested", ok);
        // ...and it must reach the active view for view-specific handling.
        expect(view.handleEventCalled == true,
               "Case B: plain S (no CTRL) was not routed to the active view", ok);
    }

    // ---- Case C (Req 1.4): idempotent flag-set when a save is already requested. ----
    // Model a save already requested/in progress by starting from a state whose
    // saveRequested flag is already set. The reducer must (1) keep saveRequested
    // true (setting it again is harmless) and (2) still consume the event globally.
    // The actual "don't launch a second save while one runs" suppression lives in
    // UIController::drainSaveRequest via saveInProgress — see the file header comment.
    {
        RecordingView view;
        state::UIState state{};
        state.saveRequested = true;  // a save has already been requested / is in progress
        const events::Event event =
            events::Event::keyPressed(KeyId::S, mod::CTRL, /*ts=*/3000);

        const state::UIState result = state::reduce(state, event, &view);

        // Idempotent: re-setting the flag while it is already set is harmless.
        expect(result.saveRequested == true,
               "Case C: second Ctrl+S did not leave saveRequested set (idempotency broken)", ok);
        // Still consumed globally: never routed to the active view.
        expect(view.handleEventCalled == false,
               "Case C: second Ctrl+S was routed to the active view (should be consumed globally)",
               ok);
    }

    if (!ok) {
        std::fprintf(stderr,
                     "FAIL: Ctrl+S recognition/debounce example did not match the reducer "
                     "contract (Req 1.1, 1.2, 1.4)\n");
        return 1;
    }

    std::printf("PASS: Ctrl+S is recognized and consumed globally, plain S routes to the view, "
                "and re-requesting a save is idempotent (Req 1.1, 1.2, 1.4)\n");
    return 0;
}
