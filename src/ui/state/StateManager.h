#pragma once

#include "UIState.h"
#include "../Event.h"
#include "../views/IView.h"
#include <array>
#include <functional>

namespace ui::state {

class StateManager {
public:
    StateManager();
    ~StateManager() = default;
    
    const UIState& getState() const { return currentState; }
    
    // Set the view registry for looking up active view
    void setViews(std::array<IView*, 2> views) { this->views = views; }
    
    // Dispatch event - looks up active view from registry based on currentState.currentView
    void dispatch(const events::Event& event);
    
    using StateChangeListener = std::function<void(const UIState& newState)>;
    void subscribe(StateChangeListener listener);
    
private:
    UIState currentState;
    StateChangeListener listener_;
    std::array<IView*, 2> views;
};

extern StateManager& getStateManager();

} // namespace ui::state
