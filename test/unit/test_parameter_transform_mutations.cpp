// generative-engine-expansion task 9 — parameter and transform mutation
// operators (FR5-FR8, AC2, AC4).
//
// Validates the eight concrete IMutationOperator plugins:
//   parameter mutations (ParameterMutations.h):
//     - DensityMutation        (FR7 GATE_DENSITY: the Euclidean pulse count)
//     - PitchSetMutation        (FR7 PITCH_CONTENTS, scale-aware via
//                               common::Scale::nearestNoteInScale — AC2)
//     - VelocitySetMutation     (FR7 VELOCITY_CONTENTS)
//     - DurationMutation        (FR7 NOTE_DURATION: the length material)
//     - EuclideanParamMutation  (FR7 EUCLIDEAN_PARAMS: steps/rotation)
//   transform mutations (TransformMutations.h):
//     - RotateMutation           (pitch / velocity / gate ordering)
//     - InvertMutation           (pitch / velocity / gate ordering)
//     - ShuffleMutation          (pitch / velocity / gate ordering)
//
// Required cases (plan task 9):
//   1. intensity 0 produces no change (NO_CHANGE, byte-identical state);
//   2. higher intensity gives monotonically larger expected change over a
//      fixed seed set;
//   3. scale-locked pitch mutation always yields in-scale notes for ALL
//      built-in templates and roots (AC2);
//   4. distance limits are never exceeded: the operator returns Rejected
//      (restoring the state) or clamps into its documented value ranges;
//   5. invariants hold: preserved pulse count under rotate/shuffle, locked
//      fields untouched (rejection, never a partial edit);
//   6. rotate by N then by -N restores the original;
//   7. invert twice restores the original;
//   8. shuffle is a permutation (contents preserved).
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/ so
// the property-test glob (test/*.cpp) never sweeps it; registered in the host
// test build under the ctest name "test_parameter_transform_mutations". Like
// test_mutation_framework it compiles common/Scale.cpp directly (the
// scale-aware operators need the shared constraint math) and links NO
// library, NO Pico SDK libs and NO RapidCheck.

#include "mutations/ParameterMutations.h"
#include "mutations/TransformMutations.h"

#include "MutationConstraints.h"
#include "MutationResult.h"
#include "PatternGenerativeState.h"

#include "../src/common/Scale.h"
#include "../src/common/ScaleTemplates.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using sequencer::DensityMutation;
using sequencer::DurationMutation;
using sequencer::EuclideanParamMutation;
using sequencer::GeneratorRole;
using sequencer::IMutationOperator;
using sequencer::InvertMutation;
using sequencer::PitchSetMutation;
using sequencer::RotateMutation;
using sequencer::ShuffleMutation;
using sequencer::VelocitySetMutation;

using sequencer::MutationConstraints;
using sequencer::MutationInvariant;
using sequencer::MutationResult;
using sequencer::MutationStatus;
using sequencer::MutationTarget;
using sequencer::PatternGenerativeState;
using sequencer::PatternMutationView;
using sequencer::Prng;

using sequencer::EuclideanGateGenerator;
using sequencer::maskOf;
using sequencer::sameSetContents;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// The fixed seed set every "expected change" average runs over (case 2).
constexpr int kSeedCount = 64;

// A well-formed working state: C-Major pitch material (mid-range, distinct),
// eight velocities, eight durations, and the default Euclidean gate
// configuration (64 steps, 32 pulses — mid-density so density mutations
// never clamp in either direction at moderate intensity).
PatternGenerativeState baseState() {
    PatternGenerativeState s;
    const uint8_t pitches[8] = {60, 62, 64, 65, 67, 69, 71, 72};
    s.pitchSet.count = 8;
    for (uint8_t i = 0; i < 8; ++i) s.pitchSet.values[i] = pitches[i];
    s.velocitySet.count = 8;
    for (uint8_t i = 0; i < 8; ++i) s.velocitySet.values[i] = static_cast<uint8_t>(10 + 10 * i);
    const uint8_t durations[8] = {12, 24, 36, 48, 60, 72, 96, 120};
    s.durationSet.count = 8;
    for (uint8_t i = 0; i < 8; ++i) s.durationSet.values[i] = durations[i];
    s.setGeneratorId(GeneratorRole::GATE, sequencer::kDefaultGateGeneratorId);
    s.setGeneratorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamSteps, 64);
    s.setGeneratorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamPulses, 32);
    s.setGeneratorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamRotation, 0);
    return s;
}

// One bounded apply() call: `state` is both mutated and measured against
// `before` (the normal working-state flow: the view's pre-mutation
// snapshot is a copy of the entry state).
MutationResult runOnce(IMutationOperator& op, PatternGenerativeState& state,
                       const PatternGenerativeState& before,
                       const MutationConstraints& constraints, uint64_t seed) {
    PatternMutationView view(state, before, 2);
    Prng rng(seed);
    return op.apply(view, constraints, rng);
}

// The Hamming distance between the live values of two sets (bounded by
// kGenerativeSetCapacity).
uint8_t hamming(const sequencer::GenerativeValueSet& a,
                const sequencer::GenerativeValueSet& b) {
    const uint8_t live = a.count < b.count ? a.count : b.count;
    uint8_t diff = static_cast<uint8_t>(a.count < b.count ? b.count - a.count
                                                          : a.count - b.count);
    for (uint8_t i = 0; i < live; ++i) {
        if (a.values[i] != b.values[i]) ++diff;
    }
    return diff;
}

// --- (1) operator identity ----------------------------------------------------

