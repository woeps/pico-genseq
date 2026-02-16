#pragma once

#include "UIState.h"
#include "../Event.h"
#include <functional>

namespace ui::state {

class StateManager {
public:
    StateManager();
    ~StateManager() = default;
    
    const UIState& getState() const { return currentState; }
    
    void dispatch(const events::Event& event);
    
    using StateChangeListener = std::function<void(const UIState& newState)>;
    void subscribe(StateChangeListener listener);
    
private:
    UIState currentState;
    StateChangeListener listener_;
};

extern StateManager& getStateManager();

} // namespace ui::state
