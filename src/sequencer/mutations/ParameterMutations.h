#pragma once

#include <cstdint>
#include <type_traits>

#include "../IMutationOperator.h"
#include "../generators/EuclideanGateGenerator.h"    // the gate param ids
#include "../generators/NoteDurationGenerator.h"     // the duration range
#include "../../common/Scale.h"                      // AC2: nearestNoteInScale

// Parameter mutation operators (generative-engine-expansion task 9, FR5,
// FR6, FR7, FR8, AC2).
//
// The five concrete IMutationOperator plugins that mutate the VALUES a
// pattern plays (the "set contents" side of FR7):
//
//   - DensityMutation        (builtin.mutate-density)      -> GATE_DENSITY
//       moves the gate role's Euclidean pulse count up or down by an
//       intensity-scaled magnitude, clamped into [0, steps];
//   - PitchSetMutation        (builtin.mutate-pitch-set)   -> PITCH_CONTENTS
//       replaces live pitch values, one element at a time, with a note drawn
//       from the request's GLOBAL scale (AC2: the raw draw is snapped with
//       common::Scale::nearestNoteInScale, so the result is ALWAYS in the
//       global scale whatever the intensity; ui-integration T16);
//   - VelocitySetMutation     (builtin.mutate-velocity-set)-> VELOCITY_CONTENTS
//       the same per-element scheme over the velocity material (MIDI 0-127);
//   - DurationMutation        (builtin.mutate-duration)    -> NOTE_DURATION
//       the same per-element scheme over the duration material (transport
//       ticks, the built-in duration generator's 1-255 range);
//   - EuclideanParamMutation  (builtin.mutate-euclidean)   -> EUCLIDEAN_PARAMS
//       redraws the gate role's Euclidean steps and/or rotation (never the
//       pulse count — DensityMutation owns that), normalized into the
//       built-in generator's ranges.
//
// FR6 intensity semantics (shared by every operator): intensity 0 is a
// NO_CHANGE; at higher intensity each operator's expected change grows
// monotonically — per-element mutations fire with probability
// intensity/128, magnitudes draw from an intensity-scaled span and transform
// prefixes/multipliers scale with the intensity (see mutation_detail).
//
// This header also hosts mutation_detail, the shared implementation skeleton
// every concrete operator (here and in TransformMutations.h, which includes
// this header for it) builds on:
//   - the apply() template method: FR8 operator control (unsupported targets
//     -> REJECTED), FR6 (intensity 0 -> NO_CHANGE), the mutation itself,
//     then the FR8 gate (invariants + distance limit against view.before,
//     restoring view.state = view.before on violation);
//   - changedTargetsBetween: the FR7 diff between two working states, which
//     reports every target whose material actually changed.
//
// AC4 bounded-operation contract: every apply() is bounded by a small
// documented constant. The mutations loop at most kGenerativeSetCapacity (16)
// live elements, each iteration drawing a bounded number of PRNG bytes plus at
// most one nearestNoteInScale scan (<= 25 membership checks); the shared FR8
// gate adds the constraint helpers' bounds (<= 137 comparisons for the
// distance metric, <= 2 * 16^2 steps per contents check). No heap
// allocation, no unbounded loop, no blocking, no I/O, no exceptions.
// SDK-free: sequencer and common headers only, so this compiles unchanged in
// the firmware and the host test build.