void test_identity() {
    DensityMutation density;
    PitchSetMutation pitchMut;
    VelocitySetMutation velMut;
    DurationMutation durMut;
    EuclideanParamMutation euclidMut;
    RotateMutation rotate(3);
    InvertMutation invert;
    ShuffleMutation shuffle;

    check(std::strcmp(density.id(), "builtin.mutate-density") == 0,
          "DensityMutation reports its id");
    check(std::strcmp(pitchMut.id(), "builtin.mutate-pitch-set") == 0,
          "PitchSetMutation reports its id");
    check(std::strcmp(velMut.id(), "builtin.mutate-velocity-set") == 0,
          "VelocitySetMutation reports its id");
    check(std::strcmp(durMut.id(), "builtin.mutate-duration") == 0,
          "DurationMutation reports its id");
    check(std::strcmp(euclidMut.id(), "builtin.mutate-euclidean") == 0,
          "EuclideanParamMutation reports its id");
    check(std::strcmp(rotate.id(), "builtin.rotate") == 0,
          "RotateMutation reports its id");
    check(std::strcmp(invert.id(), "builtin.invert") == 0,
          "InvertMutation reports its id");
    check(std::strcmp(shuffle.id(), "builtin.shuffle") == 0,
          "ShuffleMutation reports its id");

    check(density.name() != nullptr && pitchMut.name() != nullptr &&
              velMut.name() != nullptr && durMut.name() != nullptr &&
              euclidMut.name() != nullptr && rotate.name() != nullptr &&
              invert.name() != nullptr && shuffle.name() != nullptr,
          "every operator reports a display name");

    // FR8 operator control: each operator declares exactly the FR7 targets
    // it can mutate.
    check(density.supportedTargets() == maskOf(MutationTarget::GATE_DENSITY),
          "DensityMutation supports GATE_DENSITY only");
    check(pitchMut.supportedTargets() == maskOf(MutationTarget::PITCH_CONTENTS),
          "PitchSetMutation supports PITCH_CONTENTS only");
    check(velMut.supportedTargets() == maskOf(MutationTarget::VELOCITY_CONTENTS),
          "VelocitySetMutation supports VELOCITY_CONTENTS only");
    check(durMut.supportedTargets() == maskOf(MutationTarget::NOTE_DURATION),
          "DurationMutation supports NOTE_DURATION only");
    check(euclidMut.supportedTargets() == maskOf(MutationTarget::EUCLIDEAN_PARAMS),
          "EuclideanParamMutation supports EUCLIDEAN_PARAMS only");
    const uint16_t kOrderingMask =
        maskOf(MutationTarget::PITCH_ORDERING) | maskOf(MutationTarget::VELOCITY_ORDERING) |
        maskOf(MutationTarget::EUCLIDEAN_PARAMS);
    check(rotate.supportedTargets() == kOrderingMask,
          "RotateMutation supports pitch/velocity/gate ordering");
    check(invert.supportedTargets() == kOrderingMask,
          "InvertMutation supports pitch/velocity/gate ordering");
    check(shuffle.supportedTargets() == kOrderingMask,
          "ShuffleMutation supports pitch/velocity/gate ordering");
}

// --- (2) intensity 0 produces no change ---------------------------------------

void test_intensity_zero() {
    DensityMutation density;
    PitchSetMutation pitchMut;
    VelocitySetMutation velMut;
    DurationMutation durMut;
    EuclideanParamMutation euclidMut;
    RotateMutation rotate(3);
    InvertMutation invert;
    ShuffleMutation shuffle;
    IMutationOperator* const ops[] = {&density, &pitchMut, &velMut, &durMut,
                                       &euclidMut, &rotate, &invert, &shuffle};

    for (IMutationOperator* op : ops) {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        // Request EVERY target the operator supports so the zero intensity
        // is the only reason nothing happens.
        MutationConstraints c(op->supportedTargets(), 0);
        const MutationResult r = runOnce(*op, working, before, c, 777u);
        check(r.status == MutationStatus::NO_CHANGE,
              "intensity 0 reports NO_CHANGE for every operator");
        check(r.changedTargets == 0, "intensity 0 reports no changed targets");
        check(std::memcmp(&working, &before, sizeof(working)) == 0,
              "intensity 0 leaves the state byte-identical");
    }
}

// --- (3) unsupported targets are rejected without side effects ----------------

void test_unsupported_targets() {
    DensityMutation density;
    PitchSetMutation pitchMut;
    VelocitySetMutation velMut;
    DurationMutation durMut;
    EuclideanParamMutation euclidMut;
    RotateMutation rotate(3);
    InvertMutation invert;
    ShuffleMutation shuffle;
    struct Entry { IMutationOperator* op; uint16_t wrong; const char* what; };
    const Entry entries[] = {
        {&density, maskOf(MutationTarget::PITCH_CONTENTS), "density rejects pitch targets"},
        {&pitchMut, maskOf(MutationTarget::GATE_DENSITY), "pitch-set rejects gate targets"},
        {&velMut, maskOf(MutationTarget::PITCH_CONTENTS), "velocity-set rejects pitch targets"},
        {&durMut, maskOf(MutationTarget::VELOCITY_CONTENTS), "duration rejects velocity targets"},
        {&euclidMut, maskOf(MutationTarget::NOTE_DURATION), "euclidean rejects duration targets"},
        {&rotate, maskOf(MutationTarget::PITCH_CONTENTS), "rotate rejects contents targets"},
        {&invert, maskOf(MutationTarget::GATE_DENSITY), "invert rejects gate-density targets"},
        {&shuffle, maskOf(MutationTarget::NOTE_DURATION), "shuffle rejects duration targets"},
    };
    for (const Entry& e : entries) {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints c(e.wrong, 64);
        const MutationResult r = runOnce(*e.op, working, before, c, 42u);
        check(r.status == MutationStatus::REJECTED, e.what);
        check(r.changedTargets == 0, "an unsupported-target rejection reports no changes");
        check(std::memcmp(&working, &before, sizeof(working)) == 0,
              "an unsupported-target rejection leaves the state untouched");
    }
}

