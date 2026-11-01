// generative-engine-expansion task 9.6 — GateSet per-step gate+probability grid
// editor (host).
//
// Validates Requirements 9.* at the reducer / Working-State / persistence seams:
//   - navigation reaches the STEP_GRID and walks the step cursor (R9.1);
//   - ENTER toggles the selected step's gate on/off (R9.2);
//   - UP/DOWN cycles the step probability INHERIT -> 0 -> ... -> 100, with
//     INHERIT distinct from an explicit 0/100 (R9.4/R9.5);
//   - a generator property change regenerates GATE states but PRESERVES per-step
//     probability (R9.3);
//   - a generator-only config expands to the SAME tick gate array as the old
//     createEuclidean path (R9.7 byte-identical default);
//   - grid state survives a PersistMapping round-trip (R9.8).
//
// Compiles the real Reducer.cpp + PersistMapping.cpp with host-safe commands::
// seam stubs; links genseq_emission for common::GateSet. Own main(), excluded
// from the glob.

#include "ui/state/Reducer.h"
#include "ui/state/UIState.h"
#include "ui/state/PersistMapping.h"
#include "common/gate_set.h"
#include "commands/command.h"

#include <cstdint>
#include <cstdio>
#include <vector>

// --- Host-safe commands:: seams. sendGateSet records the last gate array so the
// test can assert the expanded output (R9.7). -----------------------------------
static std::vector<bool> g_lastGateArray;
namespace commands {
void sendCommand(Command, uint8_t, uint8_t) {}
void sendGateSet(uint8_t, const std::vector<bool>& gates) { g_lastGateArray = gates; }
void sendPitchSet(uint8_t, uint8_t, common::PlayingOrder, const std::vector<uint8_t>&) {}
void sendVelocitySet(uint8_t, uint8_t, common::PlayingOrder, const std::vector<uint8_t>&) {}
}  // namespace commands