namespace sequencer {

// --- shared implementation details ---------------------------------------------
//
// NOTE: implementation-detail namespace shared by ParameterMutations.h and
// TransformMutations.h (the latter includes this header for the skeleton).
namespace mutation_detail {

// FR6: the per-element mutation threshold. A drawn PRNG byte below this
// value fires the element's mutation, so the per-element probability is
// intensity/128 (0.8% at intensity 1, 99.6% at 127). intensity is always
// 0-127 (clamped by MutationConstraints), so the product never wraps.
inline uint8_t mutationThreshold(uint8_t intensity) {
    return static_cast<uint8_t>(static_cast<uint16_t>(intensity) * 2u);
}

// FR6: the largest magnitude a density mutation may move the pulse count in
// one call — 1 at intensity 1 growing to 64 at intensity 127.
inline uint8_t densitySpan(uint8_t intensity) {
    return static_cast<uint8_t>(1 + (static_cast<int>(intensity) * 63) / 127);
}

// FR7: the diff between two working states — the mask of every target whose
// material actually changed (the "after" side of the operator's report).
//   - GATE_DENSITY    <-> the Euclidean pulse parameter changed;
//   - EUCLIDEAN_PARAMS<-> the Euclidean steps or rotation parameter changed;
//   - *_CONTENTS      <-> the set's live value multiset changed;
//   - *_ORDERING      <-> the set's live sequence (or playing order) changed;
//   - NOTE_DURATION   <-> the duration material's sequence changed.
// A value replacement therefore reports CONTENTS and ORDERING (the sequence
// changed with it); a pure permutation reports ORDERING only. Bounded work:
// three contents/sequence comparisons (each <= 2 * 16^2 steps) plus a
// handful of fixed-field compares.
inline MutationTargetMask changedTargetsBetween(const PatternGenerativeState& a,
                                                const PatternGenerativeState& b) {
    MutationTargetMask mask = 0;
    const uint8_t gate = static_cast<uint8_t>(GeneratorRole::GATE);
    if (a.generatorParams[gate][EuclideanGateGenerator::kParamPulses] !=
        b.generatorParams[gate][EuclideanGateGenerator::kParamPulses]) {
        mask |= maskOf(MutationTarget::GATE_DENSITY);
    }
    if (a.generatorParams[gate][EuclideanGateGenerator::kParamSteps] !=
            b.generatorParams[gate][EuclideanGateGenerator::kParamSteps] ||
        a.generatorParams[gate][EuclideanGateGenerator::kParamRotation] !=
            b.generatorParams[gate][EuclideanGateGenerator::kParamRotation]) {
        mask |= maskOf(MutationTarget::EUCLIDEAN_PARAMS);
    }
    if (!sameSetContents(a.pitchSet, b.pitchSet)) {
        mask |= maskOf(MutationTarget::PITCH_CONTENTS);
    }
    if (!sameSetSequence(a.pitchSet, b.pitchSet)) {
        mask |= maskOf(MutationTarget::PITCH_ORDERING);
    }
    if (!sameSetContents(a.velocitySet, b.velocitySet)) {
        mask |= maskOf(MutationTarget::VELOCITY_CONTENTS);
    }
    if (!sameSetSequence(a.velocitySet, b.velocitySet)) {
        mask |= maskOf(MutationTarget::VELOCITY_ORDERING);
    }
    if (!sameSetSequence(a.durationSet, b.durationSet)) {
        mask |= maskOf(MutationTarget::NOTE_DURATION);
    }
    return mask;
}

// The FR6/FR8 tail of every apply(): diff the candidate against the entry
// state, report NO_CHANGE when nothing changed, and gate APPLIED results on
// the FR8 checks measured against `view.before` (the pre-mutation anchor —
// the pre-mutation snapshot, or the kept state after Keep). On violation the
// operator restores view.state = view.before, per the IMutationOperator
// contract ("every REJECTED path leaves the state equal to view.before").
inline MutationResult finalize(PatternMutationView& view,
                               const MutationConstraints& constraints,
                               const PatternGenerativeState& entry,
                               const PatternGenerativeState& candidate) {
    const MutationTargetMask changed = changedTargetsBetween(entry, candidate);
    if (changed == 0) {
        return MutationResult{MutationStatus::NO_CHANGE, 0};
    }
    if (!constraints.invariantsHold(view.before, candidate) ||
        !constraints.withinDistance(view.before, candidate)) {
        view.state = view.before;
        return MutationResult{MutationStatus::REJECTED, 0};
    }
    view.state = candidate;
    return MutationResult{MutationStatus::APPLIED, changed};
}

// The shared apply() skeleton every concrete operator (this header and
// TransformMutations.h) implements IMutationOperator through:
//
//   1. FR8 operator control: a request sharing no bit with
//      supportedTargets() is REJECTED without touching anything;
//   2. FR6: intensity 0 is a deliberate NO_CHANGE (never a random no-op);
//   3. the operator's bounded mutation runs on a copy of the entry state
//      (mutate() never observes a half-edited view);
//   4. the FR8 gate (invariants + distance, see finalize).
class OperatorSkeleton : public IMutationOperator {
public:
    MutationResult apply(PatternMutationView& view, const MutationConstraints& constraints,
                         Prng& rng) override {
        if ((constraints.targets & supportedTargets()) == 0) {
            return MutationResult{MutationStatus::REJECTED, 0};
        }
        if (constraints.intensity == 0) {
            return MutationResult{MutationStatus::NO_CHANGE, 0};
        }
        const PatternGenerativeState entry = view.state;
        PatternGenerativeState candidate = entry;
        mutate(candidate, constraints, rng);
        return finalize(view, constraints, entry, candidate);
    }

protected:
    // The operator's bounded mutation (see the AC4 note in the file comment).
    virtual void mutate(PatternGenerativeState& candidate,
                        const MutationConstraints& constraints, Prng& rng) const = 0;
};

// The current Euclidean gate configuration, defensively clamped into the
// built-in generator's ranges (steps 1-64, pulses 0..steps, rotation
// 0..steps-1) so an arbitrary stored state can never push a result out of
// range (the FR8 "clamps" side of the distance-limit rule).
struct EuclideanConfig {
    int16_t steps;
    int16_t pulses;
    int16_t rotation;
};

inline EuclideanConfig readEuclideanConfig(const PatternGenerativeState& s) {
    EuclideanConfig c;
    c.steps = s.generatorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamSteps);
    if (c.steps < 1) c.steps = 1;
    if (c.steps > EuclideanGateGenerator::kMaxSteps) {
        c.steps = EuclideanGateGenerator::kMaxSteps;
    }
    c.pulses = s.generatorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamPulses);
    if (c.pulses < 0) c.pulses = 0;
    if (c.pulses > c.steps) c.pulses = c.steps;
    c.rotation = s.generatorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamRotation);
    if (c.rotation < 0) c.rotation = 0;
    if (c.rotation > c.steps - 1) c.rotation = static_cast<int16_t>(c.steps - 1);
    return c;
}

}  // namespace mutation_detail

