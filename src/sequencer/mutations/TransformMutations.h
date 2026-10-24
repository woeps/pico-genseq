#pragma once

#include <cstdint>
#include <type_traits>

#include "ParameterMutations.h"  // mutation_detail: the shared apply() skeleton

// Transform mutation operators (generative-engine-expansion task 9, FR5,
// FR6, FR7, FR8).
//
// The three concrete IMutationOperator plugins that reorder the material a
// pattern already holds — the "ordering" side of FR7, applicable to the
// pitch ordering, the velocity ordering and the gate ordering (the gate
// role's Euclidean rotation, i.e. the gate pattern's phase) per the target
// mask:
//
//   - RotateMutation  (builtin.rotate):  rotate the live sequence by a
//       configurable signed amount scaled by the intensity; on the gate
//       target it adds the amount to the Euclidean rotation (mod steps);
//   - InvertMutation  (builtin.invert):  velocity inversion (v -> 127 - v),
//       melodic pitch inversion (scale DEGREES mirrored around the first
//       live note, so the result stays in the pattern's selected scale, AC2)
//       and gate-rotation reflection (r -> (steps - r) % steps), each over an
//       intensity-scaled prefix of the live material;
//   - ShuffleMutation (builtin.shuffle): a bounded Fisher-Yates prefix over
//       the live sequence — always a permutation, so the set contents and
//       the pulse count survive untouched — and a random gate phase.
//
// Per-operator algebra the tests pin:
//   - rotate by +N then by -N (same intensity) restores the original state
//     exactly (the sequence rotations cancel modulo the count, the gate
//     rotation modulo the steps);
//   - inverting twice restores the original exactly (the degree mirror and
//     the velocity/gate reflections are involutions, and the pitch pivot —
//     the first live note — is a fixed point of the mirror);
//   - shuffle is a permutation: same count, same multiset, no new values.
//
// Determinism: RotateMutation and InvertMutation consume NO PRNG entropy
// (pure functions of (state, constraints)); ShuffleMutation consumes a
// bounded number of PRNG bytes (one nextBelow per swap, at most
// kGenerativeSetCapacity - 1 of them per set, plus one for the gate phase).
// All randomness flows through the shared seedable Prng, so identical
// (view, constraints, seed) inputs reproduce identical results.
//
// AC4 bounded-operation contract: every apply() loops at most
// kGenerativeSetCapacity (16) live elements plus at most one 16-entry
// temporary copy per set rotation, and the shared FR8 gate (see
// ParameterMutations.h) adds the constraint helpers' documented bounds
// (<= 137 comparisons for the distance metric, <= 2 * 16^2 steps per
// contents check). No heap allocation, no unbounded loop, no blocking, no
// I/O, no exceptions. SDK-free: sequencer and common headers only.