// --- (4) higher intensity gives monotonically larger expected change ----------

// Average |delta pulses| over the fixed seed set at each intensity level.
void test_monotone_density() {
    DensityMutation density;
    const int levels[] = {8, 48, 127};
    double prev = -1.0;
    for (int intensity : levels) {
        uint32_t total = 0;
        for (int seed = 1; seed <= kSeedCount; ++seed) {
            PatternGenerativeState working = baseState();
            const PatternGenerativeState before = working;
            MutationConstraints c(maskOf(MutationTarget::GATE_DENSITY),
                                  static_cast<uint16_t>(intensity));
            const MutationResult r = runOnce(density, working, before, c,
                                             static_cast<uint64_t>(seed));
            check(r.status == MutationStatus::APPLIED,
                  "an unconstrained density mutation applies");
            const int16_t before_ = before.generatorParam(
                GeneratorRole::GATE, EuclideanGateGenerator::kParamPulses);
            const int16_t after = working.generatorParam(
                GeneratorRole::GATE, EuclideanGateGenerator::kParamPulses);
            total += static_cast<uint32_t>(after > before_ ? after - before_ : before_ - after);
        }
        const double avg = static_cast<double>(total) / kSeedCount;
        check(avg >= prev, "density: expected change grows with intensity");
        prev = avg;
    }
    check(prev > 2.0, "density: intensity 127 moves pulses further than intensity 8");
}

// Generic monotone check for the per-element set mutations (pitch / velocity /
// duration): the average Hamming distance over the fixed seed set grows with
// the intensity level.
void monotoneSetCase(IMutationOperator& op, MutationTarget target, int whichSet,
                     const char* what) {
    const int levels[] = {8, 48, 127};
    double prev = -1.0;
    for (int intensity : levels) {
        uint32_t total = 0;
        for (int seed = 1; seed <= kSeedCount; ++seed) {
            PatternGenerativeState working = baseState();
            const PatternGenerativeState before = working;
            MutationConstraints c(maskOf(target), static_cast<uint16_t>(intensity));
            const MutationResult r = runOnce(op, working, before, c,
                                             static_cast<uint64_t>(seed));
            check(r.status != MutationStatus::REJECTED, what);
            const sequencer::GenerativeValueSet& a =
                whichSet == 0 ? before.pitchSet
                              : (whichSet == 1 ? before.velocitySet : before.durationSet);
            const sequencer::GenerativeValueSet& b =
                whichSet == 0 ? working.pitchSet
                              : (whichSet == 1 ? working.velocitySet : working.durationSet);
            total += hamming(a, b);
        }
        const double avg = static_cast<double>(total) / kSeedCount;
        check(avg >= prev, what);
        prev = avg;
    }
    check(prev > 1.0, what);
}

void test_monotone_sets() {
    PitchSetMutation pitchMut;
    VelocitySetMutation velMut;
    DurationMutation durMut;
    monotoneSetCase(pitchMut, MutationTarget::PITCH_CONTENTS, 0,
                   "pitch-set: expected change grows with intensity");
    monotoneSetCase(velMut, MutationTarget::VELOCITY_CONTENTS, 1,
                    "velocity-set: expected change grows with intensity");
    monotoneSetCase(durMut, MutationTarget::NOTE_DURATION, 2,
                    "duration: expected change grows with intensity");
}

void test_monotone_euclidean() {
    EuclideanParamMutation euclidMut;
    const int levels[] = {8, 48, 127};
    double prev = -1.0;
    for (int intensity : levels) {
        uint32_t total = 0;
        for (int seed = 1; seed <= kSeedCount; ++seed) {
            PatternGenerativeState working = baseState();
            const PatternGenerativeState before = working;
            MutationConstraints c(maskOf(MutationTarget::EUCLIDEAN_PARAMS),
                                  static_cast<uint16_t>(intensity));
            const MutationResult r = runOnce(euclidMut, working, before, c,
                                             static_cast<uint64_t>(seed));
            check(r.status != MutationStatus::REJECTED,
                  "an unconstrained euclidean mutation applies");
            for (uint8_t p = 0; p < 2; ++p) {
                if (before.generatorParam(GeneratorRole::GATE, p) !=
                    working.generatorParam(GeneratorRole::GATE, p)) {
                    ++total;
                }
            }
        }
        const double avg = static_cast<double>(total) / kSeedCount;
        check(avg >= prev, "euclidean: expected change grows with intensity");
        prev = avg;
    }
    check(prev > 0.5, "euclidean: intensity 127 changes more parameters than intensity 8");
}

// Rotate scales the rotation magnitude with intensity: at intensity 8/48/127
// the multiplier is 1/2/4, so the gate rotation moves 1/2/4 steps for amount 1.
void test_monotone_rotate() {
    RotateMutation rotate(1);
    const int levels[] = {8, 48, 127};
    const int expected[] = {1, 2, 4};
    for (int i = 0; i < 3; ++i) {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::EUCLIDEAN_PARAMS),
                              static_cast<uint16_t>(levels[i]));
        const MutationResult r = runOnce(rotate, working, before, c, 7u);
        check(r.status == MutationStatus::APPLIED, "the gate rotation applies");
        const int16_t delta =
            working.generatorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamRotation) -
            before.generatorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamRotation);
        check(delta == expected[i],
              "rotate: the magnitude grows monotonically with intensity");
    }
}

