#pragma once

#include <cstdint>

#include "ScaleTemplates.h"

// Scale value type (generative-engine-expansion task 1, FR3/FR4/AC2).
//
// A Scale pairs a root pitch class (0-11) with an index into the fixed
// built-in template table (ScaleTemplates.h) and provides the constraint math
// that pitch generation and pitch mutation use so every emitted note satisfies
// the pattern's selected scale:
//
//   containsNote(note)       — is the MIDI note (0-127) in the scale?
//   nearestNoteInScale(note) — nearest in-scale MIDI note in 0-127, distance
//                              ties resolved deterministically to the LOWER
//                              note
//   degreeToNote(d, baseOctave) — MIDI note of scale degree d; degree 0 is
//                              the root in MIDI octave baseOctave (note
//                              baseOctave*12 + root), degrees outside
//                              [0, degreeCount) wrap octave by octave
//   noteToDegree(note)       — per-octave degree index of an in-scale note
//                              (0..degreeCount-1), or -1 when out of scale
//   degreeCount()            — number of distinct intervals (1..12)
//
// The baseOctave convention is the MIDI octave number: degree 0 of baseOctave
// 5 rooted at pitch class 0 is MIDI note 60. degreeToNote returns the raw
// note as an int (it may fall outside 0-127 for extreme octave/degree
// combinations); callers range-check before emission and containsNote
// validates membership.
//
// Global constraints honored here: a plain value type with static size (no
// heap), every method a bounded loop over at most 12 intervals (no
// allocation, no locks, no I/O — tick-path safe). This header is deliberately
// free of any Pico SDK include so it compiles unchanged in the firmware and
// the host test build.

namespace common {

class Scale {
public:
    // Default: Major rooted at C (template 0).
    constexpr Scale() : root_(0), templateIndex_(0) {}

    // `root` is a pitch class and wraps modulo 12; `templateIndex` clamps to
    // [0, scaleTemplateCount()-1] so a Scale is always valid, whatever data
    // flows in (deterministic, never out of bounds).
    constexpr Scale(uint8_t root, uint8_t templateIndex)
        : root_(static_cast<uint8_t>(root % 12)),
          templateIndex_(templateIndex < static_cast<uint8_t>(scaleTemplateCount())
                             ? templateIndex
                             : static_cast<uint8_t>(scaleTemplateCount() - 1)) {}

    constexpr uint8_t root() const { return root_; }
    constexpr uint8_t templateIndex() const { return templateIndex_; }

    constexpr bool operator==(const Scale& other) const {
        return root_ == other.root_ && templateIndex_ == other.templateIndex_;
    }
    constexpr bool operator!=(const Scale& other) const { return !(*this == other); }

    // Is the MIDI note (0-127) a member of the scale?
    bool containsNote(uint8_t note) const;

    // Nearest in-scale MIDI note within 0-127. Total for every input note:
    // every scale contains its root, so an in-scale note always exists in
    // range. Distance ties resolve to the LOWER note (deterministic).
    uint8_t nearestNoteInScale(uint8_t note) const;

    // MIDI note of scale degree `degree` anchored at `baseOctave` (degree 0
    // = the root in that octave). Degrees wrap across octaves in both
    // directions. Bounded work regardless of the degree value.
    int degreeToNote(int degree, int baseOctave) const;

    // Per-octave degree index (0..degreeCount()-1) of an in-scale note, or -1
    // when the note is not in the scale.
    int noteToDegree(uint8_t note) const;

    // Number of distinct intervals in the template (1..12).
    int degreeCount() const;

private:
    uint8_t root_;          // pitch class 0-11
    uint8_t templateIndex_;  // index into common::SCALE_TEMPLATES
};

}  // namespace common
