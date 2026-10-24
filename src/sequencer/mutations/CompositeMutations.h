#pragma once

#include <cstdint>

#include "../IMutationOperator.h"
#include "ParameterMutations.h"  // the parameter operators + the shared skeleton
#include "TransformMutations.h"   // the transform operators

// Composite mutation operators (generative-engine-expansion task 10, FR5,
// FR6, FR7, FR8).
//
// The two concrete IMutationOperator plugins that COMPOSE the task-9
// operators into the FR5 "composite mutations":
//
//   - MutateAllMutation (builtin.mutate-all) — "mutate all": dispatches
//     every T9 operator whose supported targets share a bit with the
//     request's FR7 target mask, so one invocation nudges every requested
//     target family at once, and aggregates the changed-target mask over
//     the whole composition;
//   - EvolveMutation    (builtin.evolve)    — "evolve": the same
//     composition driven at a REDUCED, intensity-scaled sub-intensity, so
//     the result is always a small, gentle change (the FR6 knob scales how
//     far the small change reaches, never how drastic it can get).
//
// Composition semantics (the contract the tests pin):
//   - the sub-operators run in ONE FIXED order (density, pitch set,
//     velocity set, duration, Euclidean params, rotate, invert, shuffle),
//     each with the request's targets masked to its supportedTargets() and
//     the request's invariants / intensity (Evolve: the scaled intensity) —
//     so identical (view, constraints, seed) inputs reproduce identical
//     results (the global determinism rule);
//   - every sub-operator's FR8 gate anchors to THE CALLER'S anchor
//     (view.before, the pre-mutation snapshot — the same anchor the FR8
//     distance limit is specified against), so each accepted component
//     already lies within the distance limit of the working state; a
//     component that would breach the limit (or an invariant) skips
//     itself, and the composite keeps the earlier components' work —
//     "best effort within the constraints", never a partial-edit mess;
//   - after the last component the composite runs the shared finalize gate
//     (changedTargetsBetween + invariants + distance against view.before),
//     which by construction passes whenever anything was accepted — it is
//     the contract-mandated defensive check (and restores
//     view.state = view.before on the impossible violation);
//   - the reported changedTargets is the material diff between the entry
//     state and the final state: the truthful aggregate over every
//     component, exactly like a single T9 operator reports its own diff.
//
// AC4 bounded-operation contract (pinned by test_composite_mutations):
// one composite apply() performs at most kCompositeWorstCaseOperations
// counted bounded iteration units — kCompositeSubOperatorCount (8)
// sub-operator dispatches plus exactly one composite finalize gate (see
// lastOperationCount(), the instrumentation counter). Each dispatch is one
// T9 apply(), itself bounded by its documented budget (at most
// kGenerativeSetCapacity = 16 element iterations of at most two PRNG draws
// plus a <= 25-check scale scan each, plus one gate of <= 137 comparisons
// and <= 2 * 16^2 bounded sort/compare steps per contents check), and the
// composite adds at most 16 fixed-size PatternGenerativeState copies
// (<= 256 bytes each, stack storage, no heap). The whole call therefore
// stays below a small fixed constant in the low thousands of bounded steps
// — no heap allocation, no unbounded loop, no blocking, no I/O, no
// exceptions; the expected worst case draws ~135 PRNG bytes at intensity
// 127 on a max-length pattern, pinned under the
// kCompositeWorstCasePrngDraws sanity cap. SDK-free: sequencer and common
// headers only, so this compiles unchanged in the firmware and the host
// test build.