// Invert scales the inverted prefix with intensity: with 8 live values the
// Hamming distance of the velocity sequence is 1 / 4 / 8 at levels 8 / 48 / 127
// (v and 127 - v are always distinct, so every inverted position differs).
void test_monotone_invert() {
    InvertMutation invert;
    const int levels[] = {8, 48, 127};
    const int expected[] = {1, 4, 8};
    for (int i = 0; i < 3; ++i) {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::VELOCITY_ORDERING),
                              static_cast<uint16_t>(levels[i]));
        const MutationResult r = runOnce(invert, working, before, c, 7u);
        check(r.status == MutationStatus::APPLIED, "the velocity inversion applies");
        check(hamming(before.velocitySet, working.velocitySet) == expected[i],
              "invert: the inverted prefix grows monotonically with intensity");
    }
}

// Shuffle performs more random swaps at higher intensity, so the expected
// number of displaced positions grows.
void test_monotone_shuffle() {
    ShuffleMutation shuffle;
    const int levels[] = {8, 48, 127};
    double prev = -1.0;
    for (int intensity : levels) {
        uint32_t total = 0;
        for (int seed = 1; seed <= kSeedCount; ++seed) {
            PatternGenerativeState working = baseState();
            const PatternGenerativeState before = working;
            MutationConstraints c(maskOf(MutationTarget::PITCH_ORDERING),
                                  static_cast<uint16_t>(intensity));
            const MutationResult r = runOnce(shuffle, working, before, c,
                                             static_cast<uint64_t>(seed));
            check(r.status != MutationStatus::REJECTED,
                  "the pitch shuffle applies (a self-swap may report NO_CHANGE)");
            total += hamming(before.pitchSet, working.pitchSet);
        }
        const double avg = static_cast<double>(total) / kSeedCount;
        check(avg >= prev, "shuffle: expected change grows with intensity");
        prev = avg;
    }
    check(prev > 1.0, "shuffle: intensity 127 displaces more than intensity 8");
}

// --- (5) AC2: scale-locked pitch mutation yields in-scale notes ----------------

void test_scale_lock_all_templates() {
    PitchSetMutation pitchMut;
    const int templateCount = common::scaleTemplateCount();
    check(templateCount >= 20 && templateCount <= 30,
          "the built-in scale template table has 20-30 entries");

    // (a) In-scale base material: every applied mutation stays in scale for
    //     EVERY template and root (AC2).
    int appliedRuns = 0;
    for (int t = 0; t < templateCount; ++t) {
        for (int root = 0; root < 12; ++root) {
            const common::Scale scale(static_cast<uint8_t>(root), static_cast<uint8_t>(t));
            PatternGenerativeState s;
            s.pitchSet.count = 8;
            for (uint8_t i = 0; i < 8; ++i) {
                s.pitchSet.values[i] = scale.nearestNoteInScale(static_cast<uint8_t>(i * 16));
            }
            for (int seed = 0; seed < 2; ++seed) {
                PatternGenerativeState working = s;
                const PatternGenerativeState before = working;
                MutationConstraints c(maskOf(MutationTarget::PITCH_CONTENTS), 96,
                                      maskOf(MutationInvariant::SCALE_LOCK));
                c.scale = scale;  // the request's global scale (T16)
                const MutationResult r = runOnce(pitchMut, working, before, c,
                                                 static_cast<uint64_t>(seed + 1));
                check(r.status != MutationStatus::REJECTED,
                      "scale-locked pitch mutation of in-scale material is never rejected");
                if (r.status == MutationStatus::APPLIED) ++appliedRuns;
                for (uint8_t i = 0; i < working.pitchSet.count; ++i) {
                    check(scale.containsNote(working.pitchSet.values[i]),
                          "AC2: every pitch stays in the selected scale");
                }
            }
        }
    }
    check(appliedRuns > 0, "the scale-lock sweep applied mutations");

    // (b) Out-of-scale base material at intensity 127: every value gets
    //     replaced by a snapped in-scale note, so any APPLIED result is fully
    //     in scale (untouched leftovers would be REJECTED by the invariant).
    const common::Scale cMajor(0, 0);
    PatternGenerativeState junk;
    junk.pitchSet.count = 8;
    const uint8_t junkPitches[8] = {61, 63, 66, 68, 70, 73, 75, 78};
    for (uint8_t i = 0; i < 8; ++i) junk.pitchSet.values[i] = junkPitches[i];
    bool anyApplied = false;
    for (int seed = 0; seed < 16; ++seed) {
        PatternGenerativeState working = junk;
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::PITCH_CONTENTS), 127,
                              maskOf(MutationInvariant::SCALE_LOCK));
        const MutationResult r = runOnce(pitchMut, working, before, c,
                                         static_cast<uint64_t>(seed));
        if (r.status == MutationStatus::APPLIED) {
            anyApplied = true;
            for (uint8_t i = 0; i < working.pitchSet.count; ++i) {
                check(cMajor.containsNote(working.pitchSet.values[i]),
                      "AC2: a full-intensity mutation snaps every pitch into the scale");
            }
        } else {
            check(r.status == MutationStatus::REJECTED,
                  "a mutation that would leave out-of-scale pitches behind is rejected");
            check(std::memcmp(&working, &before, sizeof(working)) == 0,
                  "the rejected out-of-scale attempt restored the state");
        }
    }
    check(anyApplied, "some full-intensity mutation was applied");
}

// --- (6) distance limits: reject (and restore) or clamp -----------------------