// --- FR5/FR7: the parameter mutation operators ----------------------------------

// GATE_DENSITY: move the Euclidean pulse count up or down by one
// intensity-scaled magnitude, clamped into [0, steps]. Direction and
// magnitude both draw from the PRNG (bounded: one nextBelow(span) with span
// at most 64, one nextBelow(2)); the pulse count never leaves the pattern's
// step range, so a REJECT is never needed for range reasons — only the FR8
// invariant (PRESERVE_PULSE_COUNT) or distance limit can veto.
class DensityMutation final : public mutation_detail::OperatorSkeleton {
public:
    const char* id() const override { return "builtin.mutate-density"; }
    const char* name() const override { return "Density"; }
    MutationTargetMask supportedTargets() const override {
        return maskOf(MutationTarget::GATE_DENSITY);
    }

protected:
    void mutate(PatternGenerativeState& candidate, const MutationConstraints& constraints,
                Prng& rng) const override {
        const mutation_detail::EuclideanConfig c =
            mutation_detail::readEuclideanConfig(candidate);
        const uint8_t span = mutation_detail::densitySpan(constraints.intensity);
        const int16_t magnitude =
            static_cast<int16_t>(1 + rng.nextBelow(span));  // 1..span <= 64
        int16_t pulses = c.pulses;
        if (rng.nextBelow(2) != 0) {
            pulses = static_cast<int16_t>(pulses + magnitude);
        } else {
            pulses = static_cast<int16_t>(pulses - magnitude);
        }
        if (pulses < 0) pulses = 0;
        if (pulses > c.steps) pulses = c.steps;
        candidate.setGeneratorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamPulses,
                                    pulses);
    }
};

// PITCH_CONTENTS (AC2): replace live pitch values one element at a time with
// probability intensity/128. Every replacement is snapped into the
// request's GLOBAL scale (carried by the constraints, ui-integration T16)
// with common::Scale::nearestNoteInScale, so the resulting material is
// ALWAYS in that scale — the SCALE_LOCK invariant then
// verifies what the operator guarantees by construction. Bounded: at most 16
// element iterations, each one PRNG byte plus (when firing) one raw draw and
// one nearestNoteInScale scan (<= 25 membership checks).
class PitchSetMutation final : public mutation_detail::OperatorSkeleton {
public:
    const char* id() const override { return "builtin.mutate-pitch-set"; }
    const char* name() const override { return "Pitch Set"; }
    MutationTargetMask supportedTargets() const override {
        return maskOf(MutationTarget::PITCH_CONTENTS);
    }

protected:
    void mutate(PatternGenerativeState& candidate, const MutationConstraints& constraints,
                Prng& rng) const override {
        const common::Scale scale = constraints.scale;
        const uint8_t threshold = mutation_detail::mutationThreshold(constraints.intensity);
        const uint8_t count =
            candidate.pitchSet.count < kGenerativeSetCapacity ? candidate.pitchSet.count
                                                             : kGenerativeSetCapacity;
        for (uint8_t i = 0; i < count; ++i) {
            if (rng.nextByte() < threshold) {
                // Draw a raw MIDI note and snap it into the scale (AC2).
                const uint8_t raw = static_cast<uint8_t>(rng.nextByte() & 0x7F);
                candidate.pitchSet.values[i] = scale.nearestNoteInScale(raw);
            }
        }
    }
};

// VELOCITY_CONTENTS: the same per-element scheme over the velocity material;
// replacements draw uniformly over MIDI 0-127. Bounded: at most 16
// iterations, at most two PRNG bytes each.
class VelocitySetMutation final : public mutation_detail::OperatorSkeleton {
public:
    const char* id() const override { return "builtin.mutate-velocity-set"; }
    const char* name() const override { return "Velocity Set"; }
    MutationTargetMask supportedTargets() const override {
        return maskOf(MutationTarget::VELOCITY_CONTENTS);
    }

protected:
    void mutate(PatternGenerativeState& candidate, const MutationConstraints& constraints,
                Prng& rng) const override {
        const uint8_t threshold = mutation_detail::mutationThreshold(constraints.intensity);
        const uint8_t count =
            candidate.velocitySet.count < kGenerativeSetCapacity
                ? candidate.velocitySet.count
                : kGenerativeSetCapacity;
        for (uint8_t i = 0; i < count; ++i) {
            if (rng.nextByte() < threshold) {
                candidate.velocitySet.values[i] =
                    static_cast<uint8_t>(rng.nextByte() & 0x7F);
            }
        }
    }
};

