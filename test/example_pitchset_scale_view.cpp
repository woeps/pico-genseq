// generative-engine-expansion task 8.3 — PitchSet scale controls + membership
// cue (host).
//
// Validates Requirements 8.* and 10.* for the PitchSet view, at the reducer /
// Working-State / persistence seams (the matrix pixels themselves are not
// host-observable, but every decision behind them is):
//   - field navigation reaches the new SCALE / ROOT / QUANTIZE fields;
//   - adjust cycles each (scale 3-state, root wraps 0..11, quantize 4-state);
//   - clear-to-default returns scale=CHROMATIC / root=0 / quantize=NEAREST (R10.3);
//   - edits flow through the draft and commit into Working State (R8.6: a pure
//     pitch-value edit never changes the scale);
//   - the membership-cue DECISION matches common::scale::inScale against the
//     SAME (scale, root) the sequencer uses at emission (R8.4);
//   - the scale fields survive a PersistMapping round-trip (R1.7 / save+load),
//     and a default (CHROMATIC) config maps to genScale 0 (v1-compatible).
//
// Compiles the real Reducer.cpp + PersistMapping.cpp with host-safe commands::
// seam stubs; links genseq_emission for common::scale + the enums. Own main(),
// excluded from the glob.

#include "ui/state/Reducer.h"
#include "ui/state/UIState.h"
#include "ui/state/PersistMapping.h"
#include "common/Scale.h"
#include "commands/command.h"

#include <cstdint>
#include <cstdio>
#include <vector>

// --- Host-safe commands:: seams (definitions Reducer.cpp links against) ------
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
    beginPitchSetEdit(s);
    return s;
}

// Navigation reaches SCALE/ROOT/QUANTIZE to the LEFT of COUNT.
void test_navigation_reaches_scale_fields() {
    UIState s = freshState();
    // beginPitchSetEdit lands on COUNT; move left: COUNT -> QUANTIZE -> ROOT -> SCALE.
    check(s.selectedPitchSetField == PITCH_SET_FIELD_COUNT, "edit begins on COUNT");
    movePitchSetField(s, -1);
    check(s.selectedPitchSetField == PITCH_SET_FIELD_QUANTIZE, "left of COUNT is QUANTIZE");
    movePitchSetField(s, -1);
    check(s.selectedPitchSetField == PITCH_SET_FIELD_ROOT, "then ROOT");
    movePitchSetField(s, -1);
    check(s.selectedPitchSetField == PITCH_SET_FIELD_SCALE, "then SCALE (leftmost)");
    movePitchSetField(s, -1);
    check(s.selectedPitchSetField == PITCH_SET_FIELD_SCALE, "SCALE is the left clamp");
}

// Adjusting SCALE cycles CHROMATIC -> MAJOR -> MINOR and clamps.
void test_scale_cycle_and_clear() {
    UIState s = freshState();
    s.selectedPitchSetField = PITCH_SET_FIELD_SCALE;
    check(s.pitchSetDraft.scale == common::ScaleId::CHROMATIC, "scale default CHROMATIC");
    adjustPitchSetValue(s, 1, false);
    check(s.pitchSetDraft.scale == common::ScaleId::MAJOR, "scale +1 -> MAJOR");
    adjustPitchSetValue(s, 1, false);
    check(s.pitchSetDraft.scale == common::ScaleId::MINOR_NATURAL, "scale +1 -> MINOR");
    adjustPitchSetValue(s, 1, false);
    check(s.pitchSetDraft.scale == common::ScaleId::MINOR_NATURAL, "scale clamps at MINOR");
    // Clear-to-default: step back down to CHROMATIC (R10.3).
    adjustPitchSetValue(s, -1, false);
    adjustPitchSetValue(s, -1, false);
    check(s.pitchSetDraft.scale == common::ScaleId::CHROMATIC, "scale clears to CHROMATIC");
}

// ROOT wraps 0..11.
void test_root_wraps() {
    UIState s = freshState();
    s.selectedPitchSetField = PITCH_SET_FIELD_ROOT;
    check(s.pitchSetDraft.scaleRoot == 0, "root default 0");
    adjustPitchSetValue(s, -1, false);
    check(s.pitchSetDraft.scaleRoot == 11, "root -1 wraps to 11");
    adjustPitchSetValue(s, 1, false);
    check(s.pitchSetDraft.scaleRoot == 0, "root +1 wraps back to 0");
}