namespace sequencer {

namespace mutation_composite_detail {

// The number of T9 sub-operators both composites dispatch (the fixed
// composition capacity — the registry-sized bound of the dispatch loop).
constexpr uint8_t kCompositeSubOperatorCount = 8;

// FR6 (composite): Evolve's intensity scaling. The requested 0-127
// intensity maps onto a 1-16 sub-intensity (1 + intensity * 15 / 127), so
// Evolve always mutates gently — at the knob's maximum the per-element
// mutation probability is 32/256 = 12.5% and the density magnitude span is
// 8 of 64 steps — a "small change" that scales with the intensity.
inline uint8_t evolveSubIntensity(uint8_t intensity) {
    return static_cast<uint8_t>(1 + (static_cast<int>(intensity) * 15) / 127);
}

// The eight T9 operators both composites are built from, in the fixed
// dispatch order (parameter operators first, transforms after). Held BY
// VALUE: the composites are self-contained fixed objects — no heap, no
// pointers to external state (each sub-operator is <= 16 bytes, so the
// whole bank is a small fixed member).
struct SubOperatorBank {
    DensityMutation density;
    PitchSetMutation pitchSet;
    VelocitySetMutation velocitySet;
    DurationMutation duration;
    EuclideanParamMutation euclidean;
    RotateMutation rotate;
    InvertMutation invert;
    ShuffleMutation shuffle;

    // Bounded fixed dispatch: index < kCompositeSubOperatorCount (the
    // composite's loop bound); the default arm is unreachable defensively.
    IMutationOperator& at(uint8_t index) {
        switch (index) {
            case 0: return density;
            case 1: return pitchSet;
            case 2: return velocitySet;
            case 3: return duration;
            case 4: return euclidean;
            case 5: return rotate;
            case 6: return invert;
            default: return shuffle;
        }
    }
};

}  // namespace mutation_composite_detail

// The AC4 operation bound of one composite apply() (see the file comment):
// the 8 sub-operator dispatches plus the single finalize gate — the fixed
// maximum the instrumentation counter (lastOperationCount()) reports.
constexpr uint16_t kCompositeWorstCaseOperations =
    mutation_composite_detail::kCompositeSubOperatorCount + 1;

// The AC4 PRNG-draw sanity cap for one worst-case composite apply() at
// intensity 127 on a max-length pattern: generous headroom (~4x) over the
// ~135-draw expected worst case (nextBelow's rejection sampling has no
// hard per-draw bound, so this is the documented empirical ceiling the
// budget test pins rather than a proven limit).
constexpr uint16_t kCompositeWorstCasePrngDraws = 512;

// FR5/FR6/FR7/FR8: the shared composite skeleton. Both concrete composites
// (MutateAll / Evolve) run the same bounded composition; they differ only
// in identity and how the FR6 intensity scales (scaleIntensity).
//
// The apply() obligations are the IMutationOperator contract's, discharged
// exactly like a T9 operator: unsupported target mask -> REJECTED (nothing
// touched), intensity 0 -> NO_CHANGE, and the finalize gate verifies the
// aggregate against view.before, restoring view.state = view.before on
// violation (see the composition-semantics note in the file comment).
class CompositeMutation : public IMutationOperator {
public:
    // AC4 instrumentation: the bounded-iteration count of the LAST apply()
    // call — 0 when a guard (empty mask / intensity 0) short-circuits, else
    // at most kCompositeWorstCaseOperations (8 dispatches + 1 gate). The
    // budget test asserts this never exceeds the documented bound.
    uint16_t lastOperationCount() const { return operationCount_; }

