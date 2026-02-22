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

} // namespace ui::state
