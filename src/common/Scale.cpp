// Scale constraint math (generative-engine-expansion task 1, FR3/FR4/AC2).
//
// All methods are bounded loops over at most 12 mask bits or (for the
// nearest-note fallback) at most the 128 MIDI notes: no allocation, no locks,
// no I/O — safe to call from the tick path.

#include "Scale.h"

namespace common {

namespace {

// Population count of the 12-bit interval mask (bounded 12 iterations).
int maskBitCount(uint16_t mask) {
    int bits = 0;
    for (int i = 0; i < 12; ++i) {
        if (mask & (1u << i)) ++bits;
    }
    return bits;
}

// Interval (semitones above the root) of the step-th set bit of the mask.
// `step` must be in [0, maskBitCount(mask)); the root bit guarantees at least
// one set bit, so the loop always returns in range.
int intervalAt(uint16_t mask, int step) {
    int seen = 0;
    for (int i = 0; i < 12; ++i) {
        if (mask & (1u << i)) {
            if (seen == step) return i;
            ++seen;
        }
    }
    return 0;  // unreachable for an in-range step
}

// Pitch-class distance of `note` above the scale root, in 0..11.
int intervalFromRoot(uint8_t root, uint8_t note) {
    return ((static_cast<int>(note) - static_cast<int>(root)) % 12 + 12) % 12;
}

}  // namespace

bool Scale::containsNote(uint8_t note) const {
    const uint16_t mask = scaleTemplate(templateIndex_).intervals;
    return (mask >> intervalFromRoot(root_, note)) & 1u;
}

uint8_t Scale::nearestNoteInScale(uint8_t note) const {
    // Bounded window search, lower candidate first at every distance so an
    // equidistant tie deterministically prefers the LOWER note. Any 12
    // consecutive MIDI notes cover every pitch class exactly once, so an
    // in-scale note always exists within +/-12 of any note in 0-127.
    for (int distance = 0; distance <= 12; ++distance) {
        const int down = static_cast<int>(note) - distance;
        if (down >= 0 && containsNote(static_cast<uint8_t>(down))) {
            return static_cast<uint8_t>(down);
        }
        if (distance > 0) {
            const int up = static_cast<int>(note) + distance;
            if (up <= 127 && containsNote(static_cast<uint8_t>(up))) {
                return static_cast<uint8_t>(up);
            }
        }
    }
    // Unreachable (the window above always finds a member), but keeps the
    // contract total with a bounded scan: every scale contains its root, so
    // some note in 0-127 (e.g. root+12) is always in scale.
    for (int n = 0; n <= 127; ++n) {
        if (containsNote(static_cast<uint8_t>(n))) return static_cast<uint8_t>(n);
    }
    return static_cast<uint8_t>(root_);  // dead end guard, never reached
}

int Scale::degreeToNote(int degree, int baseOctave) const {
    const int count = degreeCount();  // >= 1: the root bit is always set
    // Floor division so negative degrees wrap DOWN an octave (C truncation
    // would wrap -1 of a 7-note scale to degree 6 inside the same octave).
    const int octave = degree >= 0 ? degree / count : -((-degree + count - 1) / count);
    const int step = degree - octave * count;  // 0..count-1
    const uint16_t mask = scaleTemplate(templateIndex_).intervals;
    return baseOctave * 12 + root_ + octave * 12 + intervalAt(mask, step);
}

int Scale::noteToDegree(uint8_t note) const {
    if (!containsNote(note)) return -1;
    const uint16_t mask = scaleTemplate(templateIndex_).intervals;
    const int interval = intervalFromRoot(root_, note);
    int degree = 0;
    for (int i = 0; i < interval; ++i) {
        if (mask & (1u << i)) ++degree;
    }
    return degree;
}

int Scale::degreeCount() const {
    return maskBitCount(scaleTemplate(templateIndex_).intervals);
}

}  // namespace common
