// generative-engine-expansion task 10 — composite mutation operators and
// the fixed-capacity mutation operator registry (FR5, FR6, FR7, FR8, AC4).
//
// Validates the two composite IMutationOperator plugins
// (mutations/CompositeMutations.h):
//   - MutateAllMutation  (builtin.mutate-all) — applies the T9 operators to
//     every target in the constraint mask and aggregates changedTargets;
//   - EvolveMutation     (builtin.evolve)    — a small, intensity-scaled
//     change that always stays within the FR8 distance limit;
// and the fixed-capacity registry (mutations/MutationOperatorRegistry.h):
//   - lookup by id, duplicate / null / over-capacity registration rejected,
//     registration-order enumeration for the UI, and the built-in
//     registration list (registerBuiltInMutations) covering every T9 and
//     T10 operator.
//
// Required cases (plan task 10):
//   1. MutateAll applies to every target in the constraint mask (each of
//      the seven FR7 bits, in isolation) and aggregates changedTargets
//      (the reported mask always equals the material diff);
//   2. Evolve applies a small, intensity-scaled change that stays within
//      the distance limit;
//   3. Composite results respect all invariants (pulse-count lock, the
//      AC2 scale lock over every built-in template/root, all-locks
//      no-change) and the IMutationOperator contract (intensity 0 ->
//      NO_CHANGE, empty mask -> REJECTED, determinism, clean restores);
//   4. Registry lookup/duplicate/capacity/enumeration semantics;
//   5. AC4 budget test: worst-case MutateAll at intensity 127 on a
//      max-length pattern (16 live values per set) completes within the
//      documented operation bound — asserted three ways: the composite's
//      instrumentation counter (the fixed dispatch + gate count), a
//      test-side PRNG-draw counter (the empirical inner-loop iteration
//      count) and a host wall-clock sanity limit.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_composite_mutations". Like
// test_parameter_transform_mutations it compiles common/Scale.cpp directly
// (the scale-aware operators need the shared constraint math) and links NO
// library, NO Pico SDK libs and NO RapidCheck.

#include "mutations/CompositeMutations.h"
#include "mutations/MutationOperatorRegistry.h"

#include "MutationConstraints.h"
#include "MutationResult.h"
#include "PatternGenerativeState.h"

#include "common/Scale.h"
#include "common/ScaleTemplates.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

using sequencer::DensityMutation;
using sequencer::DurationMutation;
using sequencer::EuclideanParamMutation;
using sequencer::EvolveMutation;
using sequencer::IMutationOperator;
using sequencer::InvertMutation;
using sequencer::MutateAllMutation;
using sequencer::MutationConstraints;
using sequencer::MutationInvariant;
using sequencer::MutationInvariantMask;
using sequencer::MutationOperatorRegistry;
using sequencer::MutationResult;
using sequencer::MutationStatus;
using sequencer::MutationTarget;
using sequencer::MutationTargetMask;
using sequencer::PatternGenerativeState;
using sequencer::PatternMutationView;
using sequencer::PitchSetMutation;
using sequencer::Prng;
using sequencer::RotateMutation;
using sequencer::ShuffleMutation;
using sequencer::VelocitySetMutation;

using sequencer::EuclideanGateGenerator;
using sequencer::GeneratorRole;
using sequencer::kAllMutationTargets;
using sequencer::kTransformTargets;
using sequencer::maskOf;
using sequencer::mutationDistance;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// The fixed seed set every statistical expectation runs over (deterministic
// given the fixed seeds — the assertions pin behavior, not luck).
constexpr int kSeedCount = 32;
constexpr uint64_t kSeedBase = 0x9E3779B97F4A7C15ULL;

