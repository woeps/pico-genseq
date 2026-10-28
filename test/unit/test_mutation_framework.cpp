// generative-engine-expansion task 8 — mutation operator architecture
// (FR5-FR8).
//
// Validates the mutation framework contract:
//   - MutationConstraints: the FR7 target bitmask set/test/clear for every
//     target, the FR6 intensity clamped to 0-127, the FR8 distance-limit
//     helper over two PatternGenerativeState snapshots, and the FR8
//     invariant checks (scale lock via the request's global scale, locked
//     preserved pulse count);
//   - IMutationOperator: a test-only stub operator reports its
//     supportedTargets, its changed-target mask and its status
//     (Applied / NoChange / Rejected), restoring the state on rejection.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/ so
// the property-test glob (test/*.cpp) never sweeps it; registered in the host
// test build under the ctest name "test_mutation_framework".

#include "IMutationOperator.h"
#include "MutationConstraints.h"
#include "MutationResult.h"
#include "PatternGenerativeState.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

using sequencer::GeneratorRole;
using sequencer::GenerativeValueSet;
using sequencer::IMutationOperator;
using sequencer::MutationConstraints;
using sequencer::MutationInvariant;
using sequencer::MutationResult;
using sequencer::MutationStatus;
using sequencer::MutationTarget;
using sequencer::PatternGenerativeState;
using sequencer::PatternMutationView;
using sequencer::Prng;

// Compile-time mirrors of the framework's fixed budgets (the same convention
// as test_undo_stack): the test target fails to build if the constraint or
// result PODs ever grow past their fixed budgets, or the FR7 target set
// drifts from exactly seven bits.
static_assert(sequencer::kMutationTargetCount == 7,
              "FR7: exactly the seven mutation targets");
static_assert(sequencer::kAllMutationTargets == 0x007Fu,
              "the all-targets mask covers exactly the seven FR7 bits");
static_assert(std::is_trivially_copyable<MutationConstraints>::value,
              "MutationConstraints must be trivially copyable (undo snapshots)");
static_assert(sizeof(MutationConstraints) <= 8,
              "MutationConstraints stays a small fixed POD");
static_assert(std::is_trivially_copyable<MutationResult>::value,
              "MutationResult must be trivially copyable");
static_assert(sizeof(MutationResult) <= 4,
              "MutationResult stays a small fixed POD");
static_assert(std::is_trivially_copyable<Prng>::value,
              "the mutation PRNG stays a small fixed state");
static_assert(sizeof(Prng) <= 8, "the mutation PRNG fits one machine word");

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// All seven FR7 targets in bit order.
const MutationTarget kTargets[] = {
    MutationTarget::GATE_DENSITY,
    MutationTarget::PITCH_CONTENTS,
    MutationTarget::PITCH_ORDERING,
    MutationTarget::VELOCITY_CONTENTS,
    MutationTarget::VELOCITY_ORDERING,
    MutationTarget::NOTE_DURATION,
    MutationTarget::EUCLIDEAN_PARAMS,
};

// A well-formed working state: C-E-G pitch material (in the factory global
// C-Major scale), two velocities, two durations.
PatternGenerativeState majorPitchState() {
    PatternGenerativeState s;
    s.pitchSet.count = 3;
    s.pitchSet.values[0] = 60;
    s.pitchSet.values[1] = 64;
    s.pitchSet.values[2] = 67;
    s.velocitySet.count = 2;
    s.velocitySet.values[0] = 40;
    s.velocitySet.values[1] = 100;
    s.durationSet.count = 2;
    s.durationSet.values[0] = 24;
    s.durationSet.values[1] = 48;
    return s;
}

// --- (1) FR7 target bitmask: set / test / clear -----------------------------