void test_distance_limits() {
    DensityMutation density;
    PitchSetMutation pitchMut;

    // maxDistance 0 forbids even a one-parameter change: REJECTED + restored.
    {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::GATE_DENSITY), 64, 0, 0);
        const MutationResult r = runOnce(density, working, before, c, 5u);
        check(r.status == MutationStatus::REJECTED,
              "a density mutation breaching maxDistance 0 is rejected");
        check(std::memcmp(&working, &before, sizeof(working)) == 0,
              "the distance rejection restored the pre-mutation state");
    }

    // A full-intensity pitch mutation changes up to 8 positions: maxDistance 1
    // rejects it, the default limit admits it.
    {
        int applied = 0;
        for (int seed = 1; seed <= 16; ++seed) {
            PatternGenerativeState working = baseState();
            const PatternGenerativeState before = working;
            MutationConstraints tight(maskOf(MutationTarget::PITCH_CONTENTS), 127, 0, 1);
            const MutationResult r = runOnce(pitchMut, working, before, tight,
                                             static_cast<uint64_t>(seed));
            if (r.status == MutationStatus::APPLIED) {
                check(sequencer::mutationDistance(before, working) <= 1,
                      "an admitted mutation never exceeds the distance limit");
            } else {
                check(r.status == MutationStatus::REJECTED,
                      "over-limit results are rejected, never partially applied");
                check(std::memcmp(&working, &before, sizeof(working)) == 0,
                      "the distance rejection restored the state");
            }

            PatternGenerativeState working2 = baseState();
            const PatternGenerativeState before2 = working2;
            MutationConstraints loose(maskOf(MutationTarget::PITCH_CONTENTS), 127);
            const MutationResult r2 = runOnce(pitchMut, working2, before2, loose,
                                              static_cast<uint64_t>(seed));
            if (r2.status == MutationStatus::APPLIED) ++applied;
            check(sequencer::mutationDistance(before2, working2) <=
                      loose.maxDistance,
                  "the default limit is respected");
        }
        check(applied > 0, "the default distance limit admits full-intensity mutations");
    }
}

// The parameter mutations CLAMP their results into the documented ranges even
// from arbitrary stored values: steps 1..64, pulses 0..steps, rotation
// 0..steps-1, pitches 0..127, velocities 0..127, durations 1..255 ticks.
void test_clamping() {
    DensityMutation density;
    EuclideanParamMutation euclidMut;
    PitchSetMutation pitchMut;
    VelocitySetMutation velMut;
    DurationMutation durMut;

    // Degenerate stored parameters (out of range) stay clamped after mutation.
    for (int seed = 1; seed <= 32; ++seed) {
        PatternGenerativeState working;
        working.setGeneratorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamSteps, 500);
        working.setGeneratorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamPulses, -3);
        working.setGeneratorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamRotation, 99);
        const PatternGenerativeState before = working;

        MutationConstraints c(maskOf(MutationTarget::GATE_DENSITY) |
                                  maskOf(MutationTarget::EUCLIDEAN_PARAMS),
                              127);
        // One operator per call: run each on a fresh copy.
        PatternGenerativeState w1 = working;
        PatternMutationView v1(w1, before, 0);
        { Prng rng(static_cast<uint64_t>(seed)); density.apply(v1, c, rng); }
        const int16_t steps1 = w1.generatorParam(GeneratorRole::GATE,
                                                  EuclideanGateGenerator::kParamSteps);
        const int16_t pulses1 = w1.generatorParam(GeneratorRole::GATE,
                                                  EuclideanGateGenerator::kParamPulses);
        check(pulses1 >= 0 && pulses1 <= steps1,
              "the pulse count stays clamped into [0, steps]");

        PatternGenerativeState w2 = working;
        PatternMutationView v2(w2, before, 0);
        { Prng rng(static_cast<uint64_t>(seed)); euclidMut.apply(v2, c, rng); }
        const int16_t steps2 = w2.generatorParam(GeneratorRole::GATE,
                                                 EuclideanGateGenerator::kParamSteps);
        const int16_t pulses2 = w2.generatorParam(GeneratorRole::GATE,
                                                 EuclideanGateGenerator::kParamPulses);
        const int16_t rot2 = w2.generatorParam(GeneratorRole::GATE,
                                               EuclideanGateGenerator::kParamRotation);
        check(steps2 >= 1 && steps2 <= 64, "steps stay clamped into [1, 64]");
        check(rot2 >= 0 && rot2 <= steps2 - 1, "rotation stays clamped into [0, steps-1]");
        check(pulses2 == before.generatorParam(GeneratorRole::GATE,
                                              EuclideanGateGenerator::kParamPulses),
              "the euclidean mutation never touches the pulse count");
    }

    // Set mutations: every produced value stays in its documented range.
    for (int seed = 1; seed <= 32; ++seed) {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints cp(maskOf(MutationTarget::PITCH_CONTENTS), 127);
        runOnce(pitchMut, working, before, cp, static_cast<uint64_t>(seed));
        for (uint8_t i = 0; i < working.pitchSet.count; ++i) {
            check(working.pitchSet.values[i] <= 127, "pitches stay MIDI 0-127");
        }
        PatternGenerativeState wv = baseState();
        const PatternGenerativeState bv = wv;
        MutationConstraints cv(maskOf(MutationTarget::VELOCITY_CONTENTS), 127);
        runOnce(velMut, wv, bv, cv, static_cast<uint64_t>(seed));
        for (uint8_t i = 0; i < wv.velocitySet.count; ++i) {
            check(wv.velocitySet.values[i] <= 127, "velocities stay MIDI 0-127");
        }
        PatternGenerativeState wd = baseState();
        const PatternGenerativeState bd = wd;
        MutationConstraints cd(maskOf(MutationTarget::NOTE_DURATION), 127);
        runOnce(durMut, wd, bd, cd, static_cast<uint64_t>(seed));
        for (uint8_t i = 0; i < wd.durationSet.count; ++i) {
            check(wd.durationSet.values[i] >= 1,
                  "durations stay clamped into [1, 255] ticks");
        }
    }
}