// A worst-case "max-length pattern" working state: full 16-value material
// sets (kGenerativeSetCapacity), distinct values, and the Euclidean gate at
// 64 steps / 32 pulses / rotation 0 — the configuration that maximizes
// every sub-operator's bounded loop work (AC4).
PatternGenerativeState richState() {
    PatternGenerativeState s;
    s.pitchSet.count = sequencer::kGenerativeSetCapacity;
    for (uint8_t i = 0; i < sequencer::kGenerativeSetCapacity; ++i) {
        // C-major material spanning two octaves (16 distinct in-scale notes).
        const uint8_t table[16] = {48, 50, 52, 53, 55, 57, 59, 60,
                                   62, 64, 65, 67, 69, 71, 72, 74};
        s.pitchSet.values[i] = table[i];
    }
    s.velocitySet.count = sequencer::kGenerativeSetCapacity;
    for (uint8_t i = 0; i < sequencer::kGenerativeSetCapacity; ++i) {
        s.velocitySet.values[i] = static_cast<uint8_t>(8 + 7 * i);
    }
    s.durationSet.count = sequencer::kGenerativeSetCapacity;
    for (uint8_t i = 0; i < sequencer::kGenerativeSetCapacity; ++i) {
        s.durationSet.values[i] = static_cast<uint8_t>(1 + 16 * i);
    }
    s.setGeneratorId(GeneratorRole::GATE, sequencer::kDefaultGateGeneratorId);
    s.setGeneratorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamSteps, 64);
    s.setGeneratorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamPulses, 32);
    s.setGeneratorParam(GeneratorRole::GATE, EuclideanGateGenerator::kParamRotation, 0);
    return s;
}

// One bounded apply() call with the standard working-state flow: the view's
// pre-mutation snapshot (`before`) is a copy of the entry state.
MutationResult runOnce(IMutationOperator& op, PatternGenerativeState& state,
                       const MutationConstraints& constraints, uint64_t seed) {
    const PatternGenerativeState before = state;
    PatternMutationView view(state, before, 3);
    Prng rng(seed);
    return op.apply(view, constraints, rng);
}

// Byte-identical states (memcmp over the fixed POD).
bool sameBytes(const PatternGenerativeState& a, const PatternGenerativeState& b) {
    return std::memcmp(&a, &b, sizeof(PatternGenerativeState)) == 0;
}

// The material diff between two working states — the authoritative
// changed-target mask (mutation_detail::changedTargetsBetween).
MutationTargetMask diffBetween(const PatternGenerativeState& a,
                              const PatternGenerativeState& b) {
    return sequencer::mutation_detail::changedTargetsBetween(a, b);
}

uint8_t popcount16(uint16_t mask) {
    uint8_t n = 0;
    for (uint8_t b = 0; b < 16; ++b) {
        if ((mask & (1u << b)) != 0) ++n;
    }
    return n;
}

// The number of PRNG draws one apply() consumed: xorshift64* is a bijection
// on the 64-bit state, so the draw count is the length of the unique state
// path from the pre-call state to the post-call state (capped). This is the
// fine-grained instrumentation of the composite's INNER bounded loops (one
// draw per per-element mutation decision), complementing the composite's own
// dispatch counter.
uint32_t countPrngDraws(uint64_t beforeState, uint64_t afterState, uint32_t cap) {
    if (beforeState == afterState) return 0;
    Prng p;
    p.state = beforeState;
    for (uint32_t i = 1; i <= cap; ++i) {
        p.next();
        if (p.state == afterState) return i;
    }
    return cap + 1;  // over the cap: report cap+1 so assertions fail loudly
}

// --- case 3a: the IMutationOperator contract on the composites -----------

