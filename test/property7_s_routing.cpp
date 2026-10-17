// Feature: persist-settings, Property 7: non-CTRL S routes to view
//
// Validates: Requirements 1.3.
//
// Property 7 (design.md "Correctness Properties"): For any modifier bitmask that does NOT have
// the CTRL bit set, a KEY_PRESSED event for key S is routed to the active view for
// view-specific handling and never initiates a Save_Operation.
//
// Concretely: for any `mods` with the CTRL bit cleared, reduce(state, keyPressed(S, mods), &view)
//   - invokes view.handleEvent  (S is not reserved and Ctrl+S is not matched, so it is delegated), and
//   - leaves UIState::saveRequested == false  (plain S must never request a save).
//
// -------------------------------------------------------------------------------------------
// Standalone-target arrangement:
//
// Unlike the pure-codec property tests (which are swept into the shared genseq_property_tests
// runner), this test exercises ui::state::reduce, which lives in Reducer.cpp. Reducer.cpp
// references commands::sendCommand / sendGateSet / sendPitchSet / sendVelocitySet (the multicore
// FIFO seam) and common::GateSet::createEuclidean. To build it host-side WITHOUT pulling in
// command.cpp / the Pico SDK, this file provides host-safe no-op DEFINITIONS of the commands::
// seam functions (matching command.h exactly), and the common:: symbols come from the linked
// genseq_emission library. Because Reducer.cpp + these seams are NOT part of the shared
// genseq_property_tests binary, this file is built as its own dedicated executable
// (genseq_reducer_property_tests) with its own main(), and is excluded from the shared glob.
// -------------------------------------------------------------------------------------------

#include "ui/state/Reducer.h"
#include "ui/state/UIState.h"
#include "ui/Event.h"
#include "ui/Types.h"
#include "ui/views/IView.h"
#include "commands/command.h"

#include <rapidcheck.h>

#include <cstdint>

// ---------------------------------------------------------------------------
// Host-safe commands:: seams (definitions Reducer.cpp links against).
//
// Signatures MUST match command.h exactly. These are no-ops: the property only
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

}  // namespace

bool runPersistProperty7() {
    // Property 7: any S press whose modifier bitmask lacks the CTRL bit is delegated to the
    // active view and never sets saveRequested. RapidCheck runs >=100 cases by default.
    return rc::check(
        "Property 7: a KEY_PRESSED S with any non-CTRL modifier bitmask routes to the active "
        "view and does not request a save",
        [] {
            // Draw an arbitrary modifier byte, then clear the CTRL bit so CTRL is guaranteed
            // off while SHIFT / ALT / GUI and other bits may be set.
            const uint8_t rawMods = static_cast<uint8_t>(*rc::gen::inRange<int>(0, 256));
            const uint8_t mods = static_cast<uint8_t>(rawMods & ~ui::mod::CTRL);
            const uint32_t ts = static_cast<uint32_t>(*rc::gen::inRange<int64_t>(0, 1LL << 32));

            RC_ASSERT((mods & ui::mod::CTRL) == 0);  // sanity: CTRL is definitely cleared

            const ui::events::Event event =
                ui::events::Event::keyPressed(ui::KeyId::S, mods, ts);

            RecordingView view;
            const ui::state::UIState state{};  // defaults: saveRequested == false

            const ui::state::UIState result = ui::state::reduce(state, event, &view);

            // S is not reserved and Ctrl+S is not matched, so it must reach the view...
            RC_ASSERT(view.handleEventCalled);
            // ...and a plain S must never request a save.
            RC_ASSERT(result.saveRequested == false);
        });
}

int main() {
    return runPersistProperty7() ? 0 : 1;
}
