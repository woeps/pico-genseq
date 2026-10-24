#pragma once

#include <cstdint>

#include "../common/PatternId.h"      // common::PATTERN_CAPACITY (16 slots)
#include "PatternGenerativeState.h"  // the per-pattern state being managed
#include "UndoStack.h"               // the single global undo stack (FR9)

// Working state manager (generative-engine-expansion task 12, FR9, AC5,
// AC6; Keep arrives in task 13).
//
// The engine distinguishes three states for the per-pattern generative
// configuration:
//
//   - the BASELINE: the saved/loaded pattern state, the anchor Revert
//     restores (AC6). Initialized from the loaded state; promoted by the
//     Keep operation (task 13);
//   - the WORKING state: what the patterns currently play — mutations edit
//     this, undo restores recorded snapshots into it;
//   - the UNDO STACK: one single global stack (UndoStack) of per-pattern
//     pre-mutation snapshots, so undo works across all patterns.
//
// Flow: before applying a mutation the caller calls
// recordBeforeMutation(patternIndex), which snapshots ONLY that pattern
// (index + PatternGenerativeState copy) onto the global stack. undo() pops
// the most recent snapshot and restores it into the working state (AC5);
// revert() copies the baseline into the working state for ALL patterns and
// clears the stack (AC6).
//
// This manager performs NO FRAM writes: persistence stays on the existing
// transactional FRAM save path. No automatic mutation happens here —
// recordBeforeMutation/undo/revert run only when the performer issues a
// command through the bounded CommandInbox.
//
// Global constraints honored here: fixed-capacity arrays only — no heap, no
// RTTI, no exceptions; static size bounds below. SDK-free: PatternId.h,
// PatternGenerativeState.h and UndoStack.h are all SDK-free headers, so
// this compiles unchanged in the firmware and the host test build.

namespace sequencer {

class WorkingStateManager {
public:
    // The fixed number of pattern slots tracked (matches the runtime's
    // preallocated pattern storage, common::PATTERN_CAPACITY).
    static constexpr uint8_t kPatternCount = common::PATTERN_CAPACITY;

    // `loaded`/`loadedCount` initialize the baseline from the saved/loaded
    // pattern state (may be null/0 when no saved state exists: every slot
    // then starts from a default-constructed PatternGenerativeState). The
    // working state starts equal to the baseline. `undoDepth` is the
    // runtime-configurable undo history depth (clamped to 5-16 by
    // UndoStack).
    explicit WorkingStateManager(const PatternGenerativeState* loaded = nullptr,
                                 uint8_t loadedCount = 0,
                                 uint8_t undoDepth = UndoStack::kMaxDepth)
        : undo_(undoDepth) {
        if (loaded != nullptr) {
            // Count is defensive: never read past the caller's array or the
            // manager's fixed slots.
            const uint8_t n = (loadedCount < kPatternCount) ? loadedCount : kPatternCount;
            for (uint8_t i = 0; i < n; ++i) baseline_[i] = loaded[i];
        }
        revert();  // working state starts at the baseline, stack empty
    }

    // --- working / baseline access -------------------------------------------
    //
    // Out-of-range indices yield nullptr (defensive total behavior, never
    // out-of-bounds).

    PatternGenerativeState* working(uint8_t patternIndex) {
        return (patternIndex < kPatternCount) ? &working_[patternIndex] : nullptr;
    }

    const PatternGenerativeState* working(uint8_t patternIndex) const {
        return (patternIndex < kPatternCount) ? &working_[patternIndex] : nullptr;
    }

    const PatternGenerativeState* baseline(uint8_t patternIndex) const {
        return (patternIndex < kPatternCount) ? &baseline_[patternIndex] : nullptr;
    }

    // --- undo / revert (FR9) ---------------------------------------------------

    // Snapshot `patternIndex`'s CURRENT working state onto the global undo
    // stack, to be taken BEFORE the mutation is applied. Returns false
    // (pushing nothing) for an out-of-range pattern index.
    bool recordBeforeMutation(uint8_t patternIndex) {
        if (patternIndex >= kPatternCount) return false;
        undo_.push(patternIndex, working_[patternIndex]);
        return true;
    }

    // Pop the most recent snapshot and restore it into the working state
    // (AC5). Returns false (a no-op) when the stack is empty.
    bool undo() {
        UndoEntry entry;
        if (!undo_.pop(entry)) return false;
        // The snapshot carries its own pattern index, so one global stack
        // restores the right pattern (undo works across all patterns).
        if (entry.patternIndex < kPatternCount) {
            working_[entry.patternIndex] = entry.state;
        }
        return true;
    }

    // Restore the baseline into the working state for ALL patterns and
    // clear the undo stack (AC6).
    void revert() {
        for (uint8_t i = 0; i < kPatternCount; ++i) working_[i] = baseline_[i];
        undo_.clear();
    }

    // --- undo stack configuration ----------------------------------------------

    const UndoStack& undoStack() const { return undo_; }
    UndoStack& undoStack() { return undo_; }

    // Reconfigure the undo history depth (clamped to 5-16; the stack drops
    // its oldest entries if it currently exceeds the new depth).
    void setUndoDepth(uint8_t depth) { undo_.setDepth(depth); }

private:
    // The saved/loaded anchor state Revert restores (AC6).
    PatternGenerativeState baseline_[kPatternCount];
    // What the patterns currently play; mutations edit this.
    PatternGenerativeState working_[kPatternCount];
    // The single global undo history (FR9).
    UndoStack undo_;
};

// Size bounds on the new state (global constraint): two fixed pattern
// arrays plus the fixed-capacity undo stack, all statically bounded.
static_assert(WorkingStateManager::kPatternCount == 16,
              "the manager tracks the fixed pattern capacity (16 slots)");
static_assert(sizeof(WorkingStateManager) <=
                  2 * WorkingStateManager::kPatternCount * sizeof(PatternGenerativeState) +
                      sizeof(UndoStack),
              "WorkingStateManager is exactly two pattern arrays plus the undo stack");
static_assert(sizeof(WorkingStateManager) <= 12288,
              "WorkingStateManager fits the fixed working-state RAM budget");

}  // namespace sequencer