void test_contract_basics() {
    MutateAllMutation mutateAll;
    EvolveMutation evolve;

    // FR6: intensity 0 is a deliberate NO_CHANGE, byte-identical.
    {
        PatternGenerativeState working = richState();
        const MutationConstraints c(kAllMutationTargets, 0);
        const MutationResult r = runOnce(mutateAll, working, c, 7u);
        check(r.status == MutationStatus::NO_CHANGE, "mutate-all intensity 0 -> NO_CHANGE");
        check(r.changedTargets == 0, "mutate-all intensity 0 reports no targets");
        check(sameBytes(working, richState()), "mutate-all intensity 0 leaves state untouched");
    }
    {
        PatternGenerativeState working = richState();
        const MutationConstraints c(kAllMutationTargets, 0);
        const MutationResult r = runOnce(evolve, working, c, 7u);
        check(r.status == MutationStatus::NO_CHANGE, "evolve intensity 0 -> NO_CHANGE");
        check(sameBytes(working, richState()), "evolve intensity 0 leaves state untouched");
    }

    // FR8 operator control: a request sharing no target bit is REJECTED
    // (the composites support all seven FR7 bits, so only the empty mask
    // misses) — and nothing changes.
    {
        PatternGenerativeState working = richState();
        const MutationConstraints c(0, 127);
        const MutationResult r = runOnce(mutateAll, working, c, 7u);
        check(r.status == MutationStatus::REJECTED, "mutate-all empty mask -> REJECTED");
        check(sameBytes(working, richState()), "mutate-all empty mask leaves state untouched");
    }
    {
        PatternGenerativeState working = richState();
        const MutationConstraints c(0, 127);
        const MutationResult r = runOnce(evolve, working, c, 7u);
        check(r.status == MutationStatus::REJECTED, "evolve empty mask -> REJECTED");
        check(sameBytes(working, richState()), "evolve empty mask leaves state untouched");
    }

    // Determinism: identical (state, constraints, seed) reproduce identical
    // results and identical final states (the global determinism rule).
    {
        const MutationConstraints c(kAllMutationTargets, 96);
        PatternGenerativeState a = richState();
        PatternGenerativeState b = richState();
        const MutationResult ra = runOnce(mutateAll, a, c, 0xC0FFEEULL);
        const MutationResult rb = runOnce(mutateAll, b, c, 0xC0FFEEULL);
        check(ra.status == rb.status && ra.changedTargets == rb.changedTargets,
              "mutate-all is deterministic for identical inputs");
        check(sameBytes(a, b), "mutate-all deterministic final state");
    }
    {
        const MutationConstraints c(kAllMutationTargets, 96);
        PatternGenerativeState a = richState();
        PatternGenerativeState b = richState();
        const MutationResult ra = runOnce(evolve, a, c, 0x5EED01ULL);
        const MutationResult rb = runOnce(evolve, b, c, 0x5EED01ULL);
        check(ra.status == rb.status && ra.changedTargets == rb.changedTargets,
              "evolve is deterministic for identical inputs");
        check(sameBytes(a, b), "evolve deterministic final state");
    }
}

// --- case 1: MutateAll applies to every target + aggregates --------------

