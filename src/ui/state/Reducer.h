#pragma once

#include "UIState.h"
#include "../Event.h"

namespace ui::state {

// Pure reducer: takes old state + event, returns new state
UIState reduce(const UIState& state, const events::Event& event);

// State-setter functions: mutate state and send the corresponding command
void setBpm(UIState& state, int bpm);
void setPlaying(UIState& state, bool playing);
void setCurrentView(UIState& state, ViewId viewId);

} // namespace ui::state
