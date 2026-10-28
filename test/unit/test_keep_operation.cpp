// generative-engine-expansion task 13 — the Keep operation (FR15).
//
// Validates WorkingStateManager::keep():
//   - after mutate then keep, the working state is UNCHANGED (Keep commits
//     what the patterns currently play; it never edits it);
//   - revert after keep restores the KEPT state, not the older baseline
//     (Keep promotes the working state to the new reference point);
//   - the undo stack is EMPTY after keep (the kept state is the new anchor;
//     undo can no longer walk back through it);
//   - distance limits are measured from the KEPT state: after keep the
//     manager's baseline is the anchor a MutationConstraints::withinDistance
//     check uses (FR8's reference may be "the kept state after the Keep
//     operation", per MutationConstraints.h).
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/ so
// the property-test glob (test/*.cpp) never sweeps it; registered in the host
// test build under the ctest name "test_keep_operation".

#include "MutationConstraints.h"
#include "PatternGenerativeState.h"
#include "UndoStack.h"
#include "WorkingStateManager.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using sequencer::GeneratorRole;
using sequencer::MutationConstraints;
using sequencer::PatternGenerativeState;
using sequencer::WorkingStateManager;
using sequencer::mutationDistance;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// A deterministic, distinguishable per-pattern state (same recipe as
// test_undo_stack.cpp: id, params and material contents all derived
// from `tag`, so any restored/kept state can be recognized exactly).
PatternGenerativeState taggedState(uint8_t tag) {
    PatternGenerativeState s;
    char id[sequencer::kGeneratorIdCapacity] = {};
    std::snprintf(id, sizeof(id), "fake.%u", static_cast<unsigned>(tag));
    s.setGeneratorId(GeneratorRole::PITCH, id);
    s.setGeneratorParam(GeneratorRole::PITCH, 0, static_cast<int16_t>(tag));
    s.setGeneratorParam(GeneratorRole::PITCH, 1, static_cast<int16_t>(tag * 7));
    s.setGeneratorParam(GeneratorRole::PITCH, 2, static_cast<int16_t>(tag * 3));
    s.pitchSet.count = 4;
    for (uint8_t i = 0; i < 4; ++i) s.pitchSet.values[i] = static_cast<uint8_t>(tag + i);
    s.velocitySet.count = 2;
    s.velocitySet.values[0] = static_cast<uint8_t>(tag + 1);
    s.velocitySet.values[1] = static_cast<uint8_t>(tag + 2);
    s.durationSet.count = 1;
    s.durationSet.values[0] = static_cast<uint8_t>(tag + 3);
    s.durationSet.order = common::PlayingOrder::BACKWARDS;
    return s;
}

bool statesEqual(const PatternGenerativeState& a, const PatternGenerativeState& b) {
    return std::memcmp(&a, &b, sizeof(PatternGenerativeState)) == 0;
}

// --- (1) after mutate then keep, the working state is unchanged -------------

void test_keep_preserves_working_state() {
    const uint8_t kCount = WorkingStateManager::kPatternCount;
    PatternGenerativeState loaded[kCount];
    for (uint8_t i = 0; i < kCount; ++i) loaded[i] = taggedState(static_cast<uint8_t>(10 + i));
    WorkingStateManager mgr(loaded, kCount);

    // Mutate several patterns (with undo records, as a real mutation path
    // does), capturing the exact mutated working state.
    const uint8_t kMutated[] = {0, 3, 15};
    PatternGenerativeState kept[kCount];
    for (uint8_t m = 0; m < sizeof(kMutated) / sizeof(kMutated[0]); ++m) {
        check(mgr.recordBeforeMutation(kMutated[m]), "record before mutation");
        *mgr.working(kMutated[m]) = taggedState(static_cast<uint8_t>(70 + m));
    }
    for (uint8_t i = 0; i < kCount; ++i) kept[i] = *mgr.working(i);

    mgr.keep();

    // Keep does NOT touch the working state: every pattern (mutated or not)
    // still plays exactly what it played at the moment of Keep (FR15
    // "preserves current working state").
    for (uint8_t i = 0; i < kCount; ++i) {
        check(statesEqual(*mgr.working(i), kept[i]),
              "keep leaves the working state unchanged for every pattern");
    }

    // Keep with NO mutations is also a no-op on the working state.
    WorkingStateManager quiet(loaded, kCount);
    quiet.keep();
    for (uint8_t i = 0; i < kCount; ++i) {
        check(statesEqual(*quiet.working(i), loaded[i]),
              "keep on an unmutated state preserves the working state");
    }
}