void test_mutate_all_every_target() {
    MutateAllMutation mutateAll;

    // Each of the seven FR7 target bits, in isolation: the composite
    // handles it (APPLIED with the requested bit in changedTargets) and
    // touches ONLY that target's material family.
    struct TargetCase {
        MutationTarget target;
        const char* what;
    };
    const TargetCase cases[sequencer::kMutationTargetCount] = {
        {MutationTarget::GATE_DENSITY, "GATE_DENSITY"},
        {MutationTarget::PITCH_CONTENTS, "PITCH_CONTENTS"},
        {MutationTarget::PITCH_ORDERING, "PITCH_ORDERING"},
        {MutationTarget::VELOCITY_CONTENTS, "VELOCITY_CONTENTS"},
        {MutationTarget::VELOCITY_ORDERING, "VELOCITY_ORDERING"},
        {MutationTarget::NOTE_DURATION, "NOTE_DURATION"},
        {MutationTarget::EUCLIDEAN_PARAMS, "EUCLIDEAN_PARAMS"},
    };

    for (uint8_t t = 0; t < sequencer::kMutationTargetCount; ++t) {
        const MutationTargetMask bit = maskOf(cases[t].target);
        const MutationConstraints c(bit, 127);
        bool sawApplied = false;
        for (int s = 0; s < 8; ++s) {
            PatternGenerativeState working = richState();
            const PatternGenerativeState before = working;
            const MutationResult r = runOnce(mutateAll, working, c, kSeedBase + s);
            if (r.status != MutationStatus::APPLIED) continue;
            sawApplied = true;
            check((r.changedTargets & bit) != 0, "single-target run reports the target");
            check(r.changedTargets == diffBetween(before, working),
                  "single-target run aggregates the exact diff");
            check(!sameBytes(before, working), "single-target APPLIED changed the state");
            // Isolation: every family the target does not own is untouched.
            const bool pitchFamily = cases[t].target == MutationTarget::PITCH_CONTENTS ||
                                      cases[t].target == MutationTarget::PITCH_ORDERING;
            const bool velocityFamily =
                cases[t].target == MutationTarget::VELOCITY_CONTENTS ||
                cases[t].target == MutationTarget::VELOCITY_ORDERING;
            const bool durationFamily = cases[t].target == MutationTarget::NOTE_DURATION;
            const bool gateFamily = cases[t].target == MutationTarget::GATE_DENSITY ||
                                    cases[t].target == MutationTarget::EUCLIDEAN_PARAMS;
            if (!pitchFamily) {
                check(std::memcmp(&before.pitchSet, &working.pitchSet,
                                  sizeof(working.pitchSet)) == 0,
                      "non-pitch target leaves the pitch set untouched");
            }
            if (!velocityFamily) {
                check(std::memcmp(&before.velocitySet, &working.velocitySet,
                                  sizeof(working.velocitySet)) == 0,
                      "non-velocity target leaves the velocity set untouched");
            }
            if (!durationFamily) {
                check(std::memcmp(&before.durationSet, &working.durationSet,
                                  sizeof(working.durationSet)) == 0,
                      "non-duration target leaves the duration set untouched");
            }
            if (!gateFamily) {
                check(std::memcmp(before.generatorParams[static_cast<uint8_t>(
                                      GeneratorRole::GATE)],
                                  working.generatorParams[static_cast<uint8_t>(
                                      GeneratorRole::GATE)],
                                  sizeof(working.generatorParams[0])) == 0,
                      "non-gate target leaves the gate parameters untouched");
            }
            if (cases[t].target == MutationTarget::GATE_DENSITY) {
                // The density family owns ONLY the pulse count: steps and
                // rotation stay fixed.
                check(working.generatorParam(GeneratorRole::GATE,
                                              EuclideanGateGenerator::kParamSteps) ==
                          before.generatorParam(GeneratorRole::GATE,
                                                EuclideanGateGenerator::kParamSteps),
                      "GATE_DENSITY leaves the step count fixed");
                check(working.generatorParam(GeneratorRole::GATE,
                                              EuclideanGateGenerator::kParamRotation) ==
                          before.generatorParam(GeneratorRole::GATE,
                                                EuclideanGateGenerator::kParamRotation),
                      "GATE_DENSITY leaves the rotation fixed");
            }
            if (cases[t].target == MutationTarget::EUCLIDEAN_PARAMS) {
                check(working.generatorParam(GeneratorRole::GATE,
                                              EuclideanGateGenerator::kParamPulses) ==
                          before.generatorParam(GeneratorRole::GATE,
                                                EuclideanGateGenerator::kParamPulses),
                      "EUCLIDEAN_PARAMS preserves the pulse count");
            }
        }
        check(sawApplied, "mutate-all applied for the isolated target");
    }

    // The full mask: every seed's reported mask equals the material diff,
    // and across the seed set the aggregate covers all seven FR7 bits.
    {
        const MutationConstraints c(kAllMutationTargets, 127);
        MutationTargetMask unionMask = 0;
        uint8_t maxBits = 0;
        for (int s = 0; s < kSeedCount; ++s) {
            PatternGenerativeState working = richState();
            const PatternGenerativeState before = working;
            const MutationResult r = runOnce(mutateAll, working, c, kSeedBase + s);
            check(r.status == MutationStatus::APPLIED,
                  "full-mask mutate-all at intensity 127 changes the state");
            check(r.changedTargets == diffBetween(before, working),
                  "full-mask aggregate equals the exact diff");
            unionMask = static_cast<MutationTargetMask>(unionMask | r.changedTargets);
            const uint8_t bits = popcount16(r.changedTargets);
            if (bits > maxBits) maxBits = bits;
        }
        check(unionMask == kAllMutationTargets,
              "the aggregate over seeds covers all seven FR7 targets");
        check(maxBits >= 4, "a full-mask run changes several targets at once");
    }
}

// --- case 2: Evolve is small, intensity-scaled, distance-bounded ----------

// The average mutationDistance of `op` at `intensity` over the fixed seed
// set (unlimited distance so every change counts).
double averageDistance(IMutationOperator& op, uint16_t intensity) {
    const MutationConstraints c(kAllMutationTargets, intensity);
    uint32_t total = 0;
    int applied = 0;
    for (int s = 0; s < kSeedCount; ++s) {
        PatternGenerativeState working = richState();
        const PatternGenerativeState before = working;
        const MutationResult r = runOnce(op, working, c, kSeedBase + s);
        if (r.status == MutationStatus::APPLIED) {
            total += mutationDistance(before, working);
            ++applied;
        }
    }
    return applied == 0 ? 0.0 : static_cast<double>(total) / applied;
}