// --- (7) invariants: preserved pulse count, locked fields untouched ------------

void test_invariants() {
    RotateMutation rotate(3);
    ShuffleMutation shuffle;
    InvertMutation invert;
    DensityMutation density;
    VelocitySetMutation velMut;
    DurationMutation durMut;
    EuclideanParamMutation euclidMut;

    // Preserved pulse count under rotate / shuffle on the gate target: the
    // transforms only move the rotation, so the pulses parameter is intact.
    for (int seed = 1; seed <= 8; ++seed) {
        for (int intensity = 16; intensity <= 127; intensity += 32) {
            PatternGenerativeState working = baseState();
            const PatternGenerativeState before = working;
            MutationConstraints c(maskOf(MutationTarget::EUCLIDEAN_PARAMS),
                                  static_cast<uint16_t>(intensity),
                                  maskOf(MutationInvariant::PRESERVE_PULSE_COUNT));
            const MutationResult rr = runOnce(rotate, working, before, c,
                                               static_cast<uint64_t>(seed));
            check(rr.status == MutationStatus::APPLIED, "rotation preserves the pulse count");
            check(working.generatorParam(GeneratorRole::GATE,
                                         EuclideanGateGenerator::kParamPulses) ==
                      before.generatorParam(GeneratorRole::GATE,
                                            EuclideanGateGenerator::kParamPulses),
                  "rotate leaves the pulse count untouched");

            working = baseState();
            const MutationResult rs = runOnce(shuffle, working, before, c,
                                              static_cast<uint64_t>(seed));
            check(rs.status == MutationStatus::APPLIED, "shuffle preserves the pulse count");
            check(working.generatorParam(GeneratorRole::GATE,
                                         EuclideanGateGenerator::kParamPulses) ==
                      before.generatorParam(GeneratorRole::GATE,
                                            EuclideanGateGenerator::kParamPulses),
                  "shuffle leaves the pulse count untouched");
        }
    }

    // A locked ordering forces rejection (never a partial edit).
    {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::PITCH_ORDERING), 64,
                              maskOf(MutationInvariant::LOCK_PITCH_ORDERING));
        const MutationResult r = runOnce(rotate, working, before, c, 9u);
        check(r.status == MutationStatus::REJECTED,
              "a locked pitch ordering forces rejection");
        check(std::memcmp(&working, &before, sizeof(working)) == 0,
              "the invariant rejection restored the state");
    }

    // Shuffle PRESERVES the pitch contents: with LOCK_PITCH_CONTENTS set it
    // still applies (a permutation is contents-preserving).
    {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::PITCH_ORDERING), 127,
                              maskOf(MutationInvariant::LOCK_PITCH_CONTENTS));
        const MutationResult r = runOnce(shuffle, working, before, c, 11u);
        check(r.status == MutationStatus::APPLIED,
              "shuffle satisfies the contents lock (it is a permutation)");
        check(sameSetContents(before.pitchSet, working.pitchSet),
              "shuffle keeps the pitch multiset");
    }

    // Invert REPLACES pitch values, so the contents lock rejects it.
    {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::PITCH_ORDERING), 127,
                              maskOf(MutationInvariant::LOCK_PITCH_CONTENTS));
        const MutationResult r = runOnce(invert, working, before, c, 11u);
        check(r.status == MutationStatus::REJECTED,
              "inversion violates the contents lock and is rejected");
        check(std::memcmp(&working, &before, sizeof(working)) == 0,
              "the contents-lock rejection restored the state");
    }

    // Locked velocity contents / duration material: the matching parameter
    // mutations are rejected.
    {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::VELOCITY_CONTENTS), 127,
                              maskOf(MutationInvariant::LOCK_VELOCITY_CONTENTS));
        const MutationResult r = runOnce(velMut, working, before, c, 13u);
        check(r.status == MutationStatus::REJECTED,
              "a locked velocity set forces rejection");
        check(std::memcmp(&working, &before, sizeof(working)) == 0,
              "the velocity-lock rejection restored the state");
    }
    {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::NOTE_DURATION), 127,
                              maskOf(MutationInvariant::LOCK_NOTE_DURATION));
        const MutationResult r = runOnce(durMut, working, before, c, 13u);
        check(r.status == MutationStatus::REJECTED,
              "a locked duration material forces rejection");
    }

    // The density mutation is the ONLY one that changes the pulse count, so
    // PRESERVE_PULSE_COUNT rejects it.
    {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::GATE_DENSITY), 64,
                              maskOf(MutationInvariant::PRESERVE_PULSE_COUNT));
        const MutationResult r = runOnce(density, working, before, c, 17u);
        check(r.status == MutationStatus::REJECTED,
              "the density mutation cannot preserve the pulse count");
        check(std::memcmp(&working, &before, sizeof(working)) == 0,
              "the pulse-count rejection restored the state");
    }

    // The euclidean mutation changes steps/rotation only: PRESERVE_PULSE_COUNT
    // holds, so it applies (or reports no change when the draws agree).
    {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::EUCLIDEAN_PARAMS), 127,
                              maskOf(MutationInvariant::PRESERVE_PULSE_COUNT));
        const MutationResult r = runOnce(euclidMut, working, before, c, 19u);
        check(r.status != MutationStatus::REJECTED,
              "the euclidean mutation preserves the pulse count");
        check(working.generatorParam(GeneratorRole::GATE,
                                    EuclideanGateGenerator::kParamPulses) ==
                  before.generatorParam(GeneratorRole::GATE,
                                       EuclideanGateGenerator::kParamPulses),
              "the euclidean mutation leaves the pulse count untouched");
    }
}

