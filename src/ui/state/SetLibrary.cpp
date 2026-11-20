#include "SetLibrary.h"

namespace ui::state {

namespace {

uint8_t& refOf(PatternRefs& r, SetType type) {
    switch (type) {
        case SetType::GATE: return r.gate;
        case SetType::NOTE: return r.note;
        default:            return r.velocity;
    }
}

uint8_t refOf(const PatternRefs& r, SetType type) {
    switch (type) {
        case SetType::GATE: return r.gate;
        case SetType::NOTE: return r.note;
        default:            return r.velocity;
    }
}

uint16_t& occupiedRef(UIState& s, SetType type) {
    switch (type) {
        case SetType::GATE: return s.gateLib.occupied;
        case SetType::NOTE: return s.noteLib.occupied;
        default:            return s.velocityLib.occupied;
    }
}

int lowestFree(uint16_t occupied) {
    for (uint8_t i = 0; i < SET_LIBRARY_SIZE; ++i) {
        if (!((occupied >> i) & 1u)) return i;
    }
    return -1;
}

void resetSlot(UIState& s, SetType type, uint8_t slot) {
    switch (type) {
        case SetType::GATE:     s.gateLib.slots[slot] = GateSetConfig{}; break;
        case SetType::NOTE:     s.noteLib.slots[slot] = PitchSetConfig{}; break;
        default:                s.velocityLib.slots[slot] = VelocitySetConfig{}; break;
    }
}

void copySlot(UIState& s, SetType type, uint8_t dst, uint8_t src) {
    switch (type) {
        case SetType::GATE:     s.gateLib.slots[dst] = s.gateLib.slots[src]; break;
        case SetType::NOTE:     s.noteLib.slots[dst] = s.noteLib.slots[src]; break;
        default:                s.velocityLib.slots[dst] = s.velocityLib.slots[src]; break;
    }
}

} // namespace

uint16_t occupiedMask(const UIState& state, SetType type) {
    switch (type) {
        case SetType::GATE: return state.gateLib.occupied;
        case SetType::NOTE: return state.noteLib.occupied;
        default:            return state.velocityLib.occupied;
    }
}

uint8_t lowestOccupied(const UIState& state, SetType type) {
    const uint16_t occ = occupiedMask(state, type);
    for (uint8_t i = 0; i < SET_LIBRARY_SIZE; ++i) {
        if ((occ >> i) & 1u) return i;
    }
    return 0;
}

uint16_t usersOf(const UIState& state, SetType type, uint8_t slot) {
    uint16_t mask = 0;
    const uint8_t n = state.patternCount < MAX_PATTERNS ? state.patternCount : MAX_PATTERNS;
    for (uint8_t p = 0; p < n; ++p) {
        if (refOf(state.patternRefs[p], type) == slot) mask |= static_cast<uint16_t>(1u << p);
    }
    return mask;
}

uint8_t popcount16(uint16_t mask) {
    uint8_t n = 0;
    while (mask) { n += mask & 1u; mask >>= 1; }
    return n;
}

uint8_t nextOccupied(const UIState& state, SetType type, uint8_t from, int dir) {
    const uint16_t occ = occupiedMask(state, type);
    const int step = dir < 0 ? -1 : 1;
    int s = from;
    for (uint8_t i = 0; i < SET_LIBRARY_SIZE - 1; ++i) {
        s = (s + step + SET_LIBRARY_SIZE) % SET_LIBRARY_SIZE;
        if ((occ >> s) & 1u) return static_cast<uint8_t>(s);
    }
    return from;
}

bool createSet(UIState& state, SetType type, uint8_t& newSlot) {
    uint16_t& occ = occupiedRef(state, type);
    const int slot = lowestFree(occ);
    if (slot < 0) return false;
    resetSlot(state, type, static_cast<uint8_t>(slot));
    occ |= static_cast<uint16_t>(1u << slot);
    newSlot = static_cast<uint8_t>(slot);
    return true;
}

bool copySet(UIState& state, SetType type, uint8_t src, uint8_t& newSlot) {
    uint16_t& occ = occupiedRef(state, type);
    if (src >= SET_LIBRARY_SIZE || !((occ >> src) & 1u)) return false;
    const int slot = lowestFree(occ);
    if (slot < 0) return false;
    copySlot(state, type, static_cast<uint8_t>(slot), src);
    occ |= static_cast<uint16_t>(1u << slot);
    newSlot = static_cast<uint8_t>(slot);
    return true;
}

bool deleteSet(UIState& state, SetType type, uint8_t slot) {
    uint16_t& occ = occupiedRef(state, type);
    if (slot >= SET_LIBRARY_SIZE || !((occ >> slot) & 1u)) return false;
    if (popcount16(occ) <= 1) return false;               // keep at least one set
    if (usersOf(state, type, slot) != 0) return false;    // still referenced
    resetSlot(state, type, slot);
    occ &= static_cast<uint16_t>(~(1u << slot));
    return true;
}

bool assignRef(UIState& state, uint8_t pattern, SetType type, uint8_t slot) {
    if (pattern >= state.patternCount || pattern >= MAX_PATTERNS) return false;
    if (slot >= SET_LIBRARY_SIZE || !((occupiedMask(state, type) >> slot) & 1u)) return false;
    refOf(state.patternRefs[pattern], type) = slot;
    return true;
}

} // namespace ui::state