void test_target_bitmask() {
    // Every FR7 target is a distinct, non-overlapping bit.
    for (uint8_t i = 0; i < sequencer::kMutationTargetCount; ++i) {
        for (uint8_t j = 0; j < sequencer::kMutationTargetCount; ++j) {
            if (i == j) continue;
            check((sequencer::maskOf(kTargets[i]) & sequencer::maskOf(kTargets[j])) == 0,
                  "every FR7 target occupies a distinct bit");
        }
    }

    MutationConstraints c;
    check(c.targets == 0, "fresh constraints target nothing");
    check(!c.hasTarget(MutationTarget::GATE_DENSITY),
          "an unset target tests false");

    // set / test / clear works for every target, and re-adding is idempotent.
    for (const MutationTarget t : kTargets) {
        c.addTarget(t);
        check(c.hasTarget(t), "addTarget sets the target bit");
        check(c.targets == sequencer::maskOf(t),
              "after one addTarget the mask holds exactly that bit");
        c.addTarget(t);
        check(c.targets == sequencer::maskOf(t),
              "re-adding a target is idempotent");
        c.clearTarget(t);
        check(!c.hasTarget(t), "clearTarget clears the target bit");
        check(c.targets == 0, "after clearTarget the mask is empty again");
    }

    // All seven together form the all-targets mask.
    for (const MutationTarget t : kTargets) c.addTarget(t);
    check(c.targets == sequencer::kAllMutationTargets,
          "the seven targets combine into the all-targets mask");
    c.clearTarget(MutationTarget::NOTE_DURATION);
    check(!c.hasTarget(MutationTarget::NOTE_DURATION),
          "clearing one target leaves the others set");

    // Enum-level combination helper.
    check(sequencer::maskOf(MutationTarget::PITCH_CONTENTS |
                            MutationTarget::PITCH_ORDERING) ==
              (sequencer::maskOf(MutationTarget::PITCH_CONTENTS) |
               sequencer::maskOf(MutationTarget::PITCH_ORDERING)),
          "operator| combines target bits");

    // The constructor carries a target mask.
    MutationConstraints cc(sequencer::maskOf(MutationTarget::EUCLIDEAN_PARAMS), 0);
    check(cc.hasTarget(MutationTarget::EUCLIDEAN_PARAMS),
          "the constructor accepts a target mask");
    check(!cc.hasTarget(MutationTarget::GATE_DENSITY),
          "unrequested targets stay unset after construction");
}

// --- (2) FR6 intensity clamps to 0-127 ---------------------------------------

void test_intensity_clamp() {
    MutationConstraints c;
    check(c.intensity == 0, "the default intensity is 0");

    c.setIntensity(127);
    check(c.intensity == 127, "127 is in range and kept");
    c.setIntensity(128);
    check(c.intensity == 127, "128 clamps down to 127");
    c.setIntensity(200);
    check(c.intensity == 127, "200 clamps to 127");
    c.setIntensity(1000);
    check(c.intensity == 127, "1000 clamps to 127");
    c.setIntensity(0);
    check(c.intensity == 0, "0 stays 0");
    c.setIntensity(64);
    check(c.intensity == 64, "a mid intensity is kept verbatim");

    // The constructor clamps the same way (FR6: the parameter is 0-127).
    check(MutationConstraints(0, 255).intensity == 127,
          "the constructor clamps 255 to 127");
    check(MutationConstraints(0, 130).intensity == 127,
          "the constructor clamps 130 to 127");
    check(MutationConstraints(0, 127).intensity == 127,
          "the constructor keeps 127");
    check(MutationConstraints(0, 42).intensity == 42,
          "the constructor keeps an in-range intensity");
}

// --- (3) FR8 distance limit between two state snapshots ----------------------

