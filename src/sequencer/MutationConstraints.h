#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "../common/Scale.h"
#include "PatternGenerativeState.h"
#include "generators/EuclideanGateGenerator.h"  // kParamPulses (pulse-count invariant)

// Mutation constraints (generative-engine-expansion task 8, FR6, FR7, FR8).
//
// The value types every mutation request carries (FR5-FR8) and the bounded
// helpers that police the "randomness WITH constraints" philosophy:
//
//   - MutationTarget — the FR7 bitmask of the fields a mutation may touch:
//     gate density, pitch set contents/ordering, velocity set
//     contents/ordering, note duration and the Euclidean parameters;
//   - MutationInvariant — the FR8 bitmask of preserved properties: the
//     scale lock (AC2), locked material fields (contents/ordering/duration),
//     the locked Euclidean parameters, the preserved pulse count and the
//     locked generator assignments. (Step-level locks are enforced by the
//     concrete operators over the material they permute, task 9; this mask
//     carries the field-level invariants. The former LOCK_SCALE bit is
//     gone — the scale is a TRUE GLOBAL no mutation can touch, so a
//     "locked scale" invariant would be vacuous, ui-integration T16.);
//   - MutationConstraints — one request's targets, intensity (FR6, always
//     clamped to 0-127), invariants, maximum distance from the working
//     state (FR8) and the GLOBAL scale the request operates under — the
//     single-source scale (TransportTimeline) every pattern's pitch
//     generation and pitch mutation read (ui-integration T16, FR12/FR13);
//   - mutationDistance — the documented distance metric between two
//     PatternGenerativeState snapshots (the working-state snapshot type the
//     undo stack stores, task 12);
//   - the constraint checks: withinDistance (FR8 distance limit) and
//     violatedInvariants / invariantsHold (FR8 invariants), the latter
//     checking pitch material against the request's GLOBAL scale (AC2 —
//     never a per-pattern field).
//
// Global constraints honored here: plain fixed-size value types — no heap,
// no RTTI, no exceptions; every helper is a bounded loop over fixed
// capacities (kGenerativeSetCapacity = 16 values, kGeneratorRoleCount = 4
// roles, kMaxGeneratorParams = 8 parameters), so each check completes in a
// documented bounded operation count (AC4: the distance metric costs at most
// 135 comparisons; a contents-equality check costs at most 2 * 16^2 bounded
// sort/compare steps — no unbounded loop, no blocking). SDK-free: standard
// headers plus the SDK-free Scale / PatternGenerativeState / Euclidean gate
// generator headers only, so this compiles unchanged in the firmware and the
// host test build.

