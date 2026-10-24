// generative-engine-expansion task 12 — global undo stack + working state
// manager (FR9, AC5, AC6).
//
// Validates:
//   - UndoStack: the runtime-configurable depth CLAMPS to 5-16 (compile-time
//     maximum 16), pushing past capacity DROPS THE OLDEST entry (ring
//     overwrite, FIFO pop order), pop on an empty stack is a no-op returning
//     false, and the fixed memory bound holds;
//   - WorkingStateManager: recordBeforeMutation() snapshots only the affected
//     pattern (index + PatternGenerativeState copy) onto the GLOBAL stack, so
//     N undos after N mutations across DIFFERENT patterns restore the exact
//     prior state of each pattern (AC5); revert() restores the baseline for
//     ALL patterns and leaves the stack empty (AC6); out-of-range pattern
//     indices are rejected without side effects.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/ so
// the property-test glob (test/*.cpp) never sweeps it; registered in the host
// test build under the ctest name "test_undo_stack".

#include "PatternGenerativeState.h"
#include "UndoStack.h"
#include "WorkingStateManager.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using sequencer::GeneratorRole;
using sequencer::PatternGenerativeState;
using sequencer::UndoEntry;
using sequencer::UndoStack;
using sequencer::WorkingStateManager;

// Task-12 required test item (6): static size asserts hold. Mirrors the
// compile-time bounds of the new headers in this test translation unit, so
// the test target fails to build if the new POD state ever grows past its
// fixed budget.
static_assert(UndoStack::kMaxDepth == 16,
              "FR9: the compile-time undo maximum is exactly 16 entries");
static_assert(UndoStack::kMinDepth == 5,
              "FR9: the undo depth never clamps below 5 entries");
static_assert(WorkingStateManager::kPatternCount == 16,
              "the manager tracks the fixed pattern capacity (16 slots)");
static_assert(sizeof(UndoEntry) <= 272,
              "UndoEntry stays a small fixed POD (index + one state copy)");
static_assert(sizeof(UndoStack) <= 4096,
              "UndoStack fits its fixed RAM budget");