void test_distance_limit() {
    const PatternGenerativeState a = majorPitchState();

    check(sequencer::mutationDistance(a, a) == 0,
          "identical snapshots have distance 0");

    PatternGenerativeState b = a;
    b.pitchSet.values[0] = 72;
    check(sequencer::mutationDistance(a, b) == 1,
          "one changed pitch value costs distance 1");
    check(sequencer::mutationDistance(b, a) == 1,
          "the distance metric is symmetric");

    b = a;
    b.pitchSet.count = 4;
    b.pitchSet.values[3] = 72;
    check(sequencer::mutationDistance(a, b) == 1,
          "a grown set costs its count difference");

    b = a;
    b.pitchSet.order = common::PlayingOrder::BACKWARDS;
    check(sequencer::mutationDistance(a, b) == 1,
          "a changed playing order costs distance 1");

    b = a;
    b.setGeneratorParam(GeneratorRole::PITCH, 3, 9);
    check(sequencer::mutationDistance(a, b) == 1,
          "one changed generator parameter costs distance 1");

    b = a;
    b.setGeneratorId(GeneratorRole::GATE, "fake.gate");
    check(sequencer::mutationDistance(a, b) == 1,
          "a changed generator assignment costs distance 1");

    b = a;
    b.pitchSet.values[0] = 72;
    b.velocitySet.values[0] = 41;
    check(sequencer::mutationDistance(a, b) == 2,
          "two independent changes cost distance 2");

    // Distance-limit reporting (FR8): maxDistance gates the candidate.
    MutationConstraints none(0, 0, 0, 0);  // maxDistance 0
    check(none.withinDistance(a, a),
          "maxDistance 0 still admits an identical state");
    b = a;
    b.pitchSet.values[0] = 72;
    check(!none.withinDistance(a, b),
          "maxDistance 0 reports a one-step change as a violation");

    MutationConstraints one(0, 0, 0, 1);
    check(one.withinDistance(a, b),
          "maxDistance 1 admits a one-step change");
    b = a;
    b.pitchSet.values[0] = 72;
    b.pitchSet.values[1] = 76;
    b.pitchSet.values[2] = 79;
    check(!one.withinDistance(a, b),
          "maxDistance 1 reports a three-step change as a violation");

    // The default maxDistance is effectively unlimited.
    MutationConstraints dflt;
    check(dflt.withinDistance(a, b),
          "the default distance limit admits any bounded change");
}

// --- (4) FR8 invariant checks ------------------------------------------------