// NOTE_DURATION: the same per-element scheme over the duration material;
// replacements draw uniformly over the built-in duration generator's
// transport-tick range (1-255). Bounded: at most 16 iterations, at most one
// PRNG byte plus one bounded nextBelow(255) each.
class DurationMutation final : public mutation_detail::OperatorSkeleton {
public:
    const char* id() const override { return "builtin.mutate-duration"; }
    const char* name() const override { return "Duration"; }
    MutationTargetMask supportedTargets() const override {
        return maskOf(MutationTarget::NOTE_DURATION);
    }

protected:
    void mutate(PatternGenerativeState& candidate, const MutationConstraints& constraints,
                Prng& rng) const override {
        static_assert(NoteDurationGenerator::kMinDurationTicks == 1,
                      "the duration material keeps the generator's lower bound");
        static_assert(NoteDurationGenerator::kMaxDurationTicks <= 255,
                      "the duration material fits the uint8 set storage");
        const uint8_t threshold = mutation_detail::mutationThreshold(constraints.intensity);
        const uint8_t count =
            candidate.durationSet.count < kGenerativeSetCapacity
                ? candidate.durationSet.count
                : kGenerativeSetCapacity;
        const uint8_t span = static_cast<uint8_t>(
            NoteDurationGenerator::kMaxDurationTicks - NoteDurationGenerator::kMinDurationTicks +
            1);
        for (uint8_t i = 0; i < count; ++i) {
            if (rng.nextByte() < threshold) {
                candidate.durationSet.values[i] = static_cast<uint8_t>(
                    NoteDurationGenerator::kMinDurationTicks + rng.nextBelow(span));
            }
        }
    }
};

// EUCLIDEAN_PARAMS: redraw the gate role's Euclidean steps and/or rotation,
// each with probability intensity/128, normalized into the built-in
// generator's ranges. The PULSE COUNT IS NEVER TOUCHED (DensityMutation owns
// it), so PRESERVE_PULSE_COUNT always holds for this operator. When the step
// count changes, a kept rotation is re-clamped into [0, steps-1]. Bounded:
// two decision draws plus at most two bounded nextBelow draws.
class EuclideanParamMutation final : public mutation_detail::OperatorSkeleton {
public:
    const char* id() const override { return "builtin.mutate-euclidean"; }
    const char* name() const override { return "Euclidean Params"; }
    MutationTargetMask supportedTargets() const override {
        return maskOf(MutationTarget::EUCLIDEAN_PARAMS);
    }

protected:
    void mutate(PatternGenerativeState& candidate, const MutationConstraints& constraints,
                Prng& rng) const override {
        const mutation_detail::EuclideanConfig c =
            mutation_detail::readEuclideanConfig(candidate);
        const uint8_t threshold = mutation_detail::mutationThreshold(constraints.intensity);
        int16_t steps = c.steps;
        int16_t rotation = c.rotation;
        if (rng.nextByte() < threshold) {
            steps = static_cast<int16_t>(1 + rng.nextBelow(EuclideanGateGenerator::kMaxSteps));
        }
        if (rng.nextByte() < threshold) {
            rotation = static_cast<int16_t>(rng.nextBelow(static_cast<uint8_t>(steps)));
        } else if (rotation > steps - 1) {
            rotation = static_cast<int16_t>(steps - 1);  // normalized after a step change
        }
        candidate.setGeneratorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamSteps,
                                   steps);
        candidate.setGeneratorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamRotation,
                                    rotation);
    }
};

// Size bounds on the new operator state (global constraint): one vtable
// pointer each; the operators keep no data members, so they stay small fixed
// objects (the registry, task 10, stores them by value).
static_assert(sizeof(DensityMutation) <= 16,
              "DensityMutation stays a small fixed object");
static_assert(sizeof(PitchSetMutation) <= 16, "PitchSetMutation stays a small fixed object");
static_assert(sizeof(VelocitySetMutation) <= 16,
              "VelocitySetMutation stays a small fixed object");
static_assert(sizeof(DurationMutation) <= 16, "DurationMutation stays a small fixed object");
static_assert(sizeof(EuclideanParamMutation) <= 16,
              "EuclideanParamMutation stays a small fixed object");

}  // namespace sequencer
