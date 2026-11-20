#pragma once

#include "UIState.h"
#include "../Event.h"
#include "../views/IView.h"

namespace ui::state {

// Delegates to the active view's handleEvent method
void reduce(UIState& state, const events::Event& event, ui::IView* activeView);

// State-setter functions: mutate state and send the corresponding command
void setBpm(UIState& state, int bpm);
void setPlaying(UIState& state, bool playing);
void setCurrentView(UIState& state, ViewId viewId);
void setValue(UIState& state, int value);
void beginGateSetEdit(UIState& state);
void moveGateSetProperty(UIState& state, int direction);
void adjustGateSetValue(UIState& state, int delta, bool coarse = false);
void toggleGateStep(UIState& state);
void commitGateSetEdit(UIState& state);
void undoGateSetEdit(UIState& state);
void syncGateSet(const UIState& state, uint8_t index);
void beginPitchSetEdit(UIState& state);
void movePitchSetField(UIState& state, int direction);
void adjustPitchSetValue(UIState& state, int delta, bool coarse = false);
void commitPitchSetEdit(UIState& state);
void undoPitchSetEdit(UIState& state);
void syncPitchSet(const UIState& state, uint8_t index);
void beginVelocitySetEdit(UIState& state);
void moveVelocitySetField(UIState& state, int direction);
void adjustVelocitySetValue(UIState& state, int delta, bool coarse = false);
void commitVelocitySetEdit(UIState& state);
void undoVelocitySetEdit(UIState& state);
void syncVelocitySet(const UIState& state, uint8_t index);
void setSelectedPattern(UIState& state, uint8_t index);
void setSelectedPatternSet(UIState& state, PatternSet patternSet);
void addPattern(UIState& state);
void removePattern(UIState& state, uint8_t index);
void togglePatternActive(UIState& state, uint8_t index);

// Seed core1 from a restored UIState by replaying the existing sync-command
// path (persist-settings feature, task 6.3). Grows core1's pattern vector with
// PATTERN_ADD to match state.patternCount (pattern 0 already exists by
// default), syncs every pattern's gate/pitch/velocity sets, applies the active
// mask via PATTERN_ACTIVATE / PATTERN_DEACTIVATE, and sets the BPM. Pulls the
// SDK (commands::) so it is firmware-only, not host-compilable.
// --- Set libraries (separate-velocity-set-gate-set-and-note-set) -------------
// Point the three set views at the sets the selected pattern references (R4.2). Call when
// entering a set view from the PatternsView, before the matching begin*Edit.
void selectSlotsOfSelectedPattern(UIState& state);
// Switch a set view to `slot` of its library: an uncommitted draft is discarded (and the
// committed set re-sent to its users), then the slot's draft is begun. No-op on a free slot.
void selectSetSlot(UIState& state, SetType type, uint8_t slot);
// Step the selected slot to the previous/next occupied slot (wraps).
void stepSetSlot(UIState& state, SetType type, int direction);
// Library actions for the set views. A refused action leaves the libraries untouched and
// sets state.setNotice (LIBRARY_FULL / IN_USE / LAST_SET).
void createSetInView(UIState& state, SetType type);
void copySetInView(UIState& state, SetType type);
void deleteSetInView(UIState& state, SetType type);
// Change which set a pattern references. Rejects a free/out-of-range slot (reference kept).
// Sends the new set and the pattern's gen-params to core1 as one batch.
void assignSetRef(UIState& state, uint8_t pattern, SetType type, uint8_t slot);
// Step a pattern's reference to the previous/next occupied slot of the library (wraps).
void stepSetRef(UIState& state, uint8_t pattern, SetType type, int direction);

// --- Go to pattern (Requirement 10) -------------------------------------------
// From a set view: jump to a pattern that references the selected set. 0 users -> notice
// UNUSED; 1 user -> jump straight there; several -> open the picker (state.gotoPicker).
// reduce() routes Ctrl+G here while a set view is active, and while the picker is open
// consumes every key except the global ones (see gotoPickerHandleKey).
void openGotoPattern(UIState& state, SetType type);
// Move the picker cursor to the previous/next candidate pattern (ascending, wraps).
void gotoPickerMove(UIState& state, int direction);
// Select the highlighted pattern and switch to the PatternsView; an uncommitted draft is
// discarded (R10.3, R10.5).
void gotoPickerConfirm(UIState& state);
// Close the picker; slot, draft and view are unchanged (R10.4).
void gotoPickerCancel(UIState& state);

void restoreSequencerFromState(const UIState& state);

} // namespace ui::state
