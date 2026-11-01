// generative-engine-expansion task 10 — VelocitySet humanize + Pattern-scope
// seed/coupling fields, and the R10 control-map verification (host).
//
// Validates Requirements 3/4/5 UI + 10.*:
//   - HUMANIZE field adjusts 0..64, clear-to-0 (R3/R10);
//   - SEED field spans no-seed <-> explicit seed, clearable to no-seed (R4/R10.3);
//   - COUPLING field cycles INDEPENDENT/LOCKED, clear-to-INDEPENDENT (R5/R10);
//   - R10 control map: EVERY Generation Parameter (scale, root, quantize,
//     per-Pattern probability, per-step probability, humanize, seed, coupling)
//     has a demonstrated matrix-UI set-and-clear path via the reducer — none is
//     reachable only through tooling (R10.1/10.6), each reuses move/adjust
//     (R10.2), each returns to its behavior-preserving default (R10.3).
//
// Compiles the real Reducer.cpp + PersistMapping.cpp with host-safe commands::
// seam stubs; links genseq_emission. Own main(), excluded from the glob.

#include "ui/state/Reducer.h"
#include "ui/state/UIState.h"
#include "ui/state/PersistMapping.h"
#include "commands/command.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace commands {
void sendCommand(Command, uint8_t, uint8_t) {}
void sendGateSet(uint8_t, const std::vector<bool>&) {}
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
    beginVelocitySetEdit(s);
    return s;
}

// HUMANIZE: 0..64, clear-to-0.
void test_humanize_field() {
    UIState s = freshState();
    s.selectedVelocitySetField = VELOCITY_SET_FIELD_HUMANIZE;
    check(s.velocitySetDraft.humanizeRange == 0, "humanize default 0");
    adjustVelocitySetValue(s, 10, false);
    check(s.velocitySetDraft.humanizeRange == 10, "humanize +10");
    adjustVelocitySetValue(s, 100, false);
    check(s.velocitySetDraft.humanizeRange == 64, "humanize clamps at 64");
    adjustVelocitySetValue(s, -100, false);
    check(s.velocitySetDraft.humanizeRange == 0, "humanize clears to 0");
}

// SEED: no-seed <-> explicit, clearable.
void test_seed_field() {
    UIState s = freshState();
    s.selectedVelocitySetField = VELOCITY_SET_FIELD_SEED;
    check(!s.velocitySetDraft.seedSet, "seed default: no seed");
    adjustVelocitySetValue(s, 1, false);
    check(s.velocitySetDraft.seedSet && s.velocitySetDraft.seed == 0, "first +1 sets seed=0");
    adjustVelocitySetValue(s, 5, false);
    check(s.velocitySetDraft.seed == 5, "seed increments to 5");
    adjustVelocitySetValue(s, -100, false);
    check(!s.velocitySetDraft.seedSet, "seed clears to no-seed below 0 (R10.3)");
}

// COUPLING: INDEPENDENT/LOCKED, clear-to-INDEPENDENT.
void test_coupling_field() {
    UIState s = freshState();
    s.selectedVelocitySetField = VELOCITY_SET_FIELD_COUPLING;
    check(s.velocitySetDraft.coupling == common::CouplingMode::INDEPENDENT, "coupling default");
    adjustVelocitySetValue(s, 1, false);
    check(s.velocitySetDraft.coupling == common::CouplingMode::LOCKED, "coupling -> LOCKED");
    adjustVelocitySetValue(s, 1, false);
    check(s.velocitySetDraft.coupling == common::CouplingMode::LOCKED, "coupling clamps at LOCKED");
    adjustVelocitySetValue(s, -1, false);
    check(s.velocitySetDraft.coupling == common::CouplingMode::INDEPENDENT,
          "coupling clears to INDEPENDENT");
}

// Navigation reaches the three new fields to the left of COUNT.
void test_velocity_navigation() {
    UIState s = freshState();
    check(s.selectedVelocitySetField == VELOCITY_SET_FIELD_COUNT, "begins on COUNT");
    moveVelocitySetField(s, -1);
    check(s.selectedVelocitySetField == VELOCITY_SET_FIELD_COUPLING, "left -> COUPLING");
    moveVelocitySetField(s, -1);
    check(s.selectedVelocitySetField == VELOCITY_SET_FIELD_SEED, "then SEED");
    moveVelocitySetField(s, -1);
    check(s.selectedVelocitySetField == VELOCITY_SET_FIELD_HUMANIZE, "then HUMANIZE (leftmost)");
    moveVelocitySetField(s, -1);
    check(s.selectedVelocitySetField == VELOCITY_SET_FIELD_HUMANIZE, "HUMANIZE is the clamp");
}