    MutationResult apply(PatternMutationView& view, const MutationConstraints& constraints,
                         Prng& rng) override {
        operationCount_ = 0;

        // FR8 operator control: a request sharing no FR7 target bit is
        // rejected without touching anything (the composites support all
        // seven bits, so only the empty mask misses).
        if ((constraints.targets & supportedTargets()) == 0) {
            return MutationResult{MutationStatus::REJECTED, 0};
        }
        // FR6: intensity 0 is a deliberate NO_CHANGE (never a random no-op).
        if (constraints.intensity == 0) {
            return MutationResult{MutationStatus::NO_CHANGE, 0};
        }

        const uint8_t subIntensity = scaleIntensity(constraints.intensity);
        const PatternGenerativeState entry = view.state;
        PatternGenerativeState candidate = entry;

        // The bounded composition: one dispatch per T9 sub-operator, each
        // gated against the caller's anchor (view.before) so an accepted
        // component already respects the FR8 invariants and distance limit.
        // A component that violates them restores `candidate` to the
        // ANCHOR (its own contract) — the composite undoes that restore to
        // keep the earlier components' work, i.e. the component is simply
        // skipped.
        for (uint8_t i = 0; i < mutation_composite_detail::kCompositeSubOperatorCount; ++i) {
            ++operationCount_;  // AC4: one counted dispatch unit
            IMutationOperator& op = subOperators_.at(i);
            const MutationTargetMask subTargets =
                constraints.targets & op.supportedTargets();
            if (subTargets == 0) continue;  // masked out: not this component's targets

            const MutationConstraints sub(subTargets, subIntensity, constraints.invariants,
                                          constraints.maxDistance);
            const PatternGenerativeState beforeSub = candidate;
            PatternMutationView subView(candidate, view.before, view.patternIndex);
            const MutationResult subResult = op.apply(subView, sub, rng);
            if (subResult.status == MutationStatus::REJECTED) {
                // The component restored the anchor; keep the accumulated
                // composition instead (skip the component).
                candidate = beforeSub;
            }
        }

        // The single composite gate: the truthful aggregate diff plus the
        // contract-mandated invariants / distance check against the
        // caller's anchor (defensive: every accepted component was already
        // gated against it).
        ++operationCount_;
        return mutation_detail::finalize(view, constraints, entry, candidate);
    }

protected:
    // How the request's FR6 intensity scales for the sub-operators:
    // MutateAll passes it through; Evolve reduces it (a small change).
    virtual uint8_t scaleIntensity(uint8_t intensity) const = 0;

private:
    mutation_composite_detail::SubOperatorBank subOperators_;
    uint16_t operationCount_ = 0;
};

// builtin.mutate-all — FR5's "mutate all": every T9 operator whose
// supported targets intersect the request mask runs at the full requested
// intensity; the reported mask aggregates every changed FR7 target.
class MutateAllMutation final : public CompositeMutation {
public:
    const char* id() const override { return "builtin.mutate-all"; }
    const char* name() const override { return "Mutate All"; }
    // The composite can touch every FR7 target family.
    MutationTargetMask supportedTargets() const override { return kAllMutationTargets; }

protected:
    uint8_t scaleIntensity(uint8_t intensity) const override { return intensity; }
};

// builtin.evolve — FR5's "evolve": the same composition at the reduced,
// intensity-scaled sub-intensity, so the result is always a small change
// that stays within the FR8 distance limit.
class EvolveMutation final : public CompositeMutation {
public:
    const char* id() const override { return "builtin.evolve"; }
    const char* name() const override { return "Evolve"; }
    MutationTargetMask supportedTargets() const override { return kAllMutationTargets; }

protected:
    uint8_t scaleIntensity(uint8_t intensity) const override {
        return mutation_composite_detail::evolveSubIntensity(intensity);
    }
};

// Size bounds on the new operator state (global constraint): one vtable
// pointer, the instrumentation word and the fixed by-value sub-operator
// bank (8 operators of <= 16 bytes each) — small fixed objects with no
// dynamic part, storable in the fixed-capacity registry (task 10) and the
// built-in bank below.
static_assert(sizeof(mutation_composite_detail::SubOperatorBank) <= 96,
              "the sub-operator bank stays a small fixed member");
static_assert(sizeof(MutateAllMutation) <= 128, "MutateAllMutation stays a small fixed object");
static_assert(sizeof(EvolveMutation) <= 128, "EvolveMutation stays a small fixed object");
static_assert(mutation_composite_detail::kCompositeSubOperatorCount == 8,
              "the composition dispatches exactly the eight T9 operators");
static_assert(kCompositeWorstCaseOperations == 9,
              "the AC4 bound is the 8 dispatches plus the single gate");

}  // namespace sequencer