void test_invariants() {
    const PatternGenerativeState base = majorPitchState();

    // Scale lock (AC2): every pitch must stay in the GLOBAL scale the
    // request carries (ui-integration T16 — never a per-pattern field).
    // The factory default is C-Major, which majorPitchState() is in.
    MutationConstraints scaleLock;
    scaleLock.setInvariant(MutationInvariant::SCALE_LOCK);
    check(scaleLock.hasInvariant(MutationInvariant::SCALE_LOCK),
          "setInvariant sets the invariant bit");
    check(scaleLock.invariantsHold(base, base),
          "in-scale material satisfies the scale lock");

    PatternGenerativeState outOfScale = base;
    outOfScale.pitchSet.values[1] = 61;  // C# is not in C-Major
    check(!scaleLock.invariantsHold(base, outOfScale),
          "an out-of-scale pitch violates the scale lock");
    check(scaleLock.violatedInvariants(base, outOfScale) ==
              sequencer::maskOf(MutationInvariant::SCALE_LOCK),
          "the violation report names exactly SCALE_LOCK");

    PatternGenerativeState inScale = base;
    inScale.pitchSet.values[1] = 65;  // F is in C-Major
    check(scaleLock.invariantsHold(base, inScale),
          "an in-scale pitch change satisfies the scale lock");

    PatternGenerativeState velocityOnly = base;
    velocityOnly.velocitySet.values[0] = 90;
    check(scaleLock.invariantsHold(base, velocityOnly),
          "a non-pitch change never trips the scale lock");

    // The check reads the REQUEST's global scale, not the factory default:
    // under a G-Major request, F (in C-Major) is the out-of-scale note.
    MutationConstraints gLock;
    gLock.setInvariant(MutationInvariant::SCALE_LOCK);
    gLock.scale = common::Scale(7, 0);  // G Major: G A B C D E F#
    PatternGenerativeState gMajor = base;
    gMajor.pitchSet.values[0] = 67;  // G
    gMajor.pitchSet.values[1] = 71;  // B
    gMajor.pitchSet.values[2] = 74;  // D
    check(gLock.invariantsHold(gMajor, gMajor),
          "in-scale material satisfies the request's global scale");
    PatternGenerativeState fNatural = gMajor;
    fNatural.pitchSet.values[1] = 65;  // F natural: in C-Major, NOT in G-Major
    check(!gLock.invariantsHold(gMajor, fNatural),
          "the lock checks the request's global scale, not the factory default");

    // Locked fields: pitch contents.
    MutationConstraints lockContents;
    lockContents.setInvariant(MutationInvariant::LOCK_PITCH_CONTENTS);
    PatternGenerativeState reordered = base;  // {67, 64, 60}
    reordered.pitchSet.values[0] = 67;
    reordered.pitchSet.values[2] = 60;
    check(lockContents.invariantsHold(base, reordered),
          "reordering keeps the pitch contents (the lock is order-free)");

    PatternGenerativeState replaced = base;
    replaced.pitchSet.values[0] = 62;  // D, still in scale, but new content
    check(!lockContents.invariantsHold(base, replaced),
          "replacing a pitch violates the contents lock");

    PatternGenerativeState dropped = base;
    dropped.pitchSet.count = 2;
    check(!lockContents.invariantsHold(base, dropped),
          "dropping a pitch violates the contents lock");

    // Locked fields: pitch ordering.
    MutationConstraints lockOrder;
    lockOrder.setInvariant(MutationInvariant::LOCK_PITCH_ORDERING);
    check(!lockOrder.invariantsHold(base, reordered),
          "reordering violates the ordering lock");
    check(lockOrder.invariantsHold(base, base),
          "an untouched sequence satisfies the ordering lock");

    // Locked fields: note duration.
    MutationConstraints lockDuration;
    lockDuration.setInvariant(MutationInvariant::LOCK_NOTE_DURATION);
    PatternGenerativeState dur = base;
    dur.durationSet.values[0] = 12;
    check(!lockDuration.invariantsHold(base, dur),
          "a duration change violates the duration lock");
    check(lockDuration.invariantsHold(base, base),
          "an untouched duration set satisfies the lock");

    // Euclidean parameters and the preserved pulse count.
    PatternGenerativeState euclid = base;
    euclid.setGeneratorId(GeneratorRole::GATE, sequencer::kDefaultGateGeneratorId);
    euclid.setGeneratorParam(GeneratorRole::GATE,
                            sequencer::EuclideanGateGenerator::kParamSteps, 16);
    euclid.setGeneratorParam(GeneratorRole::GATE,
                            sequencer::EuclideanGateGenerator::kParamPulses, 4);
    euclid.setGeneratorParam(GeneratorRole::GATE,
                            sequencer::EuclideanGateGenerator::kParamRotation, 0);

    MutationConstraints preservePulses;
    preservePulses.setInvariant(MutationInvariant::PRESERVE_PULSE_COUNT);
    PatternGenerativeState rotated = euclid;
    rotated.setGeneratorParam(GeneratorRole::GATE,
                              sequencer::EuclideanGateGenerator::kParamRotation, 5);
    check(preservePulses.invariantsHold(euclid, rotated),
          "rotating preserves the pulse count");
    PatternGenerativeState denser = euclid;
    denser.setGeneratorParam(GeneratorRole::GATE,
                             sequencer::EuclideanGateGenerator::kParamPulses, 5);
    check(!preservePulses.invariantsHold(euclid, denser),
          "changing the pulse count violates PRESERVE_PULSE_COUNT");

    MutationConstraints lockEuclid;
    lockEuclid.setInvariant(MutationInvariant::LOCK_EUCLIDEAN_PARAMS);
    check(!lockEuclid.invariantsHold(euclid, rotated),
          "a rotation change violates the euclidean-params lock");
    check(lockEuclid.invariantsHold(euclid, euclid),
          "an untouched parameter set satisfies the euclidean lock");
    // A non-gate parameter change never trips the euclidean locks.
    PatternGenerativeState pitchParam = euclid;
    pitchParam.setGeneratorParam(GeneratorRole::PITCH, 0, 9);
    check(lockEuclid.invariantsHold(euclid, pitchParam),
          "a pitch-role parameter change leaves the gate-role euclidean lock satisfied");

    // Locked fields: generator assignments. (The former LOCK_SCALE bit is
    // gone — ui-integration T16: the scale is a global no mutation can
    // touch, so there is no per-pattern scale left to lock.)
    MutationConstraints lockGenerators;
    lockGenerators.setInvariant(MutationInvariant::LOCK_GENERATORS);
    PatternGenerativeState regen = base;
    regen.setGeneratorId(GeneratorRole::PITCH, "fake.pitch");
    check(!lockGenerators.invariantsHold(base, regen),
          "a generator reassignment violates the generator lock");

    // Several invariants at once: the violation mask reports each violated bit.
    MutationConstraints both;
    both.setInvariant(MutationInvariant::SCALE_LOCK);
    both.setInvariant(MutationInvariant::LOCK_PITCH_ORDERING);
    PatternGenerativeState bad = base;  // out of scale AND reordered
    bad.pitchSet.values[0] = 61;
    bad.pitchSet.values[1] = 67;
    bad.pitchSet.values[2] = 64;
    check(both.violatedInvariants(base, bad) ==
              (sequencer::maskOf(MutationInvariant::SCALE_LOCK) |
               sequencer::maskOf(MutationInvariant::LOCK_PITCH_ORDERING)),
          "the violation mask reports every violated invariant bit");
    check(!both.invariantsHold(base, bad),
          "invariantsHold is false when any configured invariant is violated");

    // An empty invariant mask never reports violations.
    MutationConstraints noInvariants;
    check(noInvariants.invariantsHold(base, bad),
          "with no invariants configured everything holds");
    check(noInvariants.violatedInvariants(base, bad) == 0,
          "with no invariants configured the violation mask is empty");

    // clearInvariant removes one bit without touching the others.
    MutationConstraints clearable;
    clearable.setInvariant(MutationInvariant::SCALE_LOCK);
    clearable.setInvariant(MutationInvariant::LOCK_GENERATORS);
    clearable.clearInvariant(MutationInvariant::SCALE_LOCK);
    check(!clearable.hasInvariant(MutationInvariant::SCALE_LOCK),
          "clearInvariant clears the requested bit");
    check(clearable.hasInvariant(MutationInvariant::LOCK_GENERATORS),
          "clearInvariant leaves the other bits set");
}

