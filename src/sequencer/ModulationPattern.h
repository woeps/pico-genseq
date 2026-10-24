#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

// Global modulation pattern (generative-engine-expansion task 15, FR11).
//
// A modulation pattern is a fixed-size array of steps, each holding a value
// 0-127 — the SOURCE of the non-destructive modulation overlay. One global
// pattern can drive any number of ModulationTarget slots in the
// ModulationRegistry; every target derives its own step position from the
// AUTHORITATIVE TransportTimeline (never from a separate clock, FR11/FR13/
// FR14), so a pattern's step data alone never advances anything.
//
// Fixed capacity: MAX_STEPS statically sized; no heap, no locks, no runtime
// initialization — safe to read from the tick path.
// SDK-free: <cstddef>/<cstdint> only, no Pico SDK types.

namespace sequencer {

class ModulationPattern {
public:
    static constexpr size_t MAX_STEPS = 16;

    // Set one step's value. FR11: steps hold values 0-127, so `value` is
    // CLAMPED to that range. An out-of-range `index` is rejected (the fixed
    // capacity is enforced) and leaves the pattern unchanged.
    bool setStep(size_t index, uint8_t value) {
        if (index >= MAX_STEPS) return false;
        steps_[index] = (value > 127) ? 127 : value;
        return true;
    }

    // Active step count (1..MAX_STEPS). Traversal wraps within `length`.
    uint8_t length() const { return length_; }
    bool setLength(uint8_t length) {
        if (length == 0 || length > MAX_STEPS) return false;
        length_ = length;
        return true;
    }

    // Value at `index`, wrapping modulo the active length so callers may feed
    // any monotonic step counter (the playing-order mapping happens in
    // ModulationTarget). Total function: any index reads inside the pattern.
    uint8_t stepAt(size_t index) const {
        if (length_ == 0) return 0;
        return steps_[index % length_];
    }

    // Factory reset: every step 0, full length.
    void clear() {
        for (size_t i = 0; i < MAX_STEPS; ++i) steps_[i] = 0;
        length_ = static_cast<uint8_t>(MAX_STEPS);
    }

private:
    uint8_t steps_[MAX_STEPS] = {0};
    uint8_t length_ = static_cast<uint8_t>(MAX_STEPS);
};

// The whole pattern is plain fixed storage: safe to copy around (registry
// moves, snapshots) with no custom copy machinery.
static_assert(std::is_trivially_copyable<ModulationPattern>::value,
              "ModulationPattern must be trivially copyable");

}  // namespace sequencer