void test_evolve_small_scaled_bounded() {
    MutateAllMutation mutateAll;
    EvolveMutation evolve;

    // Small: at the same intensity, evolve drifts far less than mutate-all.
    const double evolve127 = averageDistance(evolve, 127);
    const double all127 = averageDistance(mutateAll, 127);
    check(evolve127 > 0.0, "evolve at intensity 127 changes the state");
    check(evolve127 < all127, "evolve at 127 is smaller than mutate-all at 127");

    // Intensity-scaled: the drift grows with the intensity.
    const double evolve32 = averageDistance(evolve, 32);
    check(evolve32 > 0.0, "evolve at intensity 32 changes the state");
    check(evolve32 < evolve127, "evolve drifts more at 127 than at 32");

    // Stays within the distance limit: with a tight limit, evolve at maximum
    // intensity still APPLIES for EVERY seed and never exceeds the limit
    // (the density component alone fits the budget, so a graceful-skip
    // composite always evolves something).
    {
        const MutationConstraints c(kAllMutationTargets, 127, 0, 12);
        for (int s = 0; s < kSeedCount; ++s) {
            PatternGenerativeState working = richState();
            const PatternGenerativeState before = working;
            const MutationResult r = runOnce(evolve, working, c, kSeedBase + s);
            check(r.status == MutationStatus::APPLIED,
                  "evolve at 127 within distance 12 applies for every seed");
            check(mutationDistance(before, working) <= 12,
                  "evolve result stays within the distance limit");
            check(r.changedTargets == diffBetween(before, working),
                  "evolve aggregate equals the exact diff");
        }
        // Mutate-all under the same tight budget: still bounded (every
        // APPLIED result within the limit — the composite's guarantee).
        for (int s = 0; s < kSeedCount; ++s) {
            PatternGenerativeState working = richState();
            const PatternGenerativeState before = working;
            const MutationResult r = runOnce(mutateAll, working, c, kSeedBase + s);
            check(mutationDistance(before, working) <= 12,
                  "mutate-all result stays within the distance limit");
            check(r.status != MutationStatus::REJECTED || sameBytes(before, working),
                  "mutate-all REJECTED restores the state byte-identically");
        }
    }
}

// --- case 3b: composite results respect all invariants --------------------