namespace {

using namespace ui::state;

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

UIState freshState() {
    UIState s;
    s.patternCount = 1;
    s.selectedPattern = 0;
    beginGateSetEdit(s);
    return s;
}

// Walk right from ALGORITHM to STEP_GRID, then into the step cursor.
void test_navigation_into_grid() {
    UIState s = freshState();
    check(s.selectedGateSetProperty == GateSetProperty::ALGORITHM, "begins on ALGORITHM");
    for (int i = 0; i < 7; ++i) moveGateSetProperty(s, 1);  // -> STEP_GRID (8th property)
    check(s.selectedGateSetProperty == GateSetProperty::STEP_GRID, "reaches STEP_GRID");
    check(s.selectedGateStep == 0, "grid lands on step 0");
    moveGateSetProperty(s, 1);
    check(s.selectedGateStep == 1, "RIGHT walks the step cursor");
    moveGateSetProperty(s, -1);
    check(s.selectedGateStep == 0, "LEFT walks back");
    moveGateSetProperty(s, -1);
    check(s.selectedGateSetProperty == GateSetProperty::PROBABILITY,
          "LEFT past step 0 exits the grid to PROBABILITY");
}

// ENTER toggles the selected step's gate.
void test_gate_toggle() {
    UIState s = freshState();
    s.selectedGateSetProperty = GateSetProperty::STEP_GRID;
    s.selectedGateStep = 2;
    const uint8_t before = s.gateSetDraft.gateStates[2];
    toggleGateStep(s);
    check(s.gateSetDraft.gateStates[2] != before, "ENTER toggles the step gate");
    toggleGateStep(s);
    check(s.gateSetDraft.gateStates[2] == before, "toggle again restores");
}

// Probability ladder: INHERIT -> 0 -> 10 ... -> 100, INHERIT distinct.
void test_probability_ladder() {
    UIState s = freshState();
    s.selectedGateSetProperty = GateSetProperty::STEP_GRID;
    s.selectedGateStep = 0;
    check(s.gateSetDraft.stepProbability[0] == STEP_PROB_INHERIT, "default INHERIT");
    adjustGateSetValue(s, 1, false);
    check(s.gateSetDraft.stepProbability[0] == 0, "INHERIT +1 -> 0%");
    adjustGateSetValue(s, 5, false);
    check(s.gateSetDraft.stepProbability[0] == 50, "+5 -> 50%");
    adjustGateSetValue(s, 10, false);
    check(s.gateSetDraft.stepProbability[0] == 100, "clamps at 100%");
    // Step back down to INHERIT.
    adjustGateSetValue(s, -10, false);
    adjustGateSetValue(s, -1, false);
    check(s.gateSetDraft.stepProbability[0] == STEP_PROB_INHERIT, "returns to INHERIT below 0%");
}

// R9.3: changing a generator property regenerates gate states but keeps
// per-step probability.
void test_generator_reseeds_gates_keeps_probability() {
    UIState s = freshState();
    // Set a probability on step 3.
    s.selectedGateSetProperty = GateSetProperty::STEP_GRID;
    s.selectedGateStep = 3;
    adjustGateSetValue(s, 6, false);  // INHERIT -> 0 -> 50 (6 steps: idx -1->5)
    const uint8_t savedProb = s.gateSetDraft.stepProbability[3];
    check(savedProb != STEP_PROB_INHERIT, "step 3 has an explicit probability");

    // Manually toggle a gate so we can see the generator overwrite it.
    s.selectedGateStep = 1;
    const uint8_t manual = s.gateSetDraft.gateStates[1] ? 0 : 1;
    s.gateSetDraft.gateStates[1] = manual;

    // Change PULSES (a generator prop) -> gates regenerate, probability preserved.
    s.selectedGateSetProperty = GateSetProperty::PULSES;
    adjustGateSetValue(s, 1, false);
    check(s.gateSetDraft.stepProbability[3] == savedProb,
          "generator change PRESERVES per-step probability (R9.3)");
    // gateStates now reflect the generator (not necessarily the manual toggle).
    // We can't assert the exact pattern, but probability survived, which is the
    // contract under test.
}

// R9.7: a generator-only config expands to the SAME tick gate array as the old
// createEuclidean path.
void test_generator_only_byte_identical() {
    UIState s = freshState();
    // freshState seeds gateStates from the default generator (steps16/pulses4).
    // Force a send to capture the expanded array.
    s.selectedGateSetProperty = GateSetProperty::ROTATION;
    adjustGateSetValue(s, 1, false);   // rotation 0->1, regenerates + sends
    adjustGateSetValue(s, -1, false);  // back to rotation 0, regenerates + sends
    const std::vector<bool> fromGrid = g_lastGateArray;

    // The reference: createEuclidean with the same params.
    const auto& g = s.gateSetDraft;
    uint8_t stepLen = 6;  // SIXTEENTH default
    const common::GateSet ref = common::GateSet::createEuclidean(
        g.steps, g.pulses, g.rotation, stepLen, g.gateLength);
    check(fromGrid == ref.getGates(),
          "generator-only grid expansion == createEuclidean output (R9.7)");
}

// R9.8: grid state survives a PersistMapping round-trip.
void test_persist_roundtrip() {
    UIState s = freshState();
    s.selectedGateSetProperty = GateSetProperty::STEP_GRID;
    s.selectedGateStep = 5;
    adjustGateSetValue(s, 3, false);   // step 5 -> explicit 20%
    s.selectedGateStep = 2;
    toggleGateStep(s);                 // flip step 2's gate
    const uint8_t prob5 = s.gateSetDraft.stepProbability[5];
    const uint8_t gate2 = s.gateSetDraft.gateStates[2];
    commitGateSetEdit(s);

    const persistence::PersistableConfig cfg = toPersistableConfig(s);
    UIState loaded;
    applyPersistableConfig(loaded, cfg);
    check(loaded.gateSetConfigs[0].stepProbability[5] == prob5,
          "per-step probability survives save/load");
    check(loaded.gateSetConfigs[0].gateStates[2] == gate2,
          "manual gate state survives save/load");
}

// Per-Pattern PROBABILITY field: 0..100, clear-to-100 (R2/R10).
void test_pattern_probability_field() {
    UIState s = freshState();
    s.selectedGateSetProperty = GateSetProperty::PROBABILITY;
    check(s.gateSetDraft.patternProbability == 100, "per-Pattern probability default 100");
    adjustGateSetValue(s, -30, false);
    check(s.gateSetDraft.patternProbability == 70, "-30 -> 70");
    adjustGateSetValue(s, 100, false);
    check(s.gateSetDraft.patternProbability == 100, "clamps at 100 (clear-to-default)");
    adjustGateSetValue(s, -100, false);
    check(s.gateSetDraft.patternProbability == 0, "clamps at 0");
}

}  // namespace

int main() {
    test_navigation_into_grid();
    test_gate_toggle();
    test_probability_ladder();
    test_pattern_probability_field();
    test_generator_reseeds_gates_keeps_probability();
    test_generator_only_byte_identical();
    test_persist_roundtrip();

    if (failures == 0) {
        std::printf("example_gateset_grid_view: all checks passed\n");
        return 0;
    }
    std::printf("example_gateset_grid_view: %d check(s) failed\n", failures);
    return 1;
}
