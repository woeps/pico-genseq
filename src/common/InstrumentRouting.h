#pragma once

#include <cstdint>

// Instrument routing model shared by core0 (UI mask maintenance) and core1 (playback).
// See .kiro/specs/help-me-brainstorm-an-additional/design.md section 1.
//
// An Instrument owns one MIDI channel and the patterns that sound on it. core0 folds the
// per-Instrument "members AND enabled AND NOT muted" into ONE effective mask per slot and sends it
// with INSTRUMENT_SET; core1 keeps only the RouteTable below. Header-only and SDK-free so it
// compiles unchanged in the firmware and the host test builds.

namespace common {

constexpr uint8_t MAX_INSTRUMENTS = 16;     // one per MIDI channel
constexpr uint8_t MAX_ROUTE_PATTERNS = 15;  // mirrors ui::state::MAX_PATTERNS

// Mask of all valid pattern bits.
constexpr uint16_t ROUTE_PATTERN_MASK = static_cast<uint16_t>((1u << MAX_ROUTE_PATTERNS) - 1u);

// Mask update for removing pattern `index`: drops bit `index` and shifts the higher bits down.
constexpr uint16_t removePatternBit(uint16_t mask, uint8_t index) {
    const uint16_t lower = index == 0 ? 0 : static_cast<uint16_t>(mask & ((1u << index) - 1u));
    const uint16_t upper = static_cast<uint16_t>(mask >> (index + 1u));
    return static_cast<uint16_t>(lower | (upper << index));
}

// core1's view of the routing.
//   channel[s]  : protocol channel 0..15 of slot s
//   patterns[s] : effective mask of patterns that sound on slot s
//   routes[p]   : transpose, bit s set == pattern p sounds on slot s (rebuilt by set())
struct RouteTable {
    uint8_t channel[MAX_INSTRUMENTS];
    uint16_t patterns[MAX_INSTRUMENTS];
    uint16_t routes[MAX_ROUTE_PATTERNS];

    RouteTable() { reset(); }

    // Boot default: slot 0 on channel 0 (user channel 1) plays pattern 0, everything else empty.
    // This is the pre-Instrument behaviour, so core1 sounds sensible before the UI sends anything.
    void reset() {
        for (uint8_t s = 0; s < MAX_INSTRUMENTS; ++s) {
            channel[s] = s;
            patterns[s] = 0;
        }
        for (uint8_t p = 0; p < MAX_ROUTE_PATTERNS; ++p) routes[p] = 0;
        patterns[0] = 0x0001;
        rebuildRoutes();
    }

    // Replace slot `slot`. Out-of-range input is ignored; the mask is limited to valid patterns.
    void set(uint8_t slot, uint8_t protocolChannel, uint16_t mask) {
        if (slot >= MAX_INSTRUMENTS || protocolChannel > 15) return;
        channel[slot] = protocolChannel;
        patterns[slot] = static_cast<uint16_t>(mask & ROUTE_PATTERN_MASK);
        rebuildRoutes();
    }

    // Pattern `index` was removed: shift every slot's mask the same way the UI does.
    void removePattern(uint8_t index) {
        for (uint8_t s = 0; s < MAX_INSTRUMENTS; ++s) {
            patterns[s] = removePatternBit(patterns[s], index);
        }
        rebuildRoutes();
    }

private:
    void rebuildRoutes() {
        for (uint8_t p = 0; p < MAX_ROUTE_PATTERNS; ++p) {
            uint16_t r = 0;
            for (uint8_t s = 0; s < MAX_INSTRUMENTS; ++s) {
                if ((patterns[s] >> p) & 1u) r |= static_cast<uint16_t>(1u << s);
            }
            routes[p] = r;
        }
    }
};

}  // namespace common
