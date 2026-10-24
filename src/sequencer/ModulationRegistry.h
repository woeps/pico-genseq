#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "ModulationPattern.h"
#include "ModulationTarget.h"

// Statically allocated modulation registry (generative-engine-expansion
// task 15, FR11, FR13, FR14).
//
// Owns the two fixed pools of the modulation layer:
//   - MAX_PATTERNS global ModulationPatterns (FR11 "global patterns");
//   - MAX_TARGETS ModulationTarget slots, each binding a pattern to a
//     parameter with its own scale/balance/duration/order/scope config
//     (FR13/FR14).
//
// Fixed footprint: plain arrays, no heap, no locks, no runtime
// initialization — the tick path can read patterns and active targets
// freely; slots are claimed and released by operator configuration. The
// process-wide instance g_modulationRegistry is an inline variable: zero
// first-call guards, statically allocated (no dynamic memory).
// SDK-free: <cstddef>/<cstdint> only, no Pico SDK types.

namespace sequencer {

class ModulationRegistry {
public:
    static constexpr size_t MAX_PATTERNS = 8;
    static constexpr size_t MAX_TARGETS = 16;
    static constexpr size_t INVALID_INDEX = static_cast<size_t>(-1);

    // --- Global patterns (FR11) ---------------------------------------------
    // Bounds-checked pattern access: an out-of-range index yields a shared,
    // default-constructed null pattern (all steps 0) instead of undefined
    // behavior, so tick-path readers stay total. Writes through an
    // out-of-range reference land in the shared null pattern and are
    // discarded.
    ModulationPattern& pattern(size_t index) { return patternAt(patterns_, index); }
    const ModulationPattern& pattern(size_t index) const { return patternAt(patterns_, index); }
    bool patternIndexValid(size_t index) const { return index < MAX_PATTERNS; }

    // --- Target slots (FR13, FR14) --------------------------------------------
    // Claim a free slot, returning its index; the slot arrives in factory
    // configuration (per-pattern scope, scale 64, beat/1/forward). The
    // capacity is FIXED: when all MAX_TARGETS slots are claimed the next add
    // returns INVALID_INDEX — the registry never grows.
    size_t addTarget() {
        for (size_t i = 0; i < MAX_TARGETS; ++i) {
            if (!targetActive_[i]) {
                targetActive_[i] = true;
                targets_[i] = ModulationTarget{};
                ++activeTargets_;
                return i;
            }
        }
        return INVALID_INDEX;
    }

    // Free a claimed slot. Out-of-range or already-free slots are rejected.
    bool releaseTarget(size_t index) {
        if (index >= MAX_TARGETS || !targetActive_[index]) return false;
        targetActive_[index] = false;
        --activeTargets_;
        return true;
    }

    bool targetActive(size_t index) const {
        return index < MAX_TARGETS && targetActive_[index];
    }
    size_t activeTargetCount() const { return activeTargets_; }

    // Bounds-checked slot access (same safe-default rule as pattern()).
    ModulationTarget& target(size_t index) { return targetAt(targets_, index); }
    const ModulationTarget& target(size_t index) const { return targetAt(targets_, index); }

    // Release every target slot; the global patterns are preserved.
    void clearTargets() {
        for (size_t i = 0; i < MAX_TARGETS; ++i) targetActive_[i] = false;
        activeTargets_ = 0;
    }

    // Factory reset: patterns cleared, every target slot released.
    void reset() {
        for (size_t i = 0; i < MAX_PATTERNS; ++i) patterns_[i].clear();
        clearTargets();
    }

private:
    static ModulationPattern& patternAt(ModulationPattern* array, size_t index) {
        static ModulationPattern nullPattern;  // shared safe default
        return (index < MAX_PATTERNS) ? array[index] : nullPattern;
    }
    static const ModulationPattern& patternAt(const ModulationPattern* array, size_t index) {
        static ModulationPattern nullPattern;  // shared safe default
        return (index < MAX_PATTERNS) ? array[index] : nullPattern;
    }
    static ModulationTarget& targetAt(ModulationTarget* array, size_t index) {
        static ModulationTarget nullTarget;  // shared safe default
        return (index < MAX_TARGETS) ? array[index] : nullTarget;
    }
    static const ModulationTarget& targetAt(const ModulationTarget* array, size_t index) {
        static ModulationTarget nullTarget;  // shared safe default
        return (index < MAX_TARGETS) ? array[index] : nullTarget;
    }

    ModulationPattern patterns_[MAX_PATTERNS] = {};
    ModulationTarget targets_[MAX_TARGETS] = {};
    bool targetActive_[MAX_TARGETS] = {};
    size_t activeTargets_ = 0;
};

// The process-wide modulation registry: statically allocated (an inline
// variable — no heap, no first-call guard), shared across translation units.
// Tests may instead instantiate local ModulationRegistry objects.
inline ModulationRegistry g_modulationRegistry{};

// Size bounds on the new POD state: both pools are fixed and the whole
// registry stays a small static allocation (416 bytes).
static_assert(std::is_trivially_copyable<ModulationRegistry>::value,
              "ModulationRegistry must be trivially copyable");
static_assert(ModulationRegistry::MAX_PATTERNS <= 16,
              "the global pattern pool stays a small fixed pool");
static_assert(ModulationRegistry::MAX_TARGETS <= 16,
              "the target-slot pool stays a small fixed pool");
static_assert(sizeof(ModulationRegistry) <= 1024,
              "ModulationRegistry must stay a small fixed POD");

}  // namespace sequencer