namespace sequencer {

namespace mutation_transform_detail {

// FR6: the rotation multiplier — the configured amount is scaled by the
// intensity: 1x at intensity 1-42, 2x at 43-84, 3x at 85-127... precisely:
// 1 + intensity * 3 / 127, so 1..4, monotone in the intensity.
inline int rotateMultiplier(uint8_t intensity) {
    return 1 + (static_cast<int>(intensity) * 3) / 127;
}

// FR6: the inverted / shuffled prefix length — the first
// min(count, 1 + count * intensity / 127) live elements take part, so the
// affected prefix grows monotonically from 1 element to the whole set.
// `count` is the clamped live count (0 when the set is empty).
inline uint8_t activePrefix(uint8_t count, uint8_t intensity) {
    if (count == 0) return 0;
    uint8_t k = static_cast<uint8_t>(1 + (static_cast<int>(count) * intensity) / 127);
    if (k < 1) k = 1;
    if (k > count) k = count;
    return k;
}

// The Euclidean-ish positive modulus (total for any signed shift).
inline int posMod(int value, int modulus) {
    const int r = value % modulus;
    return (r < 0) ? r + modulus : r;
}

// Rotate the live values of `set` right by `shift` positions (mod the live
// count; a shift of 1 moves values[0] to values[1]). Bounded: one
// kGenerativeSetCapacity temporary copy plus one pass over the live values.
inline void rotateSetValues(GenerativeValueSet& set, int shift) {
    const uint8_t count =
        set.count < kGenerativeSetCapacity ? set.count : kGenerativeSetCapacity;
    if (count < 2) return;  // a 0/1-element set has no non-trivial rotation
    const uint8_t r = static_cast<uint8_t>(posMod(shift, static_cast<int>(count)));
    if (r == 0) return;
    uint8_t rotated[kGenerativeSetCapacity];
    for (uint8_t i = 0; i < count; ++i) {
        rotated[(i + r) % count] = set.values[i];
    }
    for (uint8_t i = 0; i < count; ++i) {
        set.values[i] = rotated[i];
    }
}

// The note's global scale degree: the per-octave degree (Scale::noteToDegree)
// extended across octaves so the mapping note <-> global degree is a strict
// order isomorphism over the in-scale notes in 0-127 (degree g corresponds
// to Scale::degreeToNote(g, 0)). `note` must be in scale (callers snap first);
// out-of-scale input falls back to degree 0 (defensive, never out of range).
// Bounded: the Scale helpers are bounded loops over at most 12 intervals.
inline int noteToGlobalDegree(const common::Scale& scale, uint8_t note) {
    const int d = scale.noteToDegree(note);
    if (d < 0) return 0;
    const int degrees = scale.degreeCount();
    const int interval = scale.degreeToNote(d, 0) - static_cast<int>(scale.root());
    const int octave =
        (static_cast<int>(note) - static_cast<int>(scale.root()) - interval) / 12;
    return octave * degrees + d;
}

// Invert the pitch material in place: mirror the global scale degrees of the
// first `k` live values around the first live note's degree (the pivot is a
// fixed point of the mirror), snapping out-of-scale input notes into the
// scale first (AC2 — the operator is scale-aware, like PitchSetMutation).
// Degrees beyond the representable 0-127 range clamp onto the nearest
// in-scale boundary degree, so every result stays a valid in-scale MIDI
// note. Bounded: k <= 16 iterations, each at most two nearestNoteInScale
// scans (<= 25 membership checks each) plus bounded Scale helpers.
inline void invertPitchPrefix(GenerativeValueSet& set, const common::Scale& scale,
                              uint8_t k) {
    const uint8_t count =
        set.count < kGenerativeSetCapacity ? set.count : kGenerativeSetCapacity;
    if (count == 0 || k == 0) return;
    if (k > count) k = count;
    const uint8_t pivotNote = scale.nearestNoteInScale(set.values[0]);
    const int pivot = noteToGlobalDegree(scale, pivotNote);
    // The representable degree range: the lowest / highest in-scale notes.
    const int lowDegree = noteToGlobalDegree(scale, scale.nearestNoteInScale(0));
    const int highDegree = noteToGlobalDegree(scale, scale.nearestNoteInScale(127));
    for (uint8_t i = 0; i < k; ++i) {
        const uint8_t note = scale.nearestNoteInScale(set.values[i]);  // AC2 snap
        const int degree = noteToGlobalDegree(scale, note);
        int mirrored = 2 * pivot - degree;
        if (mirrored < lowDegree) mirrored = lowDegree;
        if (mirrored > highDegree) mirrored = highDegree;
        const int mirroredNote = scale.degreeToNote(mirrored, 0);
        set.values[i] = static_cast<uint8_t>(mirroredNote);
    }
}

// Invert the velocity material in place: v -> 127 - v over the first `k` live
// values (an exact involution — 127 - (127 - v) == v).
inline void invertVelocityPrefix(GenerativeValueSet& set, uint8_t k) {
    const uint8_t count =
        set.count < kGenerativeSetCapacity ? set.count : kGenerativeSetCapacity;
    if (k > count) k = count;
    for (uint8_t i = 0; i < k; ++i) {
        set.values[i] = static_cast<uint8_t>(127 - set.values[i]);
    }
}

// A bounded Fisher-Yates prefix over the live values: `swaps` iterations,
// each swapping values[i] with values[i + nextBelow(count - i)]. Any swap
// sequence preserves the multiset, so the result is always a permutation.
// Bounded: swaps <= kGenerativeSetCapacity, one bounded nextBelow each.
inline void shuffleSetValues(GenerativeValueSet& set, uint8_t swaps, Prng& rng) {
    const uint8_t count =
        set.count < kGenerativeSetCapacity ? set.count : kGenerativeSetCapacity;
    for (uint8_t i = 0; i < swaps && i + 1 < count; ++i) {
        const uint8_t j = static_cast<uint8_t>(i + rng.nextBelow(static_cast<uint8_t>(count - i)));
        const uint8_t tmp = set.values[i];
        set.values[i] = set.values[j];
        set.values[j] = tmp;
    }
}

}  // namespace mutation_transform_detail

// --- FR5/FR7: the transform mutation operators ---------------------------------

// The shared target mask of the three transform operators: they reorder the
// pitch ordering, the velocity ordering and the gate ordering (the Euclidean
// rotation).
constexpr MutationTargetMask kTransformTargets =
    maskOf(MutationTarget::PITCH_ORDERING) | maskOf(MutationTarget::VELOCITY_ORDERING) |
    maskOf(MutationTarget::EUCLIDEAN_PARAMS);

// PITCH_ORDERING | VELOCITY_ORDERING | EUCLIDEAN_PARAMS: rotate the
// requested orderings by a configurable signed amount (default +1), scaled
// by the FR6 intensity multiplier (1x..4x). Consumes no PRNG entropy.
//   - pitch / velocity: the live sequence rotates by the amount (mod the
//     live count) — a pure permutation, so set contents and the pulse count
//     are preserved;
//   - gate: the Euclidean rotation parameter advances by the amount
//     (mod steps), preserving steps AND pulses (PRESERVE_PULSE_COUNT holds).
class RotateMutation final : public mutation_detail::OperatorSkeleton {
public:
    explicit RotateMutation(int8_t amount = 1) : amount_(amount) {}

