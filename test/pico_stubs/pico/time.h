#pragma once

// Minimal host stub for Pico SDK time types used by firmware adapters compiled
// in host-side launch tests. Sequencer scheduling itself uses IMonotonicClock.
#include <cstdint>

typedef uint64_t absolute_time_t;

static inline absolute_time_t get_absolute_time() { return 0; }

static inline uint64_t to_us_since_boot(absolute_time_t t) { return t; }

static inline uint32_t to_ms_since_boot(absolute_time_t t) {
    return static_cast<uint32_t>(t / 1000u);
}

static inline int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
    return static_cast<int64_t>(to - from);
}

static inline absolute_time_t delayed_by_us(absolute_time_t t, uint64_t us) {
    return t + us;
}
