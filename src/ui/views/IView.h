#pragma once

#include "../state/UIState.h"
#include "../Event.h"

namespace ui {

class IView {
public:
    virtual ~IView() = default;

    virtual void onEnter() {}
    virtual void onExit() {}
    // Mutate the passed UIState in place (generative-engine-expansion: UIState
    // grew to ~1.4 KB, so the previous copy-and-return signature stacked
    // multiple 1.4 KB copies per event and overflowed core0's 2 KB stack). The
    // active view edits `state` directly; a no-op view simply leaves it alone.
    virtual void handleEvent(state::UIState& state, const events::Event&) {
        (void)state;  // default: no change
    }
    virtual void render(const state::UIState& state) = 0;
};

} // namespace ui