void test_invariants() {
    MutateAllMutation mutateAll;
    EvolveMutation evolve;

    // PRESERVE_PULSE_COUNT: the density component is skipped; every APPLIED
    // result keeps the pulse count and reports no GATE_DENSITY change.
    {
        const MutationConstraints c(
            kAllMutationTargets, 127,
            maskOf(MutationInvariant::PRESERVE_PULSE_COUNT));
        for (int s = 0; s < 8; ++s) {
            PatternGenerativeState working = richState();
            const PatternGenerativeState before = working;
            const MutationResult r = runOnce(mutateAll, working, c, kSeedBase + s);
            check(r.status != MutationStatus::REJECTED,
                  "pulse-count-locked mutate-all is not wholesale rejected");
            if (r.status != MutationStatus::APPLIED) continue;
            check((r.changedTargets & maskOf(MutationTarget::GATE_DENSITY)) == 0,
                  "pulse count preserved: GATE_DENSITY not reported");
            check(working.generatorParam(GeneratorRole::GATE,
                                          EuclideanGateGenerator::kParamPulses) ==
                      before.generatorParam(GeneratorRole::GATE,
                                            EuclideanGateGenerator::kParamPulses),
                  "pulse count preserved in the state");
            check(c.violatedInvariants(before, working) == 0,
                  "no configured invariant is violated");
        }
    }

    // AC2 SCALE_LOCK over EVERY built-in template and root: every pitch in
    // an APPLIED composite result stays in the request's global scale
    // (ui-integration T16: carried by the constraints, not the state).
    {
        const int templates = common::scaleTemplateCount();
        MutationConstraints c(kAllMutationTargets, 127,
                              maskOf(MutationInvariant::SCALE_LOCK));
        for (int t = 0; t < templates; ++t) {
            for (uint8_t root = 0; root < 12; ++root) {
                PatternGenerativeState working = richState();
                c.scale = common::Scale(root, static_cast<uint8_t>(t));
                const PatternGenerativeState before = working;
                const MutationResult r = runOnce(mutateAll, working, c, kSeedBase + t * 12 + root);
                if (r.status != MutationStatus::APPLIED) continue;
                check(c.violatedInvariants(before, working) == 0,
                      "scale lock holds for every template and root");
            }
        }
    }

    // Locked pitch ordering: the pitch material survives byte-identically
    // (every component that could reorder or replace it skips itself).
    {
        const MutationConstraints c(
            kAllMutationTargets, 127,
            maskOf(MutationInvariant::LOCK_PITCH_ORDERING));
        for (int s = 0; s < 8; ++s) {
            PatternGenerativeState working = richState();
            const PatternGenerativeState before = working;
            const MutationResult r = runOnce(mutateAll, working, c, kSeedBase + s);
            check(r.status != MutationStatus::REJECTED,
                  "ordering-locked mutate-all is not wholesale rejected");
            check(std::memcmp(&before.pitchSet, &working.pitchSet,
                              sizeof(working.pitchSet)) == 0,
                  "locked pitch ordering leaves the pitch set byte-identical");
            if (r.status == MutationStatus::APPLIED) {
                check(c.violatedInvariants(before, working) == 0,
                      "no configured invariant is violated");
            }
        }
    }

    // Every field locked at once: nothing can change, so both composites
    // report a deliberate NO_CHANGE and the state stays byte-identical.
    {
        const MutationInvariantMask allLocks =
            maskOf(MutationInvariant::SCALE_LOCK) | maskOf(MutationInvariant::LOCK_PITCH_CONTENTS) |
            maskOf(MutationInvariant::LOCK_PITCH_ORDERING) |
            maskOf(MutationInvariant::LOCK_VELOCITY_CONTENTS) |
            maskOf(MutationInvariant::LOCK_VELOCITY_ORDERING) |
            maskOf(MutationInvariant::LOCK_NOTE_DURATION) |
            maskOf(MutationInvariant::LOCK_EUCLIDEAN_PARAMS) |
            maskOf(MutationInvariant::PRESERVE_PULSE_COUNT) |
            maskOf(MutationInvariant::LOCK_GENERATORS);
        const MutationConstraints c(kAllMutationTargets, 127, allLocks);
        for (int s = 0; s < 8; ++s) {
            PatternGenerativeState working = richState();
            const PatternGenerativeState before = working;
            const MutationResult r = runOnce(mutateAll, working, c, kSeedBase + s);
            check(r.status == MutationStatus::NO_CHANGE,
                  "fully locked mutate-all reports NO_CHANGE");
            check(sameBytes(before, working),
                  "fully locked mutate-all leaves the state byte-identical");
        }
        for (int s = 0; s < 8; ++s) {
            PatternGenerativeState working = richState();
            const PatternGenerativeState before = working;
            const MutationResult r = runOnce(evolve, working, c, kSeedBase + s);
            check(r.status == MutationStatus::NO_CHANGE, "fully locked evolve reports NO_CHANGE");
            check(sameBytes(before, working),
                  "fully locked evolve leaves the state byte-identical");
        }
    }
}

// --- case 4: the fixed-capacity registry -----------------------------------

// A test-only operator with a configurable id (or a null id, to pin the
// registry's null-id rejection).
class StubOperator final : public IMutationOperator {
public:
    StubOperator() = default;
    explicit StubOperator(const char* id, bool nullId = false) : nullId_(nullId) {
        if (!nullId && id != nullptr) {
            uint8_t i = 0;
            while (id[i] != '\0' && i + 1 < sizeof(id_)) {
                id_[i] = id[i];
                ++i;
            }
            id_[i] = '\0';
        }
    }

    const char* id() const override { return nullId_ ? nullptr : id_; }
    const char* name() const override { return "Stub"; }
    sequencer::MutationTargetMask supportedTargets() const override { return 0; }
    MutationResult apply(PatternMutationView&, const MutationConstraints&, Prng&) override {
        return MutationResult{MutationStatus::NO_CHANGE, 0};
    }

private:
    char id_[24] = {};
    bool nullId_ = false;
};