namespace sequencer {

// --- FR7: the mutation-target bitmask ----------------------------------------

// What a mutation may touch. Exactly the seven FR7 targets; one bit each so
// a request composes them with plain bitwise OR.
enum class MutationTarget : uint16_t {
    NONE = 0,
    GATE_DENSITY = 1u << 0,      // how many pulses fire (Euclidean density)
    PITCH_CONTENTS = 1u << 1,    // which pitch values the pitch set holds
    PITCH_ORDERING = 1u << 2,    // the order pitches play in (sequence + playing order)
    VELOCITY_CONTENTS = 1u << 3,  // which velocity values the velocity set holds
    VELOCITY_ORDERING = 1u << 4,  // the order velocities play in
    NOTE_DURATION = 1u << 5,    // the note-duration material (lengths)
    EUCLIDEAN_PARAMS = 1u << 6,  // gate-role Euclidean steps/pulses/rotation
};

// A set of MutationTarget bits (a plain uint16_t so it snapshots, persists
// and crosses the wire as one word).
using MutationTargetMask = uint16_t;

// Exactly the seven FR7 targets above.
constexpr uint8_t kMutationTargetCount = 7;
// Every FR7 target bit at once (0x7F).
constexpr MutationTargetMask kAllMutationTargets = 0x007Fu;

// The raw mask of one target bit.
constexpr MutationTargetMask maskOf(MutationTarget t) {
    return static_cast<MutationTargetMask>(t);
}

constexpr MutationTarget operator|(MutationTarget a, MutationTarget b) {
    return static_cast<MutationTarget>(static_cast<MutationTargetMask>(a) |
                                       static_cast<MutationTargetMask>(b));
}

constexpr MutationTarget operator&(MutationTarget a, MutationTarget b) {
    return static_cast<MutationTarget>(static_cast<MutationTargetMask>(a) &
                                       static_cast<MutationTargetMask>(b));
}

// --- FR8: the invariant bitmask ----------------------------------------------

// Properties that must hold across a mutation (FR8). Each bit names one
// checkable guarantee over the pre-/post-mutation PatternGenerativeState
// pair; the operators (task 9/10) consult these before reporting success.
enum class MutationInvariant : uint16_t {
    NONE = 0,
    SCALE_LOCK = 1u << 0,  // every pitch stays in the pattern's scale (AC2)
    LOCK_PITCH_CONTENTS = 1u << 1,     // pitch values may not change (order-free)
    LOCK_PITCH_ORDERING = 1u << 2,     // the pitch sequence/order may not change
    LOCK_VELOCITY_CONTENTS = 1u << 3,  // velocity values may not change
    LOCK_VELOCITY_ORDERING = 1u << 4,  // the velocity sequence/order may not change
    LOCK_NOTE_DURATION = 1u << 5,      // the duration material may not change
    LOCK_EUCLIDEAN_PARAMS = 1u << 6,   // gate-role generator params may not change
    PRESERVE_PULSE_COUNT = 1u << 7,    // the gate-role pulse count stays fixed
    LOCK_GENERATORS = 1u << 9,        // role generator assignments may not change
};

// A set of MutationInvariant bits.
using MutationInvariantMask = uint16_t;

constexpr MutationInvariantMask maskOf(MutationInvariant i) {
    return static_cast<MutationInvariantMask>(i);
}

constexpr MutationInvariant operator|(MutationInvariant a, MutationInvariant b) {
    return static_cast<MutationInvariant>(static_cast<MutationInvariantMask>(a) |
                                          static_cast<MutationInvariantMask>(b));
}

// --- bounded material helpers -------------------------------------------------

// Ascending in-place insertion sort of `count` <= kGenerativeSetCapacity
// bytes (bounded: at most 16^2 shift steps, no allocation).
inline void sortValuesAsc(uint8_t* v, uint8_t count) {
    for (uint8_t i = 1; i < count; ++i) {
        const uint8_t key = v[i];
        uint8_t j = i;
        while (j > 0 && v[j - 1] > key) {
            v[j] = v[j - 1];
            --j;
        }
        v[j] = key;
    }
}

// Order-independent (multiset) equality of two material sets: same live
// values regardless of their arrangement. Bounded by two 16-element sorts
// plus one 16-element compare loop.
inline bool sameSetContents(const GenerativeValueSet& a, const GenerativeValueSet& b) {
    if (a.count != b.count) return false;
    uint8_t sa[kGenerativeSetCapacity];
    uint8_t sb[kGenerativeSetCapacity];
    for (uint8_t i = 0; i < a.count; ++i) {
        sa[i] = a.values[i];
        sb[i] = b.values[i];
    }
    sortValuesAsc(sa, a.count);
    sortValuesAsc(sb, a.count);
    for (uint8_t i = 0; i < a.count; ++i) {
        if (sa[i] != sb[i]) return false;
    }
    return true;
}

// Exact sequence equality: same live values in the same positions AND the
// same playing order (the strictest form of a locked field).
inline bool sameSetSequence(const GenerativeValueSet& a, const GenerativeValueSet& b) {
    if (a.count != b.count || a.order != b.order) return false;
    for (uint8_t i = 0; i < a.count; ++i) {
        if (a.values[i] != b.values[i]) return false;
    }
    return true;
}

// --- FR8: the state-distance metric -------------------------------------------

// The documented distance between two PatternGenerativeState (working-state)
// snapshots, a bounded sum over the fixed-capacity fields:
//   - per material set (pitch / velocity / duration):
//       +1 when the playing order differs,
//       +|countA - countB| for the size difference,
//       +1 per differing value position below the smaller count;
//   - +1 per differing generator parameter entry (all roles);
//   - +1 per role whose generator id assignment differs.
//
// Identical snapshots have distance 0. The metric's maximum is
// 3 * (1 + 16 + 16) + 4 * 8 + 4 = 135, so a maxDistance of 255 (the
// MutationConstraints default) is effectively unlimited. Bounded work: at
// most 135 comparisons, no allocation, no unbounded loop (AC4). The scale
// is NOT counted: it is a global setting outside the per-pattern state
// (ui-integration T16), so no mutation can change it.
inline uint16_t mutationDistance(const PatternGenerativeState& a,
                                 const PatternGenerativeState& b) {
    uint16_t d = 0;

    // Material sets: order, count, value positions.
    const GenerativeValueSet* setsA[3] = {&a.pitchSet, &a.velocitySet, &a.durationSet};
    const GenerativeValueSet* setsB[3] = {&b.pitchSet, &b.velocitySet, &b.durationSet};
    for (uint8_t s = 0; s < 3; ++s) {
        const GenerativeValueSet& x = *setsA[s];
        const GenerativeValueSet& y = *setsB[s];
        if (x.order != y.order) ++d;
        d = static_cast<uint16_t>(d + (x.count > y.count ? x.count - y.count
                                                         : y.count - x.count));
        const uint8_t live = x.count < y.count ? x.count : y.count;
        for (uint8_t i = 0; i < live; ++i) {
            if (x.values[i] != y.values[i]) ++d;
        }
    }

    // Generator parameters, all roles.
    for (uint8_t r = 0; r < kGeneratorRoleCount; ++r) {
        for (uint8_t p = 0; p < kMaxGeneratorParams; ++p) {
            if (a.generatorParams[r][p] != b.generatorParams[r][p]) ++d;
        }
    }

    // Generator id assignments, all roles.
    for (uint8_t r = 0; r < kGeneratorRoleCount; ++r) {
        if (std::memcmp(a.generatorIds[r], b.generatorIds[r], kGeneratorIdCapacity) != 0) {
            ++d;
        }
    }

    return d;
}

// --- FR6/FR8: the constraints value object ------------------------------------

// The maximum distance value (255): above the metric's maximum of 135, so a
// default-constructed limit means "no practical restriction".
constexpr uint8_t kUnlimitedMutationDistance = 255;

// One mutation request's constraints (FR6-FR8): what may change (targets),
// how much (intensity), what must not change (invariants), how far the
// result may drift (maxDistance) and the GLOBAL scale the request operates
// under (`scale`, ui-integration T16 — read solely from the single global
// source by whoever stages the request). A fixed POD: trivially copyable
// so it can ride in commands, the undo flow and the UI state without any
// dynamic part.
//
// `intensity` is ALWAYS in 0-127: assign through setIntensity() or the
// constructor, which clamp (FR6's adjustable 0-127 parameter).
//
// `maxDistance` bounds mutationDistance(reference, candidate) where
// `reference` is the pre-mutation working state (FR8's "distance limits from
// the working state"). The check helpers take the states explicitly so the
// caller chooses the anchor (the pre-mutation snapshot, or the kept state
// after the Keep operation, task 13).
struct MutationConstraints {
    // Which FR7 fields the operator may touch (MutationTargetMask).
    MutationTargetMask targets = 0;
    // Which FR8 properties must be preserved (MutationInvariantMask).
    MutationInvariantMask invariants = 0;
    // How drastic the change may be (FR6); always 0-127 (clamped on entry).
    uint8_t intensity = 0;
    // Maximum mutationDistance from the reference state (FR8);
    // kUnlimitedMutationDistance means no practical restriction.
    uint8_t maxDistance = kUnlimitedMutationDistance;
    // The GLOBAL scale this request operates under (ui-integration T16,
    // FR12/FR13): a copy of the single-source scale (the transport
    // timeline's, read overlay-aware through the ParameterBus) the
    // scale-lock check and the scale-aware pitch operators use — the
    // per-pattern state carries NO scale of its own. Default: C Major,
    // the factory global.
    common::Scale scale;