// --- (2) revert after keep restores the kept state, not the older baseline ---

void test_revert_after_keep_restores_kept_state() {
    const uint8_t kCount = WorkingStateManager::kPatternCount;
    PatternGenerativeState loaded[kCount];
    for (uint8_t i = 0; i < kCount; ++i) loaded[i] = taggedState(static_cast<uint8_t>(20 + i));
    WorkingStateManager mgr(loaded, kCount);

    // Mutate, then keep: the mutated state becomes the new reference point.
    check(mgr.recordBeforeMutation(0), "record pattern 0");
    *mgr.working(0) = taggedState(80);
    check(mgr.recordBeforeMutation(7), "record pattern 7");
    *mgr.working(7) = taggedState(81);
    mgr.keep();
    check(statesEqual(*mgr.baseline(0), taggedState(80)),
          "keep promotes the working state into the baseline (pattern 0)");
    check(statesEqual(*mgr.baseline(7), taggedState(81)),
          "keep promotes the working state into the baseline (pattern 7)");
    check(statesEqual(*mgr.baseline(15), loaded[15]),
          "keep keeps untouched patterns' baselines as they were");

    // Post-keep mutations (with undo records), then REVERT: the restore must
    // land on the KEPT state, NOT on the older loaded baseline.
    check(mgr.recordBeforeMutation(0), "record post-keep mutation");
    *mgr.working(0) = taggedState(90);
    check(mgr.recordBeforeMutation(7), "record post-keep mutation");
    *mgr.working(7) = taggedState(91);
    check(mgr.recordBeforeMutation(12), "record post-keep mutation");
    *mgr.working(12) = taggedState(92);

    mgr.revert();

    check(statesEqual(*mgr.working(0), taggedState(80)),
          "revert after keep restores the KEPT state (pattern 0)");
    check(statesEqual(*mgr.working(7), taggedState(81)),
          "revert after keep restores the KEPT state (pattern 7)");
    check(!statesEqual(*mgr.working(0), loaded[0]) || !statesEqual(*mgr.working(7), loaded[7]),
          "the restore is NOT the older pre-keep baseline");
    check(statesEqual(*mgr.working(12), loaded[12]),
          "a pattern never touched before keep reverts to its original baseline");
    check(mgr.undoStack().empty(), "revert still clears the stack after keep");

    // Chained keeps: each Keep re-anchors at the then-current working state.
    *mgr.working(0) = taggedState(95);
    mgr.keep();
    check(mgr.recordBeforeMutation(0), "record after the second keep");
    *mgr.working(0) = taggedState(96);
    mgr.revert();
    check(statesEqual(*mgr.working(0), taggedState(95)),
          "a second keep re-anchors: revert restores the newest kept state");
}

// --- (3) the undo stack is empty after keep ----------------------------------

