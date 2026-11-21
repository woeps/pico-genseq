#pragma once

#include <cstdint>
#include "UIState.h"

// Pure, SDK-free helpers for the Instrument list (help-me-brainstorm-an-additional, design
// section 3). None of these send commands; the Reducer wrappers call them and then send
// INSTRUMENT_SET. Every mutating helper checks its precondition first, leaves the state untouched
// when it returns false, and sets state.instrumentNotice for user-visible refusals.

namespace ui::state {

// Effective pattern mask of a slot as core1 sees it: members AND enabled AND NOT muted, limited to
// existing patterns. A free slot gives 0.
uint16_t effectiveMask(const UIState& state, uint8_t slot);

// Protocol channel (0..15) of a slot.
uint8_t protocolChannelOf(const UIState& state, uint8_t slot);

// Lowest user channel (1..16) no occupied Instrument uses; 0 when all 16 are taken.
uint8_t lowestFreeChannel(const UIState& state);

// Instruments that currently use `pattern` as a member (bitmask over slots).
uint16_t instrumentsOf(const UIState& state, uint8_t pattern);

// Create an Instrument in the lowest free slot with the lowest free channel, empty masks, not
// muted; select it. False + FULL notice when all slots are occupied.
bool createInstrumentSlot(UIState& state, uint8_t& newSlot);

// Free a slot. False (state unchanged) when the slot is free, or is the only occupied Instrument
// (LAST_INSTRUMENT notice). Selects the lowest occupied slot when the deleted one was selected.
bool deleteInstrumentSlot(UIState& state, uint8_t slot);

// Move the channel to the next channel in `direction` (>0 up, <0 down) that no other Instrument
// uses; no wrap. False + NO_FREE_CHANNEL when none exists in that direction.
bool stepInstrumentChannel(UIState& state, uint8_t slot, int direction);

bool toggleInstrumentMuteFlag(UIState& state, uint8_t slot);

// Assign / unassign pattern `p` (sets or clears both its members and enabled bit).
// False when the slot is free or p >= patternCount.
bool toggleInstrumentMember(UIState& state, uint8_t slot, uint8_t p);

// Flip the route enable bit of pattern `p`. False + NOT_MEMBER notice when p is not a member.
bool toggleInstrumentRouteFlag(UIState& state, uint8_t slot, uint8_t p);

// Add pattern `p` to the Pattern Selection of `slot` (route on). Used when a pattern is created.
void assignPatternToInstrument(UIState& state, uint8_t slot, uint8_t p);

// Pattern `index` was removed: shift every Instrument's masks (common::removePatternBit).
void removePatternFromInstruments(UIState& state, uint8_t index);

// Move the selection to the previous/next occupied slot (no wrap). Returns the new slot.
uint8_t selectInstrumentStep(UIState& state, int direction);

// Move the pattern cursor over the 5 x 3 grid by (dx, dy), clamped.
void moveInstrumentCursor(UIState& state, int dx, int dy);

// Check the invariants of design section 2 (used by tests).
bool validateInstruments(const UIState& state);

} // namespace ui::state
