#pragma once

#include <cstdint>

// Stable Pattern runtime identity (performance-safe-foundation task 2.2,
// Requirements 5.1, 6.9, 7.8).
//
// The UI orders patterns by vector position, but a vector index is NOT a stable
// identity: removing or reordering one pattern silently retargets every index
// above it. Pending cross-core operations and note ownership must instead bind
// to an identity that does not move when another pattern is removed or
// reordered. `PatternId` is that identity.
//
// Identity design:
//   - `index`      : a small fixed slot 0..CAPACITY-1 into the runtime's
//                    preallocated Pattern storage. Reused when a pattern is
//                    removed and the slot is later re-allocated.
//   - `generation` : a monotonically increasing stamp bumped every time a slot
//                    is (re)allocated. Because a freed slot comes back with a
//                    HIGHER generation, an operation captured against the old
//                    occupant can be detected as stale and REJECTED (Req 7.8),
//                    rather than silently applying to the slot's new occupant.
//
// A `(index, generation)` pair therefore uniquely identifies one occupancy of
// one slot for the lifetime of the device, within the generation counter's
// range. This header is free of any Pico SDK include so it compiles unchanged
// in the firmware and the host test build.

namespace common {

// Fixed maximum number of simultaneously-live patterns. Fixed capacity keeps
// the runtime allocation-free (Req 7.2 / 7.9); the allocator never grows.
constexpr uint8_t PATTERN_CAPACITY = 16;

struct PatternId {
    uint8_t index;        // slot 0..PATTERN_CAPACITY-1
    uint32_t generation;  // occupancy stamp; 0 == invalid/never-allocated

    constexpr PatternId() : index(0), generation(0) {}
    constexpr PatternId(uint8_t slot, uint32_t gen) : index(slot), generation(gen) {}

    // generation 0 is reserved to mean "no identity".
    constexpr bool valid() const { return generation != 0; }

    constexpr bool operator==(const PatternId& o) const {
        return index == o.index && generation == o.generation;
    }
    constexpr bool operator!=(const PatternId& o) const { return !(*this == o); }
};

// Allocates stable PatternIds over a fixed set of slots. One allocator lives in
// the Pattern runtime on core1. It performs no heap allocation.
class PatternIdAllocator {
public:
    PatternIdAllocator() {
        for (uint8_t i = 0; i < PATTERN_CAPACITY; ++i) {
            occupied_[i] = false;
            generation_[i] = 0;
        }
    }

    // Reserve the lowest free slot and stamp it with a fresh generation.
    // Returns an invalid PatternId (generation 0) when every slot is occupied.
    PatternId allocate() {
        for (uint8_t i = 0; i < PATTERN_CAPACITY; ++i) {
            if (!occupied_[i]) {
                occupied_[i] = true;
                generation_[i] = ++nextGeneration_;  // never 0: starts at 1
                return PatternId(i, generation_[i]);
            }
        }
        return PatternId{};  // full
    }

    // Release a slot. A later allocate() may reuse the slot with a HIGHER
    // generation, which is what makes stale references detectable. A stale or
    // mismatched id is ignored.
    void release(PatternId id) {
        if (!isLive(id)) return;
        occupied_[id.index] = false;
    }

    // True only when `id` names the CURRENT occupant of its slot — i.e. the slot
    // is occupied and its live generation matches. A reference captured against a
    // previous occupant (or a never-allocated id) is NOT live, so operations bound
    // to it are rejected (Req 7.8).
    bool isLive(PatternId id) const {
        return id.valid() && id.index < PATTERN_CAPACITY && occupied_[id.index] &&
               generation_[id.index] == id.generation;
    }

    // The id currently occupying `slot`, or an invalid id when the slot is free.
    PatternId current(uint8_t slot) const {
        if (slot >= PATTERN_CAPACITY || !occupied_[slot]) return PatternId{};
        return PatternId(slot, generation_[slot]);
    }

    uint8_t liveCount() const {
        uint8_t n = 0;
        for (uint8_t i = 0; i < PATTERN_CAPACITY; ++i) {
            if (occupied_[i]) ++n;
        }
        return n;
    }

private:
    bool occupied_[PATTERN_CAPACITY];
    uint32_t generation_[PATTERN_CAPACITY];
    uint32_t nextGeneration_ = 0;  // ++ before first use -> first generation is 1
};

}  // namespace common