// --- (8) rotate by N then by -N restores the original --------------------------

void test_rotate_roundtrip() {
    RotateMutation plus5(5);
    RotateMutation minus5(-5);
    RotateMutation plus7(7);
    RotateMutation minus7(-7);
    const uint16_t orderMask = maskOf(MutationTarget::PITCH_ORDERING) |
                                maskOf(MutationTarget::VELOCITY_ORDERING) |
                                maskOf(MutationTarget::EUCLIDEAN_PARAMS);

    // One call with each of the two signs at the same intensity: the sequence
    // rotations cancel modulo the count and the gate rotation cancels modulo
    // the steps, so the state is restored byte-identically.
    const int intensities[] = {1, 64, 127};
    IMutationOperator* const pairs[][2] = {{&plus5, &minus5}, {&plus7, &minus7}};
    for (int intensity : intensities) {
        for (auto& pair : pairs) {
            PatternGenerativeState working = baseState();
            const PatternGenerativeState original = working;
            MutationConstraints c(orderMask, static_cast<uint16_t>(intensity));
            const MutationResult r1 = runOnce(*pair[0], working, original, c, 3u);
            const MutationResult r2 = runOnce(*pair[1], working, original, c, 3u);
            check(r1.status == MutationStatus::APPLIED && r2.status == MutationStatus::APPLIED,
                  "both rotations apply");
            check(std::memcmp(&working, &original, sizeof(working)) == 0,
                  "rotate by N then by -N restores the original state");
        }
    }

    // The same round trip holds for a gate rotation that already wraps.
    {
        PatternGenerativeState working = baseState();
        working.setGeneratorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamRotation, 60);
        const PatternGenerativeState original = working;
        MutationConstraints c(maskOf(MutationTarget::EUCLIDEAN_PARAMS), 127);
        runOnce(plus7, working, original, c, 3u);
        runOnce(minus7, working, original, c, 3u);
        check(std::memcmp(&working, &original, sizeof(working)) == 0,
              "a wrapping gate rotation round-trips exactly");
    }
}

// --- (9) invert twice restores the original ------------------------------------

void test_invert_involution() {
    InvertMutation invert;
    const uint16_t orderMask = maskOf(MutationTarget::PITCH_ORDERING) |
                                maskOf(MutationTarget::VELOCITY_ORDERING) |
                                maskOf(MutationTarget::EUCLIDEAN_PARAMS);
    const int intensities[] = {1, 32, 96, 127};
    for (int intensity : intensities) {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState original = working;
        MutationConstraints c(orderMask, static_cast<uint16_t>(intensity));
        const MutationResult r1 = runOnce(invert, working, original, c, 23u);
        const MutationResult r2 = runOnce(invert, working, original, c, 23u);
        check(r1.status == MutationStatus::APPLIED && r2.status == MutationStatus::APPLIED,
              "both inversions apply");
        check(std::memcmp(&working, &original, sizeof(working)) == 0,
              "inverting twice restores the original state");
    }

    // Per-target detail: the gate rotation reflection r -> (steps - r) % steps
    // is its own inverse even from a non-zero rotation.
    {
        PatternGenerativeState working = baseState();
        working.setGeneratorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamRotation, 9);
        const PatternGenerativeState original = working;
        MutationConstraints c(maskOf(MutationTarget::EUCLIDEAN_PARAMS), 64);
        runOnce(invert, working, original, c, 1u);
        check(working.generatorParam(GeneratorRole::GATE,
                                     EuclideanGateGenerator::kParamRotation) == 64 - 9,
              "the gate rotation reflects around 0");
        runOnce(invert, working, original, c, 1u);
        check(working.generatorParam(GeneratorRole::GATE,
                                     EuclideanGateGenerator::kParamRotation) == 9,
              "reflecting twice returns the original rotation");
    }

    // The pitch inversion mirrors scale DEGREES, so the result is in scale
    // (AC2) while the involution still holds exactly. The scale is the
    // request's global one (T16): default-constructed constraints carry the
    // factory C-Major, which baseState() is in.
    {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState original = working;
        const common::Scale cMajor = common::Scale(0, 0);
        MutationConstraints c(maskOf(MutationTarget::PITCH_ORDERING), 127);
        runOnce(invert, working, original, c, 29u);
        for (uint8_t i = 0; i < working.pitchSet.count; ++i) {
            check(cMajor.containsNote(working.pitchSet.values[i]),
                  "AC2: the pitch inversion stays in the selected scale");
        }
        runOnce(invert, working, original, c, 29u);
        check(std::memcmp(&working, &original, sizeof(working)) == 0,
              "the degree-mirrored inversion is an exact involution");
    }
}

// --- (10) shuffle is a permutation ----------------------------------------------

void test_shuffle_permutation() {
    ShuffleMutation shuffle;
    const int intensities[] = {1, 16, 64, 127};
    for (int intensity : intensities) {
        for (int seed = 1; seed <= 32; ++seed) {
            PatternGenerativeState working = baseState();
            const PatternGenerativeState before = working;
            MutationConstraints c(maskOf(MutationTarget::PITCH_ORDERING) |
                                      maskOf(MutationTarget::VELOCITY_ORDERING),
                                  static_cast<uint16_t>(intensity));
            const MutationResult r = runOnce(shuffle, working, before, c,
                                             static_cast<uint64_t>(seed));
            check(r.status == MutationStatus::APPLIED, "the shuffle applies");
            // A permutation: same count, same multiset, no new values.
            check(working.pitchSet.count == before.pitchSet.count,
                  "shuffle preserves the pitch count");
            check(sameSetContents(before.pitchSet, working.pitchSet),
                  "shuffle is a permutation of the pitch values");
            check(sameSetContents(before.velocitySet, working.velocitySet),
                  "shuffle is a permutation of the velocity values");
            // The pulse count (and all gate parameters) survive untouched.
            check(working.generatorParam(GeneratorRole::GATE,
                                         EuclideanGateGenerator::kParamPulses) ==
                      before.generatorParam(GeneratorRole::GATE,
                                            EuclideanGateGenerator::kParamPulses),
                  "shuffle preserves the pulse count");
        }
    }
}

