#pragma once
// Minimal host stub for the Pico SDK time API used by sequencer.cpp update()/play().
// Provides absolute_time_t and the three time functions the emission logic references.
// These are host-only no-ops; the property tests exercise the emission methods directly
// (via processCommand / play() / stop()), not the time-driven tick loop, so a frozen
// clock at 0 is sufficient and correct for the host build.
#include <cstdint>

typedef uint64_t absolute_time_t;

static inline absolute_time_t get_absolute_time() { return 0; }

static inline int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
    return static_cast<int64_t>(to - from);
}

static inline absolute_time_t delayed_by_us(absolute_time_t t, uint64_t us) {
    return t + us;
}