void test_registry() {
    // Lookup by id + the full built-in list: all ten T9/T10 operators.
    MutationOperatorRegistry registry;
    sequencer::registerBuiltInMutations(registry);
    check(registry.count() == 10, "the built-in registration lists ten operators");

    struct Expected {
        const char* id;
        const char* name;
        MutationTargetMask targets;
    };
    const Expected expected[10] = {
        {"builtin.mutate-density", "Density", maskOf(MutationTarget::GATE_DENSITY)},
        {"builtin.mutate-pitch-set", "Pitch Set", maskOf(MutationTarget::PITCH_CONTENTS)},
        {"builtin.mutate-velocity-set", "Velocity Set",
         maskOf(MutationTarget::VELOCITY_CONTENTS)},
        {"builtin.mutate-duration", "Duration", maskOf(MutationTarget::NOTE_DURATION)},
        {"builtin.mutate-euclidean", "Euclidean Params",
         maskOf(MutationTarget::EUCLIDEAN_PARAMS)},
        {"builtin.rotate", "Rotate", kTransformTargets},
        {"builtin.invert", "Invert", kTransformTargets},
        {"builtin.shuffle", "Shuffle", kTransformTargets},
        {"builtin.mutate-all", "Mutate All", kAllMutationTargets},
        {"builtin.evolve", "Evolve", kAllMutationTargets},
    };
    for (uint8_t i = 0; i < 10; ++i) {
        IMutationOperator* op = registry.find(expected[i].id);
        check(op != nullptr, "built-in mutation found by id");
        if (op == nullptr) continue;
        check(std::strcmp(op->name(), expected[i].name) == 0, "built-in name matches");
        check(op->supportedTargets() == expected[i].targets,
              "built-in supportedTargets matches");
    }

    // Misses: an unknown id and a null id.
    check(registry.find("no.such.operator") == nullptr, "unknown id misses");
    check(registry.find(nullptr) == nullptr, "null id misses");

    // Enumeration for the UI: registration order, nullptr past the count
    // (and on an empty registry), clear() empties, re-registration works.
    {
        MutationOperatorRegistry fresh;
        check(fresh.count() == 0, "fresh registry is empty");
        check(fresh.at(0) == nullptr, "fresh registry at(0) is null");
        StubOperator a("stub.a"), b("stub.b");
        check(fresh.add(&a) && fresh.add(&b), "stub registration succeeds");
        check(fresh.count() == 2, "count tracks registrations");
        check(fresh.at(0) == &a && fresh.at(1) == &b,
              "enumeration yields registration order");
        check(fresh.at(2) == nullptr, "at(count) is null");
        check(std::strcmp(fresh.at(0)->id(), "stub.a") == 0,
              "the UI can read id() through enumeration");
        fresh.clear();
        check(fresh.count() == 0 && fresh.at(0) == nullptr, "clear empties the registry");
        check(fresh.add(&a), "re-registration after clear succeeds");
    }

    // Duplicate registration is rejected (and never disturbs the list).
    {
        MutationOperatorRegistry local;
        sequencer::registerBuiltInMutations(local);
        const uint8_t before = local.count();
        check(!local.add(local.find("builtin.rotate")),
              "duplicate id registration is rejected");
        check(local.count() == before, "duplicate rejection leaves the list intact");
        // Re-running the built-in registration is safe (all duplicates).
        sequencer::registerBuiltInMutations(local);
        check(local.count() == before, "repeated built-in registration is idempotent");
    }

    // Null pointer / null id rejection.
    {
        MutationOperatorRegistry local;
        check(!local.add(nullptr), "null operator registration is rejected");
        StubOperator nullId("ignored", true);
        check(!local.add(&nullId), "null id registration is rejected");
        check(local.count() == 0, "rejected registrations leave the registry empty");
    }

    // Over-capacity registration is rejected: fill the fixed capacity with
    // stubs, then the next add fails without modifying the registry.
    {
        MutationOperatorRegistry local;
        StubOperator stubs[sequencer::MutationOperatorRegistry::MAX_OPERATORS];
        bool allAdded = true;
        for (uint8_t i = 0; i < sequencer::MutationOperatorRegistry::MAX_OPERATORS; ++i) {
            char id[16];
            std::snprintf(id, sizeof(id), "stub.%u", static_cast<unsigned>(i));
            stubs[i] = StubOperator(id);
            if (!local.add(&stubs[i])) allAdded = false;
        }
        check(allAdded, "the registry holds its full fixed capacity");
        check(local.count() == sequencer::MutationOperatorRegistry::MAX_OPERATORS,
              "count equals the fixed capacity");
        StubOperator overflow("stub.overflow");
        check(!local.add(&overflow), "over-capacity registration is rejected");
        check(local.count() == sequencer::MutationOperatorRegistry::MAX_OPERATORS,
              "the rejected overflow add does not grow the registry");
    }
}

