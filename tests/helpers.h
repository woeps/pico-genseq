#pragma once

// Value-style adapters over the in-place (by-reference) view / reducer API, so tests can keep
// writing `auto next = handle(view, state, event)` and compare states.

#include "ui/Event.h"
#include "ui/state/Reducer.h"
#include "ui/state/UIState.h"
#include "ui/views/IView.h"

namespace test {

template <class View>
ui::state::UIState handle(View& view, ui::state::UIState state, const ui::events::Event& event) {
    view.handleEvent(state, event);
    return state;
}

inline ui::state::UIState reduced(ui::state::UIState state, const ui::events::Event& event,
                                  ui::IView* view) {
    ui::state::reduce(state, event, view);
    return state;
}

}  // namespace test
