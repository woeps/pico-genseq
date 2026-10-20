// performance-safe-foundation task 3.1 — supported Time Signature ranges.
//
// Validates Requirements 1.3 and 1.7:
//   - a bounded, documented numerator range (1..32) and a power-of-two denominator set
//     {1,2,4,8,16,32} whose every member divides 96 (TICKS_PER_WHOLE_NOTE at PPQN 24) EXACTLY,
//     so every supported beat boundary is exactly representable;
//   - unsupported numerators/denominators are REJECTED, not rounded;
//   - ticksPerBeat / ticksPerBar derive beat and bar boundaries from the signature with zero
//     remainder for every supported signature.
//
// Standalone example (its own main(), no RapidCheck), builds host-side unconditionally and is
// excluded from the property-test glob.

#include "common/TimeSignature.h"

#include <cstdint>
#include <cstdio>

using common::TimeSignature;
namespace ts = common::time_signature;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// Every supported denominator divides TICKS_PER_WHOLE_NOTE (96) exactly — the Req 1.7
// "exactly representable at the chosen PPQN" guarantee — and is a power of two.
void test_supported_denominators_divide_exactly() {
    for (int i = 0; i < ts::SUPPORTED_DENOMINATOR_COUNT; ++i) {
        const uint8_t den = ts::SUPPORTED_DENOMINATORS[i];
        check(ts::TICKS_PER_WHOLE_NOTE % den == 0, "supported denominator divides 96 exactly");
        check((den & (den - 1)) == 0, "supported denominator is a power of two");
        check(ts::isSupportedDenominator(den), "supported denominator reports supported");
    }
    // 64 is a power of two but does NOT divide 96 -> must be rejected, not rounded.
    check(!ts::isSupportedDenominator(64), "1/64 rejected (does not divide 96)");
    check(!ts::isSupportedDenominator(3), "denominator 3 rejected (not power of two)");
    check(!ts::isSupportedDenominator(0), "denominator 0 rejected");
}

// Numerator range is exactly 1..32 inclusive.
void test_numerator_bounds() {
    check(!ts::isSupportedNumerator(0), "numerator 0 rejected");
    check(ts::isSupportedNumerator(1), "numerator 1 accepted");
    check(ts::isSupportedNumerator(32), "numerator 32 accepted");
    check(!ts::isSupportedNumerator(33), "numerator 33 rejected");
}

// Beat and bar tick counts are exact (no remainder) for every supported signature, and the
// common meters land on the expected values.
void test_beat_and_bar_ticks_exact() {
    for (int num = ts::NUMERATOR_MIN; num <= ts::NUMERATOR_MAX; ++num) {
        for (int i = 0; i < ts::SUPPORTED_DENOMINATOR_COUNT; ++i) {
            const TimeSignature sig(static_cast<uint8_t>(num), ts::SUPPORTED_DENOMINATORS[i]);
            check(ts::isSupported(sig), "constructed supported signature reports supported");
            const uint16_t perBeat = ts::ticksPerBeat(sig);
            check(perBeat != 0, "supported signature has non-zero ticks/beat");
            check(perBeat * sig.denominator == ts::TICKS_PER_WHOLE_NOTE,
                  "ticksPerBeat * denominator == 96 (exact)");
            check(ts::ticksPerBar(sig) == static_cast<uint32_t>(perBeat) * num,
                  "ticksPerBar == ticksPerBeat * numerator");
        }
    }

    // Spot-check canonical meters.
    check(ts::ticksPerBeat(TimeSignature(4, 4)) == 24, "4/4 beat = 24 ticks (one quarter)");
    check(ts::ticksPerBar(TimeSignature(4, 4)) == 96, "4/4 bar = 96 ticks");
    check(ts::ticksPerBeat(TimeSignature(6, 8)) == 12, "6/8 beat = 12 ticks (one eighth)");
    check(ts::ticksPerBar(TimeSignature(6, 8)) == 72, "6/8 bar = 72 ticks");
    check(ts::ticksPerBeat(TimeSignature(3, 4)) == 24, "3/4 beat = 24 ticks");
    check(ts::ticksPerBar(TimeSignature(3, 4)) == 72, "3/4 bar = 72 ticks");
    check(ts::ticksPerBeat(TimeSignature(5, 16)) == 6, "5/16 beat = 6 ticks (one sixteenth)");
}

// Unsupported signatures report unsupported and yield 0 ticks (never a rounded value).
void test_unsupported_rejected_not_rounded() {
    const TimeSignature bad64(4, 64);
    check(!ts::isSupported(bad64), "4/64 unsupported");
    check(ts::ticksPerBeat(bad64) == 0, "unsupported denominator -> 0 ticks/beat (not rounded)");
    check(ts::ticksPerBar(bad64) == 0, "unsupported signature -> 0 ticks/bar");

    const TimeSignature badNum(40, 4);
    check(!ts::isSupported(badNum), "40/4 unsupported (numerator out of range)");
    check(ts::ticksPerBar(badNum) == 0, "out-of-range numerator -> 0 ticks/bar");
}

// Default is 4/4.
void test_default_is_four_four() {
    const TimeSignature def;
    check(def.numerator == 4 && def.denominator == 4, "default signature is 4/4");
    check(ts::isSupported(def), "default 4/4 is supported");
}

}  // namespace

int main() {
    test_supported_denominators_divide_exactly();
    test_numerator_bounds();
    test_beat_and_bar_ticks_exact();
    test_unsupported_rejected_not_rounded();
    test_default_is_four_four();

    if (failures == 0) {
        std::printf("example_time_signature: all checks passed\n");
        return 0;
    }
    std::printf("example_time_signature: %d check(s) failed\n", failures);
    return 1;
}