    MutationConstraints() = default;

    // Field order follows the spec's description: target bitmask, intensity
    // 0-127, invariants, maximum distance. `intensityIn` is clamped to
    // 0-127 (FR6); the other fields are masks/limits and are kept verbatim.
    constexpr MutationConstraints(MutationTargetMask targetsIn, uint16_t intensityIn,
                                  MutationInvariantMask invariantsIn = 0,
                                  uint8_t maxDistanceIn = kUnlimitedMutationDistance)
        : targets(targetsIn),
          invariants(invariantsIn),
          intensity(intensityIn > 127u ? 127u : static_cast<uint8_t>(intensityIn)),
          maxDistance(maxDistanceIn) {}

    // FR6: set the intensity, clamped to the 0-127 range (subtle .. drastic).
    void setIntensity(uint16_t value) {
        intensity = value > 127u ? 127u : static_cast<uint8_t>(value);
    }

    // --- target mask helpers ---------------------------------------------------

    void addTarget(MutationTarget t) { targets |= maskOf(t); }
    void clearTarget(MutationTarget t) { targets &= static_cast<MutationTargetMask>(~maskOf(t)); }
    bool hasTarget(MutationTarget t) const { return (targets & maskOf(t)) != 0; }

    // --- invariant mask helpers --------------------------------------------------

    void setInvariant(MutationInvariant i) { invariants |= maskOf(i); }
    void clearInvariant(MutationInvariant i) {
        invariants &= static_cast<MutationInvariantMask>(~maskOf(i));
    }
    bool hasInvariant(MutationInvariant i) const { return (invariants & maskOf(i)) != 0; }

    // --- FR8: the distance-limit check ------------------------------------------

    // True when `candidate` lies within maxDistance of `reference` (the
    // pre-mutation working state). Bounded: one mutationDistance call.
    bool withinDistance(const PatternGenerativeState& reference,
                        const PatternGenerativeState& candidate) const {
        return mutationDistance(reference, candidate) <= maxDistance;
    }

    // --- FR8: the invariant checks ------------------------------------------------

