#pragma once

#include <cstdint>
#include <type_traits>

#include "PatternGenerativeState.h"  // the state each undo snapshot carries

// Global undo stack (generative-engine-expansion task 12, FR9, AC5).
//
// One SINGLE global stack backs Undo/Revert for the whole engine — there is
// deliberately NO per-pattern undo history (out of scope). Each entry
// snapshots exactly the patterns affected by one mutation: a pattern index
// plus a byte-identical PatternGenerativeState copy, so undo works across
// all patterns from one shared stack. Modulated effective values never
// enter here — the snapshot is of the pattern's own configured state only.
//
// UndoStack is a fixed-capacity RING BUFFER:
//   - the compile-time maximum depth is kUndoMaxDepth (16);
//   - the runtime-configurable depth clamps to [kUndoMinDepth (5), 16]
//     (FR9: "limited undo history (5-16 mutations)");
//   - pushing past the configured capacity DROPS THE OLDEST entry (the
//     performer loses the least recent history, never the newest);
//   - pop() is last-in-first-out (the most recently pushed snapshot undoes
//     first) and an EMPTY stack is a no-op returning false.
//
// Global constraints honored here: a plain fixed-capacity value type — no
// heap, no RTTI, no exceptions; static size bounds below. SDK-free:
// PatternGenerativeState.h plus standard headers only, so it compiles
// unchanged in the firmware and the host test build. The stack itself never
// touches FRAM — persistence stays on the existing transactional save path.

namespace sequencer {

// FR9: the undo history depth bounds. The runtime-configurable depth clamps
// into [kUndoMinDepth, kUndoMaxDepth]; kUndoMaxDepth is also the fixed entry
// capacity of the ring.
constexpr uint8_t kUndoMinDepth = 5;
constexpr uint8_t kUndoMaxDepth = 16;

// One undo snapshot: the pattern slot index plus the pattern's generative
// state as it was BEFORE one mutation. A trivial POD so snapshots compare
// byte-identically.
struct UndoEntry {
    uint8_t patternIndex = 0;
    PatternGenerativeState state{};
};

class UndoStack {
public:
    static constexpr uint8_t kMinDepth = kUndoMinDepth;
    static constexpr uint8_t kMaxDepth = kUndoMaxDepth;

    // `depth` is the runtime-configurable history depth; it CLAMPS to
    // [kMinDepth, kMaxDepth]. Default: the compile-time maximum.
    explicit UndoStack(uint8_t depth = kMaxDepth) { setDepth(depth); }

    // Reconfigure the history depth (clamped to 5-16). If the stack
    // currently holds more entries than the new depth, the OLDEST entries
    // are dropped so size() never exceeds depth().
    void setDepth(uint8_t depth) {
        depth_ = clampDepth(depth);
        while (count_ > depth_) dropOldest();
    }

    uint8_t depth() const { return depth_; }

    uint8_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    bool full() const { return count_ == depth_; }

    // Snapshot one pattern's pre-mutation state. When the stack is at its
    // configured depth the OLDEST entry is dropped (ring overwrite): the
    // newest history is always kept.
    void push(const UndoEntry& entry) {
        if (count_ == depth_) dropOldest();
        entries_[slotOf(count_)] = entry;
        ++count_;
    }

    // Convenience form: push (patternIndex, state).
    void push(uint8_t patternIndex, const PatternGenerativeState& state) {
        push(UndoEntry{patternIndex, state});
    }

    // Pop the MOST RECENT snapshot into `out` (last-in-first-out: undo
    // reverses the latest mutation first). An EMPTY stack is a no-op that
    // returns false and leaves `out` untouched.
    bool pop(UndoEntry& out) {
        if (count_ == 0) return false;
        out = entries_[slotOf(static_cast<uint8_t>(count_ - 1u))];
        --count_;
        return true;
    }

    // Drop all history (Revert clears the stack; Keep needs no history).
    void clear() {
        head_ = 0;
        count_ = 0;
    }

private:
    static constexpr uint8_t clampDepth(uint8_t depth) {
        return (depth < kMinDepth) ? kMinDepth
                                   : ((depth > kMaxDepth) ? kMaxDepth : depth);
    }

    // Physical slot of logical position `pos` (0 == oldest) in the ring.
    uint8_t slotOf(uint8_t pos) const {
        return static_cast<uint8_t>((head_ + pos) % kMaxDepth);
    }

    // Forget the oldest entry (advances the ring head).
    void dropOldest() {
        head_ = slotOf(1);
        --count_;
    }

    UndoEntry entries_[kMaxDepth];
    uint8_t depth_ = kMaxDepth;
    uint8_t head_ = 0;   // physical slot of the oldest entry
    uint8_t count_ = 0;  // live entries: 0..depth_
};

// Size bounds on the new POD state (global constraint): snapshot data is
// trivially copyable, and the whole stack fits a fixed RAM budget.
static_assert(kUndoMaxDepth == 16, "FR9: the compile-time undo maximum is 16");
static_assert(kUndoMinDepth == 5, "FR9: the undo depth never clamps below 5");
static_assert(std::is_trivially_copyable<UndoEntry>::value,
              "UndoEntry must be trivially copyable (byte-identical snapshots)");
static_assert(std::is_standard_layout<UndoEntry>::value,
              "UndoEntry must be standard layout");
static_assert(sizeof(UndoEntry) <= 272,
              "UndoEntry stays a small fixed POD (index + one state copy)");
static_assert(sizeof(UndoStack) <= kUndoMaxDepth * sizeof(UndoEntry) + 8,
              "UndoStack carries only the fixed entry ring plus control bytes");
static_assert(sizeof(UndoStack) <= 4096,
              "UndoStack fits the fixed undo RAM budget");

}  // namespace sequencer
