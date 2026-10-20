#pragma once

#include <cstdint>

// Supported Time Signature ranges (performance-safe-foundation task 3.1,
// Requirements 1.3 and 1.7).
//
// GenSeq runs at PPQN = 24 (24 logical ticks per quarter note). A beat is one
// `1/denominator` note. A whole note is 4 quarter notes = 96 ticks, so a beat
// spans `96 / denominator` ticks. For every supported beat boundary to be
// EXACTLY representable at this PPQN (Req 1.7), `denominator` must divide 96
// evenly. The design further constrains the denominator to the power-of-two set
// (the conventional musical note values), giving:
//
//     96 = 2^5 * 3  ->  power-of-two divisors of 96: 1, 2, 4, 8, 16, 32
//
// (64 is a power of two but does NOT divide 96, so a 1/64 beat is not exactly
// representable at PPQN 24 and is rejected rather than rounded.)
//
// Numerator (beats per bar) is bounded to 1..32, a musically generous range
// that keeps a bar an exact whole number of ticks for every supported
// denominator. Values outside these sets are REJECTED, never rounded.
//
// This header is deliberately free of any Pico SDK include so it compiles
// unchanged in the firmware and the host test build.

namespace common {

struct TimeSignature {
    uint8_t numerator;    // beats per bar, 1..32
    uint8_t denominator;  // beat note value, one of {1,2,4,8,16,32}

    constexpr TimeSignature() : numerator(4), denominator(4) {}  // 4/4 default
    constexpr TimeSignature(uint8_t num, uint8_t den) : numerator(num), denominator(den) {}

    constexpr bool operator==(const TimeSignature& o) const {
        return numerator == o.numerator && denominator == o.denominator;
    }
    constexpr bool operator!=(const TimeSignature& o) const { return !(*this == o); }
};

namespace time_signature {

constexpr uint8_t PPQN_VALUE = 24;              // logical ticks per quarter note
constexpr uint16_t TICKS_PER_WHOLE_NOTE = 96;   // 4 quarter notes * 24 PPQN

constexpr uint8_t NUMERATOR_MIN = 1;
constexpr uint8_t NUMERATOR_MAX = 32;

// The supported power-of-two denominators that divide 96 exactly.
constexpr uint8_t SUPPORTED_DENOMINATORS[] = {1, 2, 4, 8, 16, 32};
constexpr int SUPPORTED_DENOMINATOR_COUNT =
    static_cast<int>(sizeof(SUPPORTED_DENOMINATORS) / sizeof(SUPPORTED_DENOMINATORS[0]));

constexpr bool isSupportedNumerator(int numerator) {
    return numerator >= NUMERATOR_MIN && numerator <= NUMERATOR_MAX;
}

constexpr bool isSupportedDenominator(int denominator) {
    for (int i = 0; i < SUPPORTED_DENOMINATOR_COUNT; ++i) {
        if (SUPPORTED_DENOMINATORS[i] == denominator) return true;
    }
    return false;
}

constexpr bool isSupported(TimeSignature ts) {
    return isSupportedNumerator(ts.numerator) && isSupportedDenominator(ts.denominator);
}

// Logical ticks spanned by one beat (one `1/denominator` note). Only meaningful
// for a supported denominator; returns 0 for an unsupported one so a caller that
// skips validation still gets an obviously-wrong (non-dividing) value rather than
// a rounded one.
constexpr uint16_t ticksPerBeat(TimeSignature ts) {
    if (!isSupportedDenominator(ts.denominator)) return 0;
    return static_cast<uint16_t>(TICKS_PER_WHOLE_NOTE / ts.denominator);
}

// Logical ticks spanned by one bar (numerator beats). 0 for an unsupported sig.
constexpr uint32_t ticksPerBar(TimeSignature ts) {
    if (!isSupported(ts)) return 0;
    return static_cast<uint32_t>(ticksPerBeat(ts)) * ts.numerator;
}

}  // namespace time_signature

}  // namespace common