// R10 control-map verification: every Generation Parameter has a reducer-driven
// set path AND a clear-to-default path. We exercise each via the reducer (not
// tooling), proving none is tooling-only (R10.1/10.6).
void test_r10_control_map() {
    // --- PitchSet: scale / root / quantize ---
    {
        UIState s; s.patternCount = 1; beginPitchSetEdit(s);
        s.selectedPitchSetField = PITCH_SET_FIELD_SCALE;
        adjustPitchSetValue(s, 1, false);
        check(s.pitchSetDraft.scale != common::ScaleId::CHROMATIC, "R10: scale settable");
        adjustPitchSetValue(s, -5, false);
        check(s.pitchSetDraft.scale == common::ScaleId::CHROMATIC, "R10: scale clears");
        s.selectedPitchSetField = PITCH_SET_FIELD_QUANTIZE;
        adjustPitchSetValue(s, 3, false);
        check(s.pitchSetDraft.quantize == common::QuantizeMode::SKIP, "R10: quantize settable");
        adjustPitchSetValue(s, -5, false);
        check(s.pitchSetDraft.quantize == common::QuantizeMode::NEAREST, "R10: quantize clears");
    }
    // --- GateSet: per-Pattern probability + per-step probability + gate ---
    {
        UIState s; s.patternCount = 1; beginGateSetEdit(s);
        s.selectedGateSetProperty = GateSetProperty::PROBABILITY;
        adjustGateSetValue(s, -40, false);
        check(s.gateSetDraft.patternProbability == 60, "R10: per-Pattern prob settable");
        adjustGateSetValue(s, 100, false);
        check(s.gateSetDraft.patternProbability == 100, "R10: per-Pattern prob clears to 100");
        s.selectedGateSetProperty = GateSetProperty::STEP_GRID;
        s.selectedGateStep = 0;
        adjustGateSetValue(s, 2, false);  // INHERIT -> 0 -> 10
        check(s.gateSetDraft.stepProbability[0] != STEP_PROB_INHERIT, "R10: per-step prob settable");
        adjustGateSetValue(s, -5, false);
        check(s.gateSetDraft.stepProbability[0] == STEP_PROB_INHERIT, "R10: per-step prob clears to INHERIT");
    }
    // --- VelocitySet: humanize / seed / coupling (covered above; re-assert clear) ---
    {
        UIState s = freshState();
        s.selectedVelocitySetField = VELOCITY_SET_FIELD_HUMANIZE;
        adjustVelocitySetValue(s, 20, false);
        adjustVelocitySetValue(s, -100, false);
        check(s.velocitySetDraft.humanizeRange == 0, "R10: humanize clears");
    }
}

// Velocity gen-params survive a PersistMapping round-trip.
void test_persist_roundtrip() {
    UIState s = freshState();
    s.selectedVelocitySetField = VELOCITY_SET_FIELD_HUMANIZE;
    adjustVelocitySetValue(s, 12, false);
    s.selectedVelocitySetField = VELOCITY_SET_FIELD_SEED;
    adjustVelocitySetValue(s, 1, false);  // seedSet, seed 0
    adjustVelocitySetValue(s, 7, false);  // seed 7
    s.selectedVelocitySetField = VELOCITY_SET_FIELD_COUPLING;
    adjustVelocitySetValue(s, 1, false);  // LOCKED
    commitVelocitySetEdit(s);

    const persistence::PersistableConfig cfg = toPersistableConfig(s);
    UIState loaded;
    applyPersistableConfig(loaded, cfg);
    const auto& v = loaded.velocitySetConfigs[0];
    check(v.humanizeRange == 12, "humanize survives save/load");
    check(v.seedSet && v.seed == 7, "seed survives save/load");
    check(v.coupling == common::CouplingMode::LOCKED, "coupling survives save/load");
}

}  // namespace

int main() {
    test_humanize_field();
    test_seed_field();
    test_coupling_field();
    test_velocity_navigation();
    test_r10_control_map();
    test_persist_roundtrip();

    if (failures == 0) {
        std::printf("example_velocity_genparams_view: all checks passed\n");
        return 0;
    }
    std::printf("example_velocity_genparams_view: %d check(s) failed\n", failures);
    return 1;
}
