#pragma once

#include <cstdint>
#include "UIState.h"

// Pure, SDK-free helpers for the Gate / Note / Velocity set libraries
// (separate-velocity-set-gate-set-and-note-set). None of these send commands;
// the reducer calls them and then performs the cross-core sync. Every mutating
// helper checks its precondition first and leaves the state untouched when it
// returns false.

namespace ui::state {

// Bitmask of the patterns (0..patternCount-1) whose reference for `type` is `slot`.
uint16_t usersOf(const UIState& state, SetType type, uint8_t slot);

// Number of set bits in a bitmask (user count for display).
uint8_t popcount16(uint16_t mask);

// Next occupied slot after `from` in direction `dir` (>0 forward, <0 backward),
// wrapping at the ends. Returns `from` when no other slot is occupied.
uint8_t nextOccupied(const UIState& state, SetType type, uint8_t from, int dir);

// Lowest-free-slot creation with default contents. False when the library is full.
bool createSet(UIState& state, SetType type, uint8_t& newSlot);

// Copy an occupied set into the lowest free slot. False when `src` is free or the
// library is full. Pattern references are not changed.
bool copySet(UIState& state, SetType type, uint8_t src, uint8_t& newSlot);

// Free a slot. False (state unchanged) when the slot is free, is the only
// occupied slot of its library, or is referenced by any pattern.
bool deleteSet(UIState& state, SetType type, uint8_t slot);

// Point a pattern's reference for `type` at `slot`. False when the pattern does
// not exist or the slot is out of range or free.
bool assignRef(UIState& state, uint8_t pattern, SetType type, uint8_t slot);

// Occupancy mask of a library.
uint16_t occupiedMask(const UIState& state, SetType type);

// Lowest occupied slot of a library (every library keeps at least one).
uint8_t lowestOccupied(const UIState& state, SetType type);

} // namespace ui::state
