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

    // Replace the entire state and notify the subscribed listener, mirroring
    // the notify-on-change behavior of dispatch(). Used at boot by
    // UIController::initialize() to install a configuration restored from flash
    // (persist-settings feature, task 7.1). Unlike dispatch() this does not run
    // the reducer or route through a view - the caller supplies a fully-formed
    // UIState. If no listener is subscribed yet, the state is still replaced.
    void setState(const UIState& newState);

    // Set the view registry for looking up active view
    void setViews(std::array<IView*, VIEW_COUNT> views) { this->views = views; }
    
    // Dispatch event - looks up active view from registry based on currentState.currentView
    void dispatch(const events::Event& event);
    
    using StateChangeListener = std::function<void(const UIState& newState)>;
    void subscribe(StateChangeListener listener);
    
private:
    UIState currentState;
    StateChangeListener listener_;
    std::array<IView*, VIEW_COUNT> views;
};

extern StateManager& getStateManager();

} // namespace ui::state