// --- (5) the operator contract: a stub reports status + changed targets ------

// A test-only mutation operator: reverses the live pitch sequence (at most
// kGenerativeSetCapacity/2 bounded swaps), honouring the framework contract
// documented in IMutationOperator.h.
class StubReorderPitches final : public IMutationOperator {
public:
    const char* id() const override { return "test.reorder-pitches"; }
    const char* name() const override { return "Reorder Pitches (test stub)"; }
    sequencer::MutationTargetMask supportedTargets() const override {
        return sequencer::maskOf(MutationTarget::PITCH_ORDERING);
    }

    MutationResult apply(PatternMutationView& view, const MutationConstraints& constraints,
                         Prng& /*rng*/) override {
        // FR8 operator control: reject requests outside the supported targets.
        if ((constraints.targets & supportedTargets()) == 0) {
            return MutationResult{MutationStatus::REJECTED, 0};
        }
        // FR6: intensity 0 asks for no change at all.
        if (constraints.intensity == 0) {
            return MutationResult{MutationStatus::NO_CHANGE, 0};
        }
        // The bounded mutation itself: reverse the live pitch sequence.
        GenerativeValueSet& set = view.state.pitchSet;
        uint8_t i = 0;
        uint8_t j = set.count;
        while (j > i + 1) {
            --j;
            const uint8_t tmp = set.values[i];
            set.values[i] = set.values[j];
            set.values[j] = tmp;
            ++i;
        }
        // FR8 constraint gate: restore the pre-mutation state and reject when
        // the result violates an invariant or leaves the distance limit.
        if (!constraints.invariantsHold(view.before, view.state) ||
            !constraints.withinDistance(view.before, view.state)) {
            view.state = view.before;
            return MutationResult{MutationStatus::REJECTED, 0};
        }
        return MutationResult{MutationStatus::APPLIED,
                              sequencer::maskOf(MutationTarget::PITCH_ORDERING)};
    }
};

