#include "Scale.h"

namespace common {
namespace scale {

namespace {

// Interval sets as a 12-bit mask with the TONIC at bit 0 (before rotation to the
// requested root). Semitone offsets from the tonic:
//   major:         0 2 4 5 7 9 11
//   natural minor: 0 2 3 5 7 8 10
// CHROMATIC is handled directly (all 12 bits) and needs no entry here.
constexpr uint16_t kMajorAtC = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5) |
                               (1u << 7) | (1u << 9) | (1u << 11);
constexpr uint16_t kMinorNaturalAtC = (1u << 0) | (1u << 2) | (1u << 3) | (1u << 5) |
                                      (1u << 7) | (1u << 8) | (1u << 10);

// Rotate a 12-bit mask up by `root` pitch classes (tonic moves from 0 to root).
uint16_t rotate12(uint16_t maskAtC, uint8_t root) {
    const uint8_t r = root % 12u;
    const uint16_t rotated =
        static_cast<uint16_t>(((maskAtC << r) | (maskAtC >> (12u - r)))) & 0x0FFFu;
    return r == 0 ? (maskAtC & 0x0FFFu) : rotated;
}

}  // namespace

uint16_t membershipMask(ScaleId id, uint8_t root) {
    switch (id) {
        case ScaleId::CHROMATIC:
            return 0x0FFFu;  // every pitch class in scale; root is irrelevant
        case ScaleId::MAJOR:
            return rotate12(kMajorAtC, root);
        case ScaleId::MINOR_NATURAL:
            return rotate12(kMinorNaturalAtC, root);
        default:
            // Unknown id: fail safe to unconstrained so emission never suppresses
            // everything on a value a newer save introduced.
            return 0x0FFFu;
    }
}

bool inScale(uint16_t mask, uint8_t n) {
    return (mask >> (n % 12u)) & 1u;
}

bool quantize(uint16_t mask, QuantizeMode mode, uint8_t n, uint8_t& out) {
    // Already in scale: identity for every mode.
    if (inScale(mask, n)) {
        out = n;
        return true;
    }

    switch (mode) {
        case QuantizeMode::SKIP:
            // Out of scale under SKIP: suppress the step. `out` is left unset;
            // the caller must not read it on a false return.
            return false;

        case QuantizeMode::DOWN: {
            // Walk down up to 12 semitones for the nearest allowed pitch at or
            // below n; clamp at 0.
            for (int c = static_cast<int>(n) - 1; c >= 0 && static_cast<int>(n) - c <= 12; --c) {
                if (inScale(mask, static_cast<uint8_t>(c))) {
                    out = static_cast<uint8_t>(c);
                    return true;
                }
            }
            out = n;  // no member found within range (only on a degenerate mask)
            return true;
        }

        case QuantizeMode::UP: {
            for (int c = static_cast<int>(n) + 1; c <= 127 && c - static_cast<int>(n) <= 12; ++c) {
                if (inScale(mask, static_cast<uint8_t>(c))) {
                    out = static_cast<uint8_t>(c);
                    return true;
                }
            }
            out = n;
            return true;
        }

        case QuantizeMode::NEAREST:
        default: {
            // Expand outward one semitone at a time; on a tie prefer the lower
            // pitch (down before up) for a defined, deterministic result.
            for (int d = 1; d <= 12; ++d) {
                const int down = static_cast<int>(n) - d;
                if (down >= 0 && inScale(mask, static_cast<uint8_t>(down))) {
                    out = static_cast<uint8_t>(down);
                    return true;
                }
                const int up = static_cast<int>(n) + d;
                if (up <= 127 && inScale(mask, static_cast<uint8_t>(up))) {
                    out = static_cast<uint8_t>(up);
                    return true;
                }
            }
            out = n;
            return true;
        }
    }
}

}  // namespace scale
}  // namespace common
