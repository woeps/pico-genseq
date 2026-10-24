#pragma once

#include <cstdint>

// Built-in scale templates (generative-engine-expansion task 1, FR3/FR4/AC2).
//
// A FIXED, READ-ONLY table of named scale templates. A template is a 12-bit
// interval mask: bit i (i in 0..11) set means the scale contains the interval
// i semitones above its root; bit 0 (the root itself) is always set. The
// concrete `Scale` value type (common::Scale, Scale.h) pairs a root pitch
// class with an index into this table and provides the constraint math
// (containsNote / nearestNoteInScale / degreeToNote / noteToDegree) that
// pitch generation and pitch mutation use to keep every emitted note in the
// pattern's selected scale.
//
// The table covers the FR3 groups: common scales (major, natural/harmonic/
// melodic minor), the seven church modes, major/minor pentatonic and blues,
// whole tone, both diminished (octatonic) sets, the exotic scales (Hungarian
// minor/major, double harmonic "Arabic", Phrygian dominant, the Japanese
// sets Hirajoshi / In-Sen / Iwato / Kumoi) and the chromatic/cluster sets.
// User-definable scales are out of scope.
//
// Constraints honored here (global): no heap, no runtime initialization — the
// table is `inline constexpr` (static storage, zero dynamic constructors), the
// names are static string literals, and the accessors are constexpr functions
// with bounded work only. This header is deliberately free of any Pico SDK
// include so it compiles unchanged in the firmware and the host test build.

namespace common {

// One read-only table entry: a static name plus the 12-bit interval mask.
struct ScaleTemplate {
    const char* name;    // static string literal, never null
    uint16_t intervals;  // bit i set <=> interval i semitones above the root is in the scale
};

namespace detail {

// Compile-time mask builder: scaleMask(0, 2, 4, ...) -> the OR of 1 << each
// interval. Keeps the table below self-documenting and impossible to typo
// into an out-of-range bit.
constexpr uint16_t scaleMask() { return 0; }
constexpr uint16_t scaleMask(int interval) {
    return static_cast<uint16_t>(1u << interval);
}
template <typename... Rest>
constexpr uint16_t scaleMask(int first, Rest... rest) {
    return static_cast<uint16_t>(scaleMask(first) | scaleMask(rest...));
}

}  // namespace detail

// The fixed template table. `inline constexpr` gives one shared read-only
// definition across translation units with zero dynamic initialization.
inline constexpr ScaleTemplate SCALE_TEMPLATES[] = {
    // Common scales.
    {"Major",           detail::scaleMask(0, 2, 4, 5, 7, 9, 11)},
    {"Natural Minor",   detail::scaleMask(0, 2, 3, 5, 7, 8, 10)},
    {"Harmonic Minor",  detail::scaleMask(0, 2, 3, 5, 7, 8, 11)},
    {"Melodic Minor",   detail::scaleMask(0, 2, 3, 5, 7, 9, 11)},
    // The seven church modes (Ionian == Major, Aeolian == Natural Minor).
    {"Ionian",          detail::scaleMask(0, 2, 4, 5, 7, 9, 11)},
    {"Dorian",          detail::scaleMask(0, 2, 3, 5, 7, 9, 10)},
    {"Phrygian",        detail::scaleMask(0, 1, 3, 5, 7, 8, 10)},
    {"Lydian",          detail::scaleMask(0, 2, 4, 6, 7, 9, 11)},
    {"Mixolydian",      detail::scaleMask(0, 2, 4, 5, 7, 9, 10)},
    {"Aeolian",         detail::scaleMask(0, 2, 3, 5, 7, 8, 10)},
    {"Locrian",         detail::scaleMask(0, 1, 3, 5, 6, 8, 10)},
    // Pentatonic and blues.
    {"Major Pentatonic", detail::scaleMask(0, 2, 4, 7, 9)},
    {"Minor Pentatonic", detail::scaleMask(0, 3, 5, 7, 10)},
    {"Blues",           detail::scaleMask(0, 3, 5, 6, 7, 10)},
    // Symmetric sets.
    {"Whole Tone",                  detail::scaleMask(0, 2, 4, 6, 8, 10)},
    {"Diminished (Whole-Half)",     detail::scaleMask(0, 2, 3, 5, 6, 8, 9, 11)},
    {"Diminished (Half-Whole)",      detail::scaleMask(0, 1, 3, 4, 6, 7, 9, 10)},
    // Exotic: Hungarian.
    {"Hungarian Minor",  detail::scaleMask(0, 2, 3, 6, 7, 8, 11)},
    {"Hungarian Major",  detail::scaleMask(0, 3, 4, 6, 7, 9, 10)},
    // Exotic: Arabic / dominant Phrygian.
    {"Double Harmonic (Arabic)", detail::scaleMask(0, 1, 4, 5, 7, 8, 11)},
    {"Phrygian Dominant",        detail::scaleMask(0, 1, 4, 5, 7, 8, 10)},
    // Exotic: Japanese.
    {"Hirajoshi",        detail::scaleMask(0, 2, 3, 7, 8)},
    {"In-Sen",           detail::scaleMask(0, 1, 5, 7, 10)},
    {"Iwato",            detail::scaleMask(0, 1, 5, 6, 10)},
    {"Kumoi",            detail::scaleMask(0, 2, 3, 7, 9)},
    // Chromatic and cluster sets.
    {"Chromatic",        detail::scaleMask(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11)},
    {"Semitone Cluster",  detail::scaleMask(0, 1, 2, 3)},
    {"Tone Cluster",      detail::scaleMask(0, 2, 4, 6)},
};

// Number of table entries (fixed at compile time).
constexpr int scaleTemplateCount() {
    return static_cast<int>(sizeof(SCALE_TEMPLATES) / sizeof(SCALE_TEMPLATES[0]));
}

// Read-only accessor. `index` is expected in [0, scaleTemplateCount()); an
// out-of-range index CLAMPS to the nearest valid entry so the accessor is
// total and can never read out of bounds (no exceptions, no OOB on bad data).
constexpr const ScaleTemplate& scaleTemplate(int index) {
    return index <= 0 ? SCALE_TEMPLATES[0]
         : (index >= scaleTemplateCount() ? SCALE_TEMPLATES[scaleTemplateCount() - 1]
                                          : SCALE_TEMPLATES[index]);
}

namespace detail {

// Compile-time table sanity (mirrors the host test): within 12 bits, bit 0
// (the root) set, non-empty name, and the FR3 count window 20-30.
constexpr bool scaleTemplatesWellFormed() {
    for (const ScaleTemplate& t : SCALE_TEMPLATES) {
        if ((t.intervals & ~uint16_t(0x0FFF)) != 0) return false;
        if ((t.intervals & 1u) == 0) return false;
        if (t.name == nullptr || t.name[0] == '\0') return false;
    }
    return scaleTemplateCount() >= 20 && scaleTemplateCount() <= 30;
}

}  // namespace detail

static_assert(detail::scaleTemplatesWellFormed(),
              "every scale template: non-empty name, 12-bit mask with bit 0 "
              "(root) set; 20-30 templates total");

}  // namespace common
