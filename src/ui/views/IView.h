#pragma once

#include "../state/UIState.h"

namespace ui {

class IView {
public:
    virtual ~IView() = default;

    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void render(const state::UIState& state) = 0;
};

} // namespace ui
