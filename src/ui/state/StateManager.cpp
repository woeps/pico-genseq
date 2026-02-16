#include "StateManager.h"
#include "Reducer.h"

namespace ui::state {

StateManager::StateManager() {}

void StateManager::dispatch(const events::Event& event) {
    UIState newState = reduce(currentState, event);
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
