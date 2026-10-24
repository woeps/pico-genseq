#pragma once

#include <cstdint>
#include <type_traits>

#include "IGenerator.h"           // GeneratorRng (the shared deterministic PRNG)
#include "MutationConstraints.h"   // FR6-FR8 constraint value type + checks
#include "MutationResult.h"        // the APPLIED/NO_CHANGE/REJECTED report
#include "PatternGenerativeState.h"  // the working state being mutated

// Mutation operator interface (generative-engine-expansion task 8, FR5,
// FR8).
//
// IMutationOperator is the contract every mutation operator implements
// (the FR5 parameter / transform / composite operators arrive in tasks 9
// and 10). One call hands the operator:
//
//   - a PatternMutationView — the bounded window over ONE pattern's
//     working state it may edit, plus the pre-mutation snapshot the
//     FR8 constraint checks are measured against;
//   - MutationConstraints — the FR6 intensity, the FR7 target mask and the
//     FR8 invariants / distance limit for this request;
//   - a Prng — the deterministic, seedable randomness source (no operator
//     keeps hidden state: identical (view, constraints, seed) reproduce
//     identical results).
//
// Operator obligations (the contract the tests pin):
//   1. FR8 operator control: when the requested target mask shares no bit
//      with supportedTargets(), return REJECTED and change nothing;
//   2. FR6: intensity 0 means NO_CHANGE (never a random no-op);
//   3. FR8: before returning APPLIED, verify the result with
//      constraints.invariantsHold(...) and constraints.withinDistance(...)
//      (measured against view.before); on violation restore
//      view.state = view.before and return REJECTED;
//   4. report the changed FR7 targets in MutationResult::changedTargets.
//
// AC4 bounded-operation contract: apply() performs only fixed-capacity
// loops (kGenerativeSetCapacity = 16 material values, kGeneratorRoleCount =
// 4 roles, kMaxGeneratorParams = 8 parameters), so a whole call is bounded
// by a small documented constant — the constraint helpers cost at most 137
// comparisons (distance) plus at most 2 * 16^2 bounded steps per
// contents-equality check, and an operator adds no more than one bounded
// pass per target it touches. No heap allocation, no unbounded loop, no
// blocking, no I/O, no exceptions. SDK-free: sequencer headers only.

namespace sequencer {

// The mutation framework's deterministic, seedable PRNG. FR8 / the global
// determinism constraint: all randomness flows through one shared,
// seedable PRNG type — the generator framework's GeneratorRng — so mutation
// results are reproducible from (seed, view, constraints) and no operator
// hides mutable state.
using Prng = GeneratorRng;

// The bounded window over ONE pattern's mutable working state that one
// apply() call may touch:
//
//   - state: the live PatternGenerativeState to mutate (the same object the
//     working state manager, task 12, hands out);
//   - before: the pre-mutation snapshot of that state — the FR8 anchor the
//     distance limit is measured from and the "before" side of every
//     invariant check, and what a REJECTED operator restores. Callers that
//     configure no invariants and an unlimited distance may alias it with
//     state (the checks are then vacuous but harmless);
//   - patternIndex: which pattern slot is being mutated (0-15), for
//     operators that need to identify the pattern (e.g. logging targets).
//
// A view is a call-scoped reference pair, never stored.
struct PatternMutationView {
    PatternGenerativeState& state;
    const PatternGenerativeState& before;
    uint8_t patternIndex = 0;

    PatternMutationView(PatternGenerativeState& stateIn,
                        const PatternGenerativeState& beforeIn,
                        uint8_t patternIndexIn)
        : state(stateIn), before(beforeIn), patternIndex(patternIndexIn) {}
};

// FR5/FR8: the mutation operator interface. Concrete operators are plain
// classes implementing this contract; a fixed-capacity registry (task 10)
// holds them and the UI lists them by id()/name() with supportedTargets()
// restricting the selectable FR7 target mask (operator control).
class IMutationOperator {
public:
    virtual ~IMutationOperator() = default;

    // Stable, unique operator id (e.g. "builtin.rotate"), for dispatch and
    // persistence.
    virtual const char* id() const = 0;
    // Human-readable display name for the UI.
    virtual const char* name() const = 0;
    // The MutationTargetMask of FR7 targets this operator can mutate; the
    // UI offers only these (FR8 operator control).
    virtual MutationTargetMask supportedTargets() const = 0;

    // Apply one mutation. Bounded and allocation-free (see the file
    // comment); the result reports the status and the changed-target mask.
    // `view.state` is only modified on an APPLIED return — every REJECTED /
    // NO_CHANGE path leaves it equal to `view.before`.
    virtual MutationResult apply(PatternMutationView& view,
                                 const MutationConstraints& constraints,
                                 Prng& rng) = 0;
};

// The mutation PRNG is the shared generator PRNG state (8 bytes, no heap).
static_assert(std::is_trivially_copyable<Prng>::value,
              "the mutation PRNG must be trivially copyable");
static_assert(sizeof(Prng) <= 8, "the mutation PRNG fits one machine word");

}  // namespace sequencer