// QUANTIZE cycles NEAREST/DOWN/UP/SKIP and clamps.
void test_quantize_cycle() {
    UIState s = freshState();
    s.selectedPitchSetField = PITCH_SET_FIELD_QUANTIZE;
    check(s.pitchSetDraft.quantize == common::QuantizeMode::NEAREST, "quantize default NEAREST");
    adjustPitchSetValue(s, 1, false);
    adjustPitchSetValue(s, 1, false);
    adjustPitchSetValue(s, 1, false);
    check(s.pitchSetDraft.quantize == common::QuantizeMode::SKIP, "quantize steps to SKIP");
    adjustPitchSetValue(s, 1, false);
    check(s.pitchSetDraft.quantize == common::QuantizeMode::SKIP, "quantize clamps at SKIP");
}

// A pure pitch-value edit must not change the scale (R8.6).
void test_pitch_edit_leaves_scale() {
    UIState s = freshState();
    s.selectedPitchSetField = PITCH_SET_FIELD_SCALE;
    adjustPitchSetValue(s, 1, false);  // MAJOR
    s.selectedPitchSetField = PITCH_SET_FIELD_PITCH_BASE;  // first pitch
    adjustPitchSetValue(s, 1, false);  // nudge pitch
    check(s.pitchSetDraft.scale == common::ScaleId::MAJOR, "pitch edit leaves scale unchanged");
    commitPitchSetEdit(s);
    check(s.pitchSetConfigs[0].scale == common::ScaleId::MAJOR, "commit persists scale to config");
}

// The cue decision matches common::scale::inScale against the draft's scale/root
// (R8.4 — view and emission agree).
void test_cue_matches_scale_library() {
    UIState s = freshState();
    s.selectedPitchSetField = PITCH_SET_FIELD_SCALE;
    adjustPitchSetValue(s, 1, false);  // MAJOR, root 0 (C major)
    const PitchSetConfig& d = s.pitchSetDraft;
    const uint16_t mask = common::scale::membershipMask(d.scale, d.scaleRoot);
    check(common::scale::inScale(mask, 60), "C in C major (cue would show in-scale)");
    check(!common::scale::inScale(mask, 61), "C# out of C major (cue would show out-of-scale)");
    check(common::scale::inScale(mask, 67), "G in C major");
}

// Scale survives the PersistMapping round-trip (R1.7), and a default config maps
// to genScale 0 (v1-compatible / behavior-preserving).
void test_persist_roundtrip() {
    UIState s = freshState();
    s.selectedPitchSetField = PITCH_SET_FIELD_SCALE;
    adjustPitchSetValue(s, 1, false);  // MAJOR
    adjustPitchSetValue(s, 1, false);  // MINOR
    s.selectedPitchSetField = PITCH_SET_FIELD_ROOT;
    adjustPitchSetValue(s, 9, false);  // root A (9)
    s.selectedPitchSetField = PITCH_SET_FIELD_QUANTIZE;
    adjustPitchSetValue(s, 3, false);  // SKIP
    commitPitchSetEdit(s);

    const persistence::PersistableConfig cfg = toPersistableConfig(s);
    check(cfg.patterns[0].genScale == static_cast<uint8_t>(common::ScaleId::MINOR_NATURAL),
          "mapping stores scale");
    check(cfg.patterns[0].genScaleRoot == 9, "mapping stores root");
    check(cfg.patterns[0].genQuantize == static_cast<uint8_t>(common::QuantizeMode::SKIP),
          "mapping stores quantize");

    UIState loaded;
    applyPersistableConfig(loaded, cfg);
    check(loaded.pitchSetConfigs[0].scale == common::ScaleId::MINOR_NATURAL,
          "load restores scale");
    check(loaded.pitchSetConfigs[0].scaleRoot == 9, "load restores root");
    check(loaded.pitchSetConfigs[0].quantize == common::QuantizeMode::SKIP,
          "load restores quantize");

    // A default (untouched) config maps to CHROMATIC/0/NEAREST == 0/0/0.
    UIState def = freshState();
    commitPitchSetEdit(def);
    const persistence::PersistableConfig dcfg = toPersistableConfig(def);
    check(dcfg.patterns[0].genScale == 0 && dcfg.patterns[0].genScaleRoot == 0 &&
          dcfg.patterns[0].genQuantize == 0,
          "default scale maps to 0/0/0 (v1-compatible)");
}

}  // namespace

int main() {
    test_navigation_reaches_scale_fields();
    test_scale_cycle_and_clear();
    test_root_wraps();
    test_quantize_cycle();
    test_pitch_edit_leaves_scale();
    test_cue_matches_scale_library();
    test_persist_roundtrip();

    if (failures == 0) {
        std::printf("example_pitchset_scale_view: all checks passed\n");
        return 0;
    }
    std::printf("example_pitchset_scale_view: %d check(s) failed\n", failures);
    return 1;
}