static_assert(sizeof(WorkingStateManager) <= 12288,
              "WorkingStateManager fits its fixed RAM budget");

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// A deterministic, distinguishable per-pattern state: generator id, params,
// scale and material contents all derived from `tag` so a restored state can
// be recognized exactly.
PatternGenerativeState taggedState(uint8_t tag) {
    PatternGenerativeState s;
    char id[sequencer::kGeneratorIdCapacity] = {};
    std::snprintf(id, sizeof(id), "fake.%u", static_cast<unsigned>(tag));
    s.setGeneratorId(GeneratorRole::PITCH, id);
    s.setGeneratorParam(GeneratorRole::PITCH, 0, static_cast<int16_t>(tag));
    s.setGeneratorParam(GeneratorRole::PITCH, 1, static_cast<int16_t>(tag * 7));
    s.scaleRoot = static_cast<uint8_t>(tag % 12);
    s.scaleTemplateIndex = static_cast<uint8_t>(tag % 5);
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

UndoEntry entryFor(uint8_t tag, uint8_t patternIndex) {
    UndoEntry e;
    e.patternIndex = patternIndex;
    e.state = taggedState(tag);
    return e;
}

// --- (1) depth clamps to 5-16 -----------------------------------------------

void test_depth_clamps() {
    UndoStack s;
    check(s.depth() == 16, "default depth is the compile-time maximum 16");

    s.setDepth(2);
    check(s.depth() == 5, "depth clamps UP to the floor 5");
    s.setDepth(0);
    check(s.depth() == 5, "depth 0 clamps to the floor 5");
    s.setDepth(200);
    check(s.depth() == 16, "depth clamps DOWN to the maximum 16");
    s.setDepth(10);
    check(s.depth() == 10, "an in-range depth is kept verbatim");

    UndoStack small(1);
    check(small.depth() == 5, "constructor depth clamps to the floor 5");
    UndoStack huge(255);
    check(huge.depth() == 16, "constructor depth clamps to the maximum 16");
    UndoStack mid(12);
    check(mid.depth() == 12, "constructor keeps an in-range depth");
}

// --- (2) pushing past capacity drops the oldest entry ------------------------

void test_ring_drops_oldest() {
    UndoStack s(5);  // minimum depth
    for (uint8_t tag = 1; tag <= 7; ++tag) s.push(entryFor(tag, static_cast<uint8_t>(tag % 3)));
    check(s.size() == 5, "a full ring keeps exactly depth entries");

    UndoEntry e{};
    // Tags 1 and 2 were dropped; 3..7 pop in last-in-first-out order
    // (undo reverses the newest mutation first) with their states.
    for (uint8_t tag = 7; tag >= 3; --tag) {
        check(s.pop(e), "entry pops while the stack is non-empty");
        check(e.state.generatorParam(GeneratorRole::PITCH, 0) == static_cast<int16_t>(tag),
              "dropped oldest entries do not survive; survivors pop newest-first");
    }
    check(!s.pop(e), "the drained stack reports empty");

    // Wrap-around behavior: fill, drain partially, push past capacity again.
    UndoStack w(5);
    for (uint8_t tag = 10; tag < 15; ++tag) w.push(entryFor(tag, 0));
    w.clear();
    for (uint8_t tag = 20; tag < 20 + 9; ++tag) w.push(entryFor(tag, 1));
    check(w.size() == 5, "clear + 9 pushes on depth 5 keeps 5 entries");
    for (uint8_t tag = 28; tag >= 24; --tag) {
        check(w.pop(e), "wrapped ring still pops");
        check(e.state.generatorParam(GeneratorRole::PITCH, 0) == static_cast<int16_t>(tag),
              "wrapped ring drops the oldest four and pops newest-first");
        check(e.patternIndex == 1, "entry keeps its pattern index across the wrap");
    }
}

// --- (4) undo on an empty stack is a no-op returning false -------------------

void test_empty_stack_noop() {
    UndoStack s(8);
    UndoEntry e = entryFor(42, 3);
    check(s.empty(), "a fresh stack is empty");
    check(!s.pop(e), "pop on an empty stack returns false");
    check(e.state.generatorParam(GeneratorRole::PITCH, 0) == 42,
          "pop on an empty stack leaves the out-entry untouched");

    WorkingStateManager mgr;
    check(!mgr.undo(), "undo on an empty manager stack returns false (no-op)");
    check(mgr.undoStack().size() == 0, "the failed undo pushed nothing");
}

// --- (3) N undos after N mutations across different patterns (AC5) -----------

void test_undo_restores_each_pattern() {
    const uint8_t kCount = WorkingStateManager::kPatternCount;
    PatternGenerativeState loaded[kCount];
    for (uint8_t i = 0; i < kCount; ++i) loaded[i] = taggedState(static_cast<uint8_t>(100 + i));
    WorkingStateManager mgr(loaded, kCount);

    // The working state starts as the loaded (baseline) state.
    for (uint8_t i = 0; i < kCount; ++i) {
        check(statesEqual(*mgr.working(i), loaded[i]), "working state starts at the baseline");
    }

    // N mutations across different patterns: record, then mutate.
    const uint8_t kMutated[] = {0, 7, 15, 3, 7, 0, 11, 15};
    const uint8_t kN = sizeof(kMutated) / sizeof(kMutated[0]);
    PatternGenerativeState preMutation[kN];
    for (uint8_t m = 0; m < kN; ++m) {
        uint8_t p = kMutated[m];
        check(mgr.recordBeforeMutation(p), "recordBeforeMutation accepts an in-range pattern");
        preMutation[m] = *mgr.working(p);
        *mgr.working(p) = taggedState(static_cast<uint8_t>(200 + m));
    }
    check(mgr.undoStack().size() == kN, "one snapshot per recorded mutation");

    // N undos restore the exact prior state of each pattern (AC5), in
    // last-in-first-out order.
    for (uint8_t m = kN; m-- > 0;) {
        check(mgr.undo(), "undo pops while mutations remain");
        uint8_t p = kMutated[m];
        check(statesEqual(*mgr.working(p), preMutation[m]),
              "undo restores the exact pre-mutation state of the recorded pattern");
    }
    check(!mgr.undo(), "the stack is drained after N undos");

    // Every pattern is back at its exact loaded state.
    for (uint8_t i = 0; i < kCount; ++i) {
        check(statesEqual(*mgr.working(i), loaded[i]),
              "after N undos all patterns match their pre-mutation state (AC5)");
    }
}

void test_undo_only_touches_recorded_pattern() {
    const uint8_t kCount = WorkingStateManager::kPatternCount;
    PatternGenerativeState loaded[kCount];
    for (uint8_t i = 0; i < kCount; ++i) loaded[i] = taggedState(static_cast<uint8_t>(50 + i));
    WorkingStateManager mgr(loaded, kCount);

    // Mutate pattern 2 only.
    check(mgr.recordBeforeMutation(2), "record pattern 2");
    *mgr.working(2) = taggedState(201);
    // A different pattern mutated WITHOUT recording is not undone.
    *mgr.working(9) = taggedState(202);

    check(mgr.undo(), "undo the recorded mutation");
    check(statesEqual(*mgr.working(2), loaded[2]),
          "the recorded pattern is restored");
    check(statesEqual(*mgr.working(9), taggedState(202)),
          "an unrecorded pattern keeps its current state: undo touches only the snapshot's pattern");
}

void test_record_rejects_out_of_range() {
    const uint8_t kCount = WorkingStateManager::kPatternCount;
    WorkingStateManager mgr;
    check(!mgr.recordBeforeMutation(kCount), "pattern index == capacity is rejected");
    check(!mgr.recordBeforeMutation(255), "a far out-of-range pattern index is rejected");
    check(mgr.undoStack().size() == 0, "a rejected record pushes nothing");
    check(mgr.working(kCount) == nullptr, "out-of-range working() access is null");
    check(mgr.baseline(kCount) == nullptr, "out-of-range baseline() access is null");
    check(mgr.working(0) != nullptr && mgr.baseline(0) != nullptr,
          "in-range accessors resolve");
}

// --- (5) revert restores the baseline for all patterns (AC6) -----------------

void test_revert_restores_baseline_and_clears() {
    const uint8_t kCount = WorkingStateManager::kPatternCount;
    PatternGenerativeState loaded[kCount];
    for (uint8_t i = 0; i < kCount; ++i) loaded[i] = taggedState(static_cast<uint8_t>(30 + i));
    WorkingStateManager mgr(loaded, kCount, 5);

    // Mutate several patterns WITH undo records.
    const uint8_t kMutated[] = {1, 5, 12, 15, 1};
    for (uint8_t m = 0; m < sizeof(kMutated) / sizeof(kMutated[0]); ++m) {
        check(mgr.recordBeforeMutation(kMutated[m]), "record before mutation");
        *mgr.working(kMutated[m]) = taggedState(static_cast<uint8_t>(90 + m));
    }
    check(mgr.undoStack().size() == 5, "five records on a depth-5 stack");
    check(mgr.undoStack().full(), "the stack is at its configured depth");

    mgr.revert();

    for (uint8_t i = 0; i < kCount; ++i) {
        check(statesEqual(*mgr.working(i), loaded[i]),
              "revert restores the saved baseline for ALL patterns (AC6)");
        check(statesEqual(mgr.baseline(i) ? *mgr.baseline(i) : taggedState(0), loaded[i]),
              "revert leaves the baseline itself untouched");
    }
    check(mgr.undoStack().empty(), "revert clears the undo stack (AC6)");
    check(!mgr.undo(), "after revert, undo is an empty-stack no-op");
}

// --- constructor / baseline initialization -----------------------------------

void test_baseline_initialization() {
    const uint8_t kCount = WorkingStateManager::kPatternCount;
    // A partially-filled load: the first 4 slots come from storage.
    PatternGenerativeState loaded[4];
    for (uint8_t i = 0; i < 4; ++i) loaded[i] = taggedState(static_cast<uint8_t>(60 + i));
    WorkingStateManager mgr(loaded, 4);

    PatternGenerativeState defaults;
    for (uint8_t i = 0; i < 4; ++i) {
        check(statesEqual(*mgr.baseline(i), loaded[i]),
              "the baseline is initialized from the loaded pattern state");
        check(statesEqual(*mgr.working(i), loaded[i]),
              "the working state starts equal to the baseline");
    }
    for (uint8_t i = 4; i < kCount; ++i) {
        check(statesEqual(*mgr.baseline(i), defaults),
              "slots beyond the load default; no out-of-bounds read");
    }

    // No loaded state at all: every slot defaults.
    WorkingStateManager fresh;
    check(statesEqual(*fresh.baseline(0), defaults),
          "a default-constructed manager starts from default states");
    check(statesEqual(*fresh.working(kCount - 1), defaults),
          "default working state reaches the last slot");
}

// --- captureBaseline(): re-anchor at the currently saved state ---------------

void test_capture_baseline_reanchor() {
    const uint8_t kCount = WorkingStateManager::kPatternCount;
    PatternGenerativeState loaded[kCount];
    for (uint8_t i = 0; i < kCount; ++i) loaded[i] = taggedState(static_cast<uint8_t>(150 + i));
    WorkingStateManager mgr(loaded, kCount);

    // Capturing immediately after load is a no-op: the baseline stays the
    // state the manager was constructed from (the loaded/saved state).
    mgr.captureBaseline();
    for (uint8_t i = 0; i < kCount; ++i) {
        check(statesEqual(*mgr.baseline(i), loaded[i]),
              "captureBaseline right after load keeps the loaded baseline");
    }

    // Mutate, then SAVE: the FRAM path calls captureBaseline() on the
    // committed state, so the baseline is re-anchored at the save point.
    check(mgr.recordBeforeMutation(0), "record pattern 0 before the save");
    *mgr.working(0) = taggedState(210);
    check(mgr.recordBeforeMutation(3), "record pattern 3 before the save");
    *mgr.working(3) = taggedState(211);
    mgr.captureBaseline();  // a successful transactional save
    check(statesEqual(*mgr.baseline(0), taggedState(210)),
          "the baseline tracks the state present at capture time (pattern 0)");
    check(statesEqual(*mgr.baseline(3), taggedState(211)),
          "the baseline tracks the state present at capture time (pattern 3)");
    check(statesEqual(*mgr.baseline(7), loaded[7]),
          "patterns untouched by the pre-save mutations keep their baseline");
    check(mgr.undoStack().size() == 2,
          "captureBaseline leaves the undo history untouched (revert clears)");

    // Post-save mutations, then REVERT: back to the SAVED baseline, stack empty.
    check(mgr.recordBeforeMutation(0), "record post-save mutation");
    *mgr.working(0) = taggedState(220);
    check(mgr.recordBeforeMutation(3), "record post-save mutation");
    *mgr.working(3) = taggedState(221);
    mgr.revert();
    check(statesEqual(*mgr.working(0), taggedState(210)),
          "revert restores the re-anchored saved baseline (AC6)");
    check(statesEqual(*mgr.working(3), taggedState(211)),
          "revert restores the re-anchored saved baseline (AC6)");
    check(statesEqual(*mgr.working(7), loaded[7]),
          "patterns untouched by mutations still match the saved baseline");
    check(mgr.undoStack().empty(), "revert still clears the stack (AC6)");
}

void test_runtime_depth_configuration() {
    const uint8_t kCount = WorkingStateManager::kPatternCount;
    WorkingStateManager mgr(nullptr, 0, 6);
    check(mgr.undoStack().depth() == 6, "the manager configures the stack depth");

    for (uint8_t m = 0; m < 10; ++m) {
        check(mgr.recordBeforeMutation(static_cast<uint8_t>(m % kCount)),
              "record mutation");
        *mgr.working(static_cast<uint8_t>(m % kCount)) = taggedState(static_cast<uint8_t>(m));
    }
    check(mgr.undoStack().size() == 6, "the stack honors its configured depth of 6");
    check(mgr.undoStack().full(), "depth-6 stack is full after 10 records");

    // Runtime reconfiguration clamps the same way as the stack.
    mgr.setUndoDepth(2);
    check(mgr.undoStack().depth() == 5, "the manager clamps depth to the floor 5");
    check(mgr.undoStack().size() == 5, "shrinking the depth drops the oldest entries");
}

}  // namespace

int main() {
    test_depth_clamps();
    test_ring_drops_oldest();
    test_empty_stack_noop();
    test_undo_restores_each_pattern();
    test_undo_only_touches_recorded_pattern();
    test_record_rejects_out_of_range();
    test_revert_restores_baseline_and_clears();
    test_baseline_initialization();
    test_capture_baseline_reanchor();
    test_runtime_depth_configuration();

    if (failures == 0) {
        std::printf("test_undo_stack: all checks passed\n");
        return 0;
    }
    std::printf("test_undo_stack: %d check(s) failed\n", failures);
    return 1;
}
