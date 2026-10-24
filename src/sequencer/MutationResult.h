#pragma once

#include <cstdint>
#include <type_traits>

#include "MutationConstraints.h"  // MutationTargetMask / maskOf

// Mutation result (generative-engine-expansion task 8, FR5, FR8).
//
// What one mutation invocation reports back: a three-way status and the
// FR7 target mask of what actually changed. The status distinguishes the
// three outcomes the framework (and the UI) must react to differently:
//
//   APPLIED   — the working state changed; the caller records it on the
//               undo stack (task 12) and reports the changedTargets mask;
//   NO_CHANGE — the operator ran but deliberately changed nothing (for
//               example FR6 intensity 0, or an empty target set); the
//               working state is untouched;
//   REJECTED  — the constraints (FR8) forbade the result: unsupported
//               targets, a violated invariant or a distance-limit breach.
//               The operator RESTORES the pre-mutation state before
//               returning this, so the working state is again untouched.
//
// Global constraints honored here: a plain fixed POD — trivially copyable,
// no heap, no RTTI, no exceptions — so results return by value and can be
// counted/logged without any dynamic part. SDK-free: <cstdint> plus the
// mutation constraint mask header only.

namespace sequencer {

// The three-way outcome of one mutation (see the file comment).
enum class MutationStatus : uint8_t {
    APPLIED = 0,
    NO_CHANGE = 1,
    REJECTED = 2,
};

// One mutation's report: the status plus the mask of FR7 targets that
// actually changed (0 unless the status is APPLIED).
struct MutationResult {
    MutationStatus status = MutationStatus::REJECTED;
    // The MutationTargetMask of what changed (maskOf(MutationTarget::...)).
    MutationTargetMask changedTargets = 0;

    // True when the mutation changed the working state.
    bool applied() const { return status == MutationStatus::APPLIED; }

    // True when the `t` target actually changed.
    bool changed(MutationTarget t) const { return (changedTargets & maskOf(t)) != 0; }
};

// Size bounds on the new POD state (global constraint): one status byte plus
// one mask word, trivially copyable.
static_assert(std::is_trivially_copyable<MutationResult>::value,
              "MutationResult must be trivially copyable");
static_assert(std::is_standard_layout<MutationResult>::value,
              "MutationResult must be standard layout");
static_assert(sizeof(MutationResult) <= 4,
              "MutationResult stays a small fixed POD");

}  // namespace sequencer
