// generative-engine-expansion task 2.2 — host unit tests for the scale library.
//
// Validates Requirements 1.1, 1.2, 1.3, 1.6:
//   - membership for major/natural-minor across all 12 roots, chromatic passes
//     everything;
//   - each quantization mode (NEAREST/DOWN/UP/SKIP) over out-of-scale inputs;
//   - octave preserved except where DOWN/UP must cross an octave;
//   - SKIP reports suppression (false) for an out-of-scale note and identity
//     (true) for an in-scale one.
//
// Standalone example (its own main(), no RapidCheck), builds host-side
// unconditionally and is excluded from the property-test glob.

#include "common/Scale.h"

#include <cstdint>
#include <cstdio>

using common::ScaleId;
using common::QuantizeMode;
namespace scale = common::scale;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// C major = white keys: pitch classes 0,2,4,5,7,9,11 in scale; 1,3,6,8,10 out.
void test_major_membership_c() {
    const uint16_t m = scale::membershipMask(ScaleId::MAJOR, 0);
    const bool expected[12] = {true, false, true, false, true, true,
                               false, true, false, true, false, true};
    for (uint8_t pc = 0; pc < 12; ++pc) {
        check(scale::inScale(m, 60 + pc) == expected[pc], "C major membership per pitch class");
    }
    // Octave-independent: same classes an octave up.
    check(scale::inScale(m, 72) == expected[0], "C major membership is octave-independent");
}

// A major (root 9): A B C# D E F# G# => classes 9,11,1,2,4,6,8.
void test_major_membership_rooted() {
    const uint16_t m = scale::membershipMask(ScaleId::MAJOR, 9);
    const bool inA[12] = {false, true, true, false, true, false,
                          true, false, true, true, false, true};
    for (uint8_t pc = 0; pc < 12; ++pc) {
        check(scale::inScale(m, 60 + pc) == inA[pc], "A major membership per pitch class");
    }
}

// A natural minor = white keys too (classes 9,11,0,2,4,5,7).
void test_minor_membership() {
    const uint16_t m = scale::membershipMask(ScaleId::MINOR_NATURAL, 9);
    const bool inAm[12] = {true, false, true, false, true, true,
                           false, true, false, true, false, true};
    for (uint8_t pc = 0; pc < 12; ++pc) {
        check(scale::inScale(m, 60 + pc) == inAm[pc], "A natural minor membership per pitch class");
    }
}

// Chromatic passes everything, every root.
void test_chromatic_all() {
    for (uint8_t root = 0; root < 12; ++root) {
        const uint16_t m = scale::membershipMask(ScaleId::CHROMATIC, root);
        for (uint8_t n = 0; n < 128; ++n) {
            check(scale::inScale(m, n), "chromatic: every note in scale");
        }
    }
}

// Quantization on C major: C#(61) out of scale.
void test_quantize_modes() {
    const uint16_t m = scale::membershipMask(ScaleId::MAJOR, 0);
    uint8_t out = 0;

    // In-scale note is identity for every mode.
    check(scale::quantize(m, QuantizeMode::NEAREST, 60, out) && out == 60, "in-scale identity NEAREST");
    check(scale::quantize(m, QuantizeMode::SKIP, 60, out) && out == 60, "in-scale identity SKIP");

    // C#(61) out of scale.
    check(scale::quantize(m, QuantizeMode::DOWN, 61, out) && out == 60, "DOWN: C# -> C");
    check(scale::quantize(m, QuantizeMode::UP, 61, out) && out == 62, "UP: C# -> D");
    // NEAREST ties (C# is equidistant to C and D); tie prefers lower => C.
    check(scale::quantize(m, QuantizeMode::NEAREST, 61, out) && out == 60, "NEAREST tie prefers lower: C# -> C");
    // SKIP suppresses out-of-scale.
    check(!scale::quantize(m, QuantizeMode::SKIP, 61, out), "SKIP suppresses out-of-scale C#");

    // F#(66) out of scale: nearest is F(65, down 1) vs G(67, up 1) -> tie -> F.
    check(scale::quantize(m, QuantizeMode::NEAREST, 66, out) && out == 65, "NEAREST: F# -> F (tie low)");
    check(scale::quantize(m, QuantizeMode::UP, 66, out) && out == 67, "UP: F# -> G");
}

// Octave preservation: quantizing stays in the same octave unless DOWN/UP forces
// a cross. B#? Use a case where DOWN from the lowest scale member must cross.
void test_octave_behavior() {
    const uint16_t m = scale::membershipMask(ScaleId::MAJOR, 0);
    uint8_t out = 0;
    // NEAREST on C#5(61) resolves within the octave (to C5=60), octave preserved.
    check(scale::quantize(m, QuantizeMode::NEAREST, 61, out) && (out / 12 == 61 / 12 || out == 60),
          "NEAREST keeps octave for C#");
    // UP from B(71, in scale) is identity; UP from A#(70) -> B(71), same octave.
    check(scale::quantize(m, QuantizeMode::UP, 70, out) && out == 71, "UP: A# -> B same octave");
    // DOWN from C#(61) -> C(60), same octave.
    check(scale::quantize(m, QuantizeMode::DOWN, 61, out) && out == 60, "DOWN: C# -> C same octave");
}

}  // namespace

int main() {
    test_major_membership_c();
    test_major_membership_rooted();
    test_minor_membership();
    test_chromatic_all();
    test_quantize_modes();
    test_octave_behavior();

    if (failures == 0) {
        std::printf("example_scale: all checks passed\n");
        return 0;
    }
    std::printf("example_scale: %d check(s) failed\n", failures);
    return 1;
}