// --- (11) determinism + changed-target reporting --------------------------------

void test_determinism_and_reporting() {
    DensityMutation density;
    PitchSetMutation pitchMut;
    RotateMutation rotate(2);

    // Identical (state, constraints, seed) reproduce identical results.
    {
        PatternGenerativeState a = baseState();
        const PatternGenerativeState beforeA = a;
        MutationConstraints c(maskOf(MutationTarget::PITCH_CONTENTS), 96);
        Prng rngA(1234u);
        PatternMutationView viewA(a, beforeA, 0);
        const MutationResult ra = pitchMut.apply(viewA, c, rngA);

        PatternGenerativeState b = baseState();
        const PatternGenerativeState beforeB = b;
        Prng rngB(1234u);
        PatternMutationView viewB(b, beforeB, 0);
        const MutationResult rb = pitchMut.apply(viewB, c, rngB);
        check(ra.status == rb.status && ra.changedTargets == rb.changedTargets,
              "identical seeds reproduce the same result");
        check(std::memcmp(&a, &b, sizeof(a)) == 0,
              "identical seeds reproduce the same state");

        PatternGenerativeState d = baseState();
        const PatternGenerativeState beforeD = d;
        Prng rngD(4321u);
        PatternMutationView viewD(d, beforeD, 0);
        const MutationResult rd = pitchMut.apply(viewD, c, rngD);
        check(rd.status == MutationStatus::APPLIED,
              "a different seed still applies (for this fixture)");
        check(std::memcmp(&a, &d, sizeof(a)) != 0 || ra.changedTargets != rd.changedTargets,
              "a different seed produces a different outcome");
    }

    // changedTargets reports the FR7 targets whose material actually changed:
    // a density mutation reports GATE_DENSITY.
    {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::GATE_DENSITY), 64);
        const MutationResult r = runOnce(density, working, before, c, 31u);
        check(r.status == MutationStatus::APPLIED, "the density mutation applies");
        check(r.changedTargets == maskOf(MutationTarget::GATE_DENSITY),
              "the density mutation reports GATE_DENSITY");
    }

    // A rotate over all three supported targets reports exactly the three
    // ordering bits (contents are preserved by a rotation).
    {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::PITCH_ORDERING) |
                                  maskOf(MutationTarget::VELOCITY_ORDERING) |
                                  maskOf(MutationTarget::EUCLIDEAN_PARAMS),
                              64);
        const MutationResult r = runOnce(rotate, working, before, c, 31u);
        check(r.status == MutationStatus::APPLIED, "the rotation applies");
        check(r.changedTargets ==
                  (maskOf(MutationTarget::PITCH_ORDERING) |
                   maskOf(MutationTarget::VELOCITY_ORDERING) |
                   maskOf(MutationTarget::EUCLIDEAN_PARAMS)),
              "the rotation reports exactly its three ordering targets");
    }

    // A pitch-set mutation replaces values, so the changed mask reports the
    // contents bit (and the sequence difference that follows from it).
    {
        PatternGenerativeState working = baseState();
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::PITCH_CONTENTS), 127);
        const MutationResult r = runOnce(pitchMut, working, before, c, 33u);
        check(r.status == MutationStatus::APPLIED, "the pitch mutation applies");
        check((r.changedTargets & maskOf(MutationTarget::PITCH_CONTENTS)) != 0,
              "the pitch mutation reports PITCH_CONTENTS");
        check((r.changedTargets & maskOf(MutationTarget::GATE_DENSITY)) == 0,
              "the pitch mutation never reports gate targets");
    }

    // Degenerate no-change paths: a rotation of a one-element set and a
    // shuffle of empty sets change nothing and report NO_CHANGE.
    {
        PatternGenerativeState working;
        working.pitchSet.count = 1;
        working.pitchSet.values[0] = 60;
        const PatternGenerativeState before = working;
        MutationConstraints c(maskOf(MutationTarget::PITCH_ORDERING) |
                                 maskOf(MutationTarget::VELOCITY_ORDERING),
                             64);
        const MutationResult r = runOnce(rotate, working, before, c, 35u);
        check(r.status == MutationStatus::NO_CHANGE,
              "rotating a one-element set reports NO_CHANGE");
        check(std::memcmp(&working, &before, sizeof(working)) == 0,
              "the no-change left the state untouched");
    }
}

}  // namespace

int main() {
    test_identity();
    test_intensity_zero();
    test_unsupported_targets();
    test_monotone_density();
    test_monotone_sets();
    test_monotone_euclidean();
    test_monotone_rotate();
    test_monotone_invert();
    test_monotone_shuffle();
    test_scale_lock_all_templates();
    test_distance_limits();
    test_clamping();
    test_invariants();
    test_rotate_roundtrip();
    test_invert_involution();
    test_shuffle_permutation();
    test_determinism_and_reporting();

    if (failures == 0) {
        std::printf("test_parameter_transform_mutations: all checks passed\n");
        return 0;
    }
    std::printf("test_parameter_transform_mutations: %d check(s) failed\n", failures);
    return 1;
}
