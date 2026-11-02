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

    // In-place boot mutation (core1 boot-fault fix). UIState is ~1.4 KB, so a
    // by-value `UIState bootState` local in UIController::initialize() consumed
    // ~70% of core0's 2 KB stack on the deepest boot path (FRAM load +
    // restoreSequencerFromState), overflowing it DOWN into core1's adjacent
    // launch stack and corrupting the entry/SP multicore_launch_core1 had pushed
    // there -> core1 HardFaulted at the trampoline before running any of its own
    // code (LED dark, core0 wedged on the FIFO). This mirrors the same in-place
    // fix dispatch()/reduce() already uses: mutate currentState (static storage)
    // directly, with no stack-resident copy. Returns a mutable reference for the
    // caller to fold a restored config into; call notifyStateChanged() after.
    UIState& mutableState() { return currentState; }
    // Notify the subscribed listener that currentState changed in place. Pairs
    // with mutableState(); same notify semantics as setState()/dispatch().
    void notifyStateChanged() { if (listener_) listener_(currentState); }

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
