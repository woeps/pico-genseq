#pragma once

#include "../state/UIState.h"
#include "../Event.h"

namespace ui {

class IView {
public:
    virtual ~IView() = default;

    virtual void onEnter() {}
    virtual void onExit() {}
    virtual state::UIState handleEvent(const state::UIState& state, const events::Event& event) {
        return state; // Default: no-op, return state unchanged
    }
    virtual void render(const state::UIState& state) = 0;
};

} // namespace ui