// --- case 5: the AC4 budget ------------------------------------------------

void test_ac4_budget() {
    MutateAllMutation mutateAll;
    EvolveMutation evolve;

    const MutationConstraints worstCase(kAllMutationTargets, 127);

    // (a) The composite's instrumentation counter: one counted unit per
    // sub-operator dispatch plus exactly one finalize gate. The guards
    // (empty mask / intensity 0) count nothing; the worst case counts
    // exactly the documented fixed bound.
    {
        PatternGenerativeState working = richState();
        runOnce(mutateAll, working, MutationConstraints(0, 127), 1u);
        check(mutateAll.lastOperationCount() == 0,
              "empty-mask guard performs zero counted operations");
        runOnce(mutateAll, working, MutationConstraints(kAllMutationTargets, 0), 1u);
        check(mutateAll.lastOperationCount() == 0,
              "intensity-0 guard performs zero counted operations");
        runOnce(mutateAll, working, worstCase, 1u);
        check(mutateAll.lastOperationCount() ==
                  sequencer::kCompositeWorstCaseOperations,
              "worst-case mutate-all counts exactly the documented bound");
        check(mutateAll.lastOperationCount() <= sequencer::kCompositeWorstCaseOperations,
              "the operation counter never exceeds the bound");
        runOnce(evolve, working, worstCase, 1u);
        check(evolve.lastOperationCount() == sequencer::kCompositeWorstCaseOperations,
              "worst-case evolve counts exactly the documented bound");
    }

    // (b) Fine-grained inner-loop instrumentation: the PRNG draw count of
    // the worst-case run (one draw per per-element mutation decision),
    // bridged test-side along the xorshift state path. The documented
    // sanity cap is kCompositeWorstCasePrngDraws (generous headroom over
    // the ~135-draw expected worst case; the rejection-sampled nextBelow
    // makes a hard per-draw bound impossible, so the cap is a documented
    // empirical ceiling).
    {
        uint32_t maxDraws = 0;
        for (int s = 0; s < kSeedCount; ++s) {
            PatternGenerativeState working = richState();
            const PatternGenerativeState before = working;
            PatternMutationView view(working, before, 0);
            Prng rng(kSeedBase + s);
            const uint64_t stateBefore = rng.state;
            (void)mutateAll.apply(view, worstCase, rng);
            const uint64_t stateAfter = rng.state;
            const uint32_t draws = countPrngDraws(stateBefore, stateAfter, 4096);
            if (draws > maxDraws) maxDraws = draws;
        }
        check(maxDraws <= sequencer::kCompositeWorstCasePrngDraws,
              "worst-case PRNG draw count stays under the documented cap");
        std::printf("  AC4: worst-case mutate-all used at most %u PRNG draws"
                    " (cap %u), %u counted dispatch units\n",
                    maxDraws, static_cast<unsigned>(sequencer::kCompositeWorstCasePrngDraws),
                    static_cast<unsigned>(sequencer::kCompositeWorstCaseOperations));
    }

    // (c) Host wall-clock sanity limit: the worst-case run is far below the
    // documented host ceiling (2000 runs inside two seconds).
    {
        PatternGenerativeState working = richState();
        PatternGenerativeState before = working;
        PatternMutationView view(working, before, 0);
        Prng rng(1);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 2000; ++i) {
            working = richState();
            before = working;
            (void)mutateAll.apply(view, worstCase, rng);
        }
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0)
                .count();
        check(elapsed < 2000, "2000 worst-case runs stay under the wall-clock sanity limit");
        std::printf("  AC4: 2000 worst-case mutate-all runs took %lld ms\n",
                    static_cast<long long>(elapsed));
    }
}

}  // namespace

int main() {
    test_contract_basics();
    test_mutate_all_every_target();
    test_evolve_small_scaled_bounded();
    test_invariants();
    test_registry();
    test_ac4_budget();

    if (failures == 0) {
        std::printf("test_composite_mutations: all checks passed\n");
        return 0;
    }
    std::printf("test_composite_mutations: %d check(s) failed\n", failures);
    return 1;
}