void test_stub_operator() {
    StubReorderPitches op;
    check(std::strcmp(op.id(), "test.reorder-pitches") == 0,
          "the operator reports its id");
    check(std::strcmp(op.name(), "Reorder Pitches (test stub)") == 0,
          "the operator reports its name");
    check(op.supportedTargets() == sequencer::maskOf(MutationTarget::PITCH_ORDERING),
          "the operator reports the target mask it supports");

    // An APPLIED mutation reports the changed-target mask.
    PatternGenerativeState working = majorPitchState();
    const PatternGenerativeState before = working;
    PatternMutationView view(working, before, 3);
    MutationConstraints c(sequencer::maskOf(MutationTarget::PITCH_ORDERING), 64);
    Prng rng(12345u);
    MutationResult r = op.apply(view, c, rng);
    check(r.status == MutationStatus::APPLIED, "a supported request applies");
    check(r.applied(), "applied() reflects an applied status");
    check(!r.applied() || r.status == MutationStatus::APPLIED, "applied() is consistent");
    check(r.changedTargets == sequencer::maskOf(MutationTarget::PITCH_ORDERING),
          "the result reports the changed-target mask");
    check(r.changed(MutationTarget::PITCH_ORDERING), "changed() tests the mask bit");
    check(!r.changed(MutationTarget::GATE_DENSITY), "unchanged targets test false");
    check(working.pitchSet.values[0] == 67 && working.pitchSet.values[1] == 64 &&
              working.pitchSet.values[2] == 60,
          "the mutation actually reordered the working state");

    // Intensity 0 reports NO_CHANGE and touches nothing.
    working = before;
    c.setIntensity(0);
    r = op.apply(view, c, rng);
    check(r.status == MutationStatus::NO_CHANGE, "intensity 0 reports NoChange");
    check(r.changedTargets == 0, "a no-change reports an empty mask");
    check(!r.applied(), "applied() is false for a no-change");
    check(std::memcmp(&working, &before, sizeof(working)) == 0,
          "a no-change leaves the state untouched");

    // An unsupported target is REJECTED without side effects.
    c = MutationConstraints(sequencer::maskOf(MutationTarget::VELOCITY_CONTENTS), 64);
    r = op.apply(view, c, rng);
    check(r.status == MutationStatus::REJECTED, "an unsupported target is rejected");
    check(r.changedTargets == 0, "a rejection reports no changed targets");
    check(std::memcmp(&working, &before, sizeof(working)) == 0,
          "a rejected request leaves the state untouched");

    // A locked field forces REJECTED and restores the pre-mutation state.
    c = MutationConstraints(sequencer::maskOf(MutationTarget::PITCH_ORDERING), 64,
                            sequencer::maskOf(MutationInvariant::LOCK_PITCH_ORDERING));
    r = op.apply(view, c, rng);
    check(r.status == MutationStatus::REJECTED, "an ordering lock forces rejection");
    check(std::memcmp(&working, &before, sizeof(working)) == 0,
          "an invariant rejection restores the pre-mutation state");

    // The distance limit gates the same reversal: two positions differ, so
    // maxDistance 1 rejects (restoring the state) and maxDistance 2 admits.
    c = MutationConstraints(sequencer::maskOf(MutationTarget::PITCH_ORDERING), 64, 0, 1);
    r = op.apply(view, c, rng);
    check(r.status == MutationStatus::REJECTED, "maxDistance 1 rejects the reversal");
    check(std::memcmp(&working, &before, sizeof(working)) == 0,
          "a distance rejection restores the pre-mutation state");

    c = MutationConstraints(sequencer::maskOf(MutationTarget::PITCH_ORDERING), 64, 0, 2);
    r = op.apply(view, c, rng);
    check(r.status == MutationStatus::APPLIED, "maxDistance 2 admits the reversal");
    check(r.changedTargets == sequencer::maskOf(MutationTarget::PITCH_ORDERING),
          "the admitted reversal reports its changed target");

    // The PRNG is the shared deterministic type: identical seeds reproduce.
    PatternGenerativeState deterministic = majorPitchState();
    const PatternGenerativeState deterministicBefore = deterministic;
    PatternMutationView dview(deterministic, deterministicBefore, 0);
    Prng rngA(99u);
    Prng rngB(99u);
    MutationConstraints dc(sequencer::maskOf(MutationTarget::PITCH_ORDERING), 32);
    const MutationResult ra = op.apply(dview, dc, rngA);
    deterministic = deterministicBefore;
    const MutationResult rb = op.apply(dview, dc, rngB);
    check(ra.status == rb.status && ra.changedTargets == rb.changedTargets,
          "identical seeds reproduce the same result");
}

}  // namespace

int main() {
    test_target_bitmask();
    test_intensity_clamp();
    test_distance_limit();
    test_invariants();
    test_stub_operator();

    if (failures == 0) {
        std::printf("test_mutation_framework: all checks passed\n");
        return 0;
    }
    std::printf("test_mutation_framework: %d check(s) failed\n", failures);
    return 1;
}
