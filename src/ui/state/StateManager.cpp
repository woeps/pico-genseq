#include "StateManager.h"
#include "Reducer.h"

namespace ui::state {

StateManager::StateManager() { views.fill(nullptr); }

void StateManager::dispatch(const events::Event& event) {
    IView* activeView = views[static_cast<size_t>(currentState.currentView)];
    // Reduce in place: UIState is ~1.4 KB, so copying it per event (as the old
    // copy-and-return reducer did) overflowed core0's 2 KB stack. reduce()
    // mutates currentState directly; the listener then renders the result.
    reduce(currentState, event, activeView);
    if (listener_) {
        listener_(currentState);
    }
}

void StateManager::setState(const UIState& newState) {
    currentState = newState;
    if (listener_) {
        listener_(currentState);
    }
}

void StateManager::subscribe(StateChangeListener listener) {
    listener_ = listener;
}

StateManager& getStateManager() {
    static StateManager instance;
    return instance;
}

} // namespace ui::state