    const char* id() const override { return "builtin.rotate"; }
    const char* name() const override { return "Rotate"; }
    MutationTargetMask supportedTargets() const override { return kTransformTargets; }

    // The configured rotation amount (sign included).
    int8_t amount() const { return amount_; }

protected:
    void mutate(PatternGenerativeState& candidate, const MutationConstraints& constraints,
                Prng& /*rng*/) const override {
        const int shift = static_cast<int>(amount_) *
                          mutation_transform_detail::rotateMultiplier(constraints.intensity);
        if (constraints.hasTarget(MutationTarget::PITCH_ORDERING)) {
            mutation_transform_detail::rotateSetValues(candidate.pitchSet, shift);
        }
        if (constraints.hasTarget(MutationTarget::VELOCITY_ORDERING)) {
            mutation_transform_detail::rotateSetValues(candidate.velocitySet, shift);
        }
        if (constraints.hasTarget(MutationTarget::EUCLIDEAN_PARAMS)) {
            const mutation_detail::EuclideanConfig c =
                mutation_detail::readEuclideanConfig(candidate);
            const int rotated = mutation_transform_detail::posMod(c.rotation + shift, c.steps);
            candidate.setGeneratorParam(GeneratorRole::GATE,
                                        EuclideanGateGenerator::kParamRotation,
                                        static_cast<int16_t>(rotated));
        }
    }

private:
    int8_t amount_;
};

// PITCH_ORDERING | VELOCITY_ORDERING | EUCLIDEAN_PARAMS: invert the
// requested orderings over an intensity-scaled prefix of the live material.
//   - velocity: v -> 127 - v (an exact involution);
//   - pitch: the scale degrees mirror around the first live note (in-scale
//     input stays in scale, AC2; out-of-scale input snaps into the scale);
//   - gate: the Euclidean rotation reflects r -> (steps - r) % steps (an
//     exact involution; a phase reflection has no partial form, so the gate
//     target inverts fully at any intensity >= 1).
// Inverting twice at the same intensity restores the original exactly.
class InvertMutation final : public mutation_detail::OperatorSkeleton {
public:
    const char* id() const override { return "builtin.invert"; }
    const char* name() const override { return "Invert"; }
    MutationTargetMask supportedTargets() const override { return kTransformTargets; }

protected:
    void mutate(PatternGenerativeState& candidate, const MutationConstraints& constraints,
                Prng& /*rng*/) const override {
        if (constraints.hasTarget(MutationTarget::PITCH_ORDERING)) {
            mutation_transform_detail::invertPitchPrefix(
                candidate.pitchSet, candidate.scale(),
                mutation_transform_detail::activePrefix(candidate.pitchSet.count,
                                                       constraints.intensity));
        }
        if (constraints.hasTarget(MutationTarget::VELOCITY_ORDERING)) {
            mutation_transform_detail::invertVelocityPrefix(
                candidate.velocitySet,
                mutation_transform_detail::activePrefix(candidate.velocitySet.count,
                                                       constraints.intensity));
        }
        if (constraints.hasTarget(MutationTarget::EUCLIDEAN_PARAMS)) {
            const mutation_detail::EuclideanConfig c =
                mutation_detail::readEuclideanConfig(candidate);
            const int reflected =
                mutation_transform_detail::posMod(c.steps - c.rotation, c.steps);
            candidate.setGeneratorParam(GeneratorRole::GATE,
                                        EuclideanGateGenerator::kParamRotation,
                                        static_cast<int16_t>(reflected));
        }
    }
};

// PITCH_ORDERING | VELOCITY_ORDERING | EUCLIDEAN_PARAMS: shuffle the
// requested orderings with a bounded Fisher-Yates prefix — an intensity-
// scaled number of random swaps (1 at intensity 1, a full pass at 127), so
// the result is always a permutation of the live values (contents and pulse
// count preserved). The gate target randomizes the Euclidean rotation within
// [0, steps-1] (one bounded draw).
class ShuffleMutation final : public mutation_detail::OperatorSkeleton {
public:
    const char* id() const override { return "builtin.shuffle"; }
    const char* name() const override { return "Shuffle"; }
    MutationTargetMask supportedTargets() const override { return kTransformTargets; }

protected:
    void mutate(PatternGenerativeState& candidate, const MutationConstraints& constraints,
                Prng& rng) const override {
        if (constraints.hasTarget(MutationTarget::PITCH_ORDERING)) {
            mutation_transform_detail::shuffleSetValues(
                candidate.pitchSet, shuffleCount(candidate.pitchSet.count, constraints.intensity),
                rng);
        }
        if (constraints.hasTarget(MutationTarget::VELOCITY_ORDERING)) {
            mutation_transform_detail::shuffleSetValues(
                candidate.velocitySet,
                shuffleCount(candidate.velocitySet.count, constraints.intensity), rng);
        }
        if (constraints.hasTarget(MutationTarget::EUCLIDEAN_PARAMS)) {
            const mutation_detail::EuclideanConfig c =
                mutation_detail::readEuclideanConfig(candidate);
            const int phase = rng.nextBelow(static_cast<uint8_t>(c.steps));
            candidate.setGeneratorParam(GeneratorRole::GATE,
                                        EuclideanGateGenerator::kParamRotation,
                                        static_cast<int16_t>(phase));
        }
    }

private:
    // FR6: the swap count grows from 1 at intensity 1 to a full Fisher-Yates
    // pass (count - 1 swaps) at intensity 127.
    static uint8_t shuffleCount(uint8_t count, uint8_t intensity) {
        if (count < 2) return 0;  // a 0/1-element set has nothing to shuffle
        const uint8_t live =
            count < kGenerativeSetCapacity ? count : kGenerativeSetCapacity;
        const uint8_t full = static_cast<uint8_t>(live - 1);
        return static_cast<uint8_t>(1 +
                                    (static_cast<int>(full) * intensity) / 127);
    }
};

// Size bounds on the new operator state (global constraint): one vtable
// pointer each; only RotateMutation carries data (the signed amount).
static_assert(sizeof(RotateMutation) <= 16, "RotateMutation stays a small fixed object");
static_assert(sizeof(InvertMutation) <= 16, "InvertMutation stays a small fixed object");
static_assert(sizeof(ShuffleMutation) <= 16, "ShuffleMutation stays a small fixed object");

}  // namespace sequencer