    // The mask of CONFIGURED invariants that do not hold between `before`
    // (the pre-mutation snapshot) and `after` (the mutated state): 0 means
    // everything requested is preserved. Bounded work (AC4): at most
    // 16 scale-membership checks, three 16^2 contents comparisons and a
    // handful of fixed-field compares.
    MutationInvariantMask violatedInvariants(const PatternGenerativeState& before,
                                             const PatternGenerativeState& after) const {
        MutationInvariantMask violated = 0;

        if (hasInvariant(MutationInvariant::SCALE_LOCK)) {
            // AC2: the resulting pitch material must lie in the request's
            // GLOBAL scale — the single source every pattern reads (T16:
            // carried by the constraints, never a per-pattern field).
            const common::Scale& scale = this->scale;
            for (uint8_t i = 0; i < after.pitchSet.count; ++i) {
                if (!scale.containsNote(after.pitchSet.values[i])) {
                    violated |= maskOf(MutationInvariant::SCALE_LOCK);
                    break;
                }
            }
        }
        if (hasInvariant(MutationInvariant::LOCK_PITCH_CONTENTS) &&
            !sameSetContents(before.pitchSet, after.pitchSet)) {
            violated |= maskOf(MutationInvariant::LOCK_PITCH_CONTENTS);
        }
        if (hasInvariant(MutationInvariant::LOCK_PITCH_ORDERING) &&
            !sameSetSequence(before.pitchSet, after.pitchSet)) {
            violated |= maskOf(MutationInvariant::LOCK_PITCH_ORDERING);
        }
        if (hasInvariant(MutationInvariant::LOCK_VELOCITY_CONTENTS) &&
            !sameSetContents(before.velocitySet, after.velocitySet)) {
            violated |= maskOf(MutationInvariant::LOCK_VELOCITY_CONTENTS);
        }
        if (hasInvariant(MutationInvariant::LOCK_VELOCITY_ORDERING) &&
            !sameSetSequence(before.velocitySet, after.velocitySet)) {
            violated |= maskOf(MutationInvariant::LOCK_VELOCITY_ORDERING);
        }
        if (hasInvariant(MutationInvariant::LOCK_NOTE_DURATION) &&
            !sameSetSequence(before.durationSet, after.durationSet)) {
            violated |= maskOf(MutationInvariant::LOCK_NOTE_DURATION);
        }
        if (hasInvariant(MutationInvariant::LOCK_EUCLIDEAN_PARAMS)) {
            // The gate role's generator parameters (the built-in Euclidean
            // steps/pulses/rotation drive the gate pattern).
            for (uint8_t p = 0; p < kMaxGeneratorParams; ++p) {
                if (before.generatorParams[static_cast<uint8_t>(GeneratorRole::GATE)][p] !=
                    after.generatorParams[static_cast<uint8_t>(GeneratorRole::GATE)][p]) {
                    violated |= maskOf(MutationInvariant::LOCK_EUCLIDEAN_PARAMS);
                    break;
                }
            }
        }
        if (hasInvariant(MutationInvariant::PRESERVE_PULSE_COUNT) &&
            before.generatorParams[static_cast<uint8_t>(GeneratorRole::GATE)]
                                [EuclideanGateGenerator::kParamPulses] !=
                after.generatorParams[static_cast<uint8_t>(GeneratorRole::GATE)]
                                     [EuclideanGateGenerator::kParamPulses]) {
            violated |= maskOf(MutationInvariant::PRESERVE_PULSE_COUNT);
        }
        if (hasInvariant(MutationInvariant::LOCK_GENERATORS)) {
            for (uint8_t r = 0; r < kGeneratorRoleCount; ++r) {
                if (std::memcmp(before.generatorIds[r], after.generatorIds[r],
                                kGeneratorIdCapacity) != 0) {
                    violated |= maskOf(MutationInvariant::LOCK_GENERATORS);
                    break;
                }
            }
        }

        return violated;
    }

    // True when every configured invariant holds between `before` and
    // `after` (the FR8 gate operators consult before reporting success).
    bool invariantsHold(const PatternGenerativeState& before,
                        const PatternGenerativeState& after) const {
        return violatedInvariants(before, after) == 0;
    }
};

// Size bounds on the new POD state (global constraint): two mask words,
// two limit bytes and the request's global scale pair, trivially copyable
// for command payloads and undo flows.
static_assert(std::is_trivially_copyable<MutationConstraints>::value,
              "MutationConstraints must be trivially copyable");
static_assert(std::is_standard_layout<MutationConstraints>::value,
              "MutationConstraints must be standard layout");
static_assert(sizeof(MutationConstraints) <= 8,
              "MutationConstraints stays a small fixed POD");
static_assert(kMutationTargetCount == 7,
              "exactly the seven FR7 mutation targets");
static_assert(kAllMutationTargets == 0x007Fu,
              "the all-targets mask covers the seven FR7 bits with no strays");

}  // namespace sequencer
