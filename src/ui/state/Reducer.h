#pragma once

#include "UIState.h"
#include "../Event.h"
#include "../views/IView.h"

namespace ui::state {

// Delegates to the active view's handleEvent method
UIState reduce(const UIState& state, const events::Event& event, ui::IView* activeView);

// State-setter functions: mutate state and send the corresponding command
void setBpm(UIState& state, int bpm);
void setPlaying(UIState& state, bool playing);
void setCurrentView(UIState& state, ViewId viewId);
void setValue(UIState& state, int value);
void beginGateSetEdit(UIState& state);
void moveGateSetProperty(UIState& state, int direction);
void adjustGateSetValue(UIState& state, int delta, bool coarse = false);
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
void restoreSequencerFromState(const UIState& state);

} // namespace ui::state