void test_keep_clears_undo_stack() {
    const uint8_t kCount = WorkingStateManager::kPatternCount;
    PatternGenerativeState loaded[kCount];
    for (uint8_t i = 0; i < kCount; ++i) loaded[i] = taggedState(static_cast<uint8_t>(40 + i));
    WorkingStateManager mgr(loaded, kCount, 5);

    // Fill the stack with recorded mutations.
    for (uint8_t m = 0; m < 5; ++m) {
        check(mgr.recordBeforeMutation(static_cast<uint8_t>(m * 3 % kCount)),
              "record before mutation");
        *mgr.working(static_cast<uint8_t>(m * 3 % kCount)) = taggedState(static_cast<uint8_t>(100 + m));
    }
    check(mgr.undoStack().size() == 5, "five records sit on the stack before keep");
    check(mgr.undoStack().full(), "the depth-5 stack is full before keep");

    mgr.keep();

    check(mgr.undoStack().empty(), "keep clears the undo stack (FR15)");
    check(mgr.undoStack().size() == 0, "the stack size is 0 after keep");
    check(!mgr.undo(), "undo after keep is an empty-stack no-op");

    // The pre-keep mutations are unreachable through undo: the kept state IS
    // the reference now, so a subsequent undo can only address post-keep
    // records. Verify a post-keep mutation still records normally and that
    // undoing it lands on the KEPT state (the mutated state at keep time),
    // not on the original loaded baseline.
    check(mgr.recordBeforeMutation(3), "post-keep records work again");
    *mgr.working(3) = taggedState(200);
    check(mgr.undoStack().size() == 1, "exactly the post-keep record is on the stack");
    check(mgr.undo(), "post-keep undo restores the pre-mutation (kept) state");
    check(statesEqual(*mgr.working(3), taggedState(101)),
          "post-keep undo lands on the kept state of the mutated pattern");
    check(mgr.undoStack().empty(), "the stack drains normally after keep");

    // Keep on an already-empty stack is harmless (idempotent no-op).
    WorkingStateManager fresh(loaded, kCount);
    fresh.keep();
    check(fresh.undoStack().empty(), "keep on an empty stack keeps it empty");
    fresh.keep();
    check(fresh.undoStack().empty(), "keep is idempotent on the stack");
    for (uint8_t i = 0; i < kCount; ++i) {
        check(statesEqual(*fresh.working(i), loaded[i]),
              "repeated keep never disturbs the working state");
    }
}

// --- (4) distance limits are measured from the kept state --------------------

void test_distance_limits_measure_from_kept_state() {
    const uint8_t kCount = WorkingStateManager::kPatternCount;
    PatternGenerativeState loaded[kCount];
    for (uint8_t i = 0; i < kCount; ++i) loaded[i] = taggedState(static_cast<uint8_t>(60 + i));
    WorkingStateManager mgr(loaded, kCount);

    // Anchor states: mutate pattern 0 by a known distance, then keep.
    const PatternGenerativeState keptState = taggedState(90);
    check(mgr.recordBeforeMutation(0), "record pattern 0");
    *mgr.working(0) = keptState;
    mgr.keep();

    // A candidate mutation a small distance from the KEPT state but a much
    // larger distance from the old (loaded) baseline.
    PatternGenerativeState nearKept = keptState;
    nearKept.pitchSet.values[0] =
        static_cast<uint8_t>(nearKept.pitchSet.values[0] ^ 0x01);  // distance 1
    const uint16_t fromKept = mutationDistance(keptState, nearKept);
    const uint16_t fromLoaded = mutationDistance(loaded[0], nearKept);
    check(fromKept == 1, "the candidate is one step from the kept state");
    check(fromKept < fromLoaded,
          "the candidate is strictly closer to the kept state than to the old baseline");

    // The FR8 constraint anchor after Keep is the manager's baseline — the
    // KEPT state, not the older loaded state (MutationConstraints.h: the
    // withinDistance reference may be "the kept state after the Keep
    // operation"). A limit that excludes the candidate from the OLD baseline
    // must ACCEPT it from the kept one.
    MutationConstraints tight(static_cast<uint16_t>(sequencer::MutationTarget::PITCH_CONTENTS),
                              64, 0, static_cast<uint8_t>(fromKept));
    check(tight.withinDistance(*mgr.baseline(0), nearKept),
          "distance limits measure from the KEPT state (kept state as anchor)");
    check(!tight.withinDistance(loaded[0], nearKept),
          "the same limit against the old baseline would reject: the anchor moved");

    // After revert-to-kept the same anchor holds: working == baseline == kept.
    check(mgr.recordBeforeMutation(0), "record a post-keep mutation");
    *mgr.working(0) = taggedState(130);
    mgr.revert();
    check(statesEqual(*mgr.working(0), keptState), "revert lands on the kept state");
    check(tight.withinDistance(*mgr.working(0), nearKept),
          "post-revert the distance anchor is still the kept state");
}

}  // namespace

int main() {
    test_keep_preserves_working_state();
    test_revert_after_keep_restores_kept_state();
    test_keep_clears_undo_stack();
    test_distance_limits_measure_from_kept_state();

    if (failures == 0) {
        std::printf("test_keep_operation: all checks passed\n");
        return 0;
    }
    std::printf("test_keep_operation: %d check(s) failed\n", failures);
    return 1;
}
