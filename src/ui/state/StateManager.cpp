#include "StateManager.h"
#include "Reducer.h"

namespace ui::state {

StateManager::StateManager() : views({nullptr, nullptr}) {}

void StateManager::dispatch(const events::Event& event) {
    IView* activeView = views[static_cast<size_t>(currentState.currentView)];
    UIState newState = reduce(currentState, event, activeView);
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
