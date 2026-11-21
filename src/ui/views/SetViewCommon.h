#pragma once

#include "../Event.h"
#include "../hardware/LedMatrix.h"
#include "../state/UIState.h"

// Behaviour shared by the Gate, Note and Velocity set views (separate-velocity-set-gate-set-
// and-note-set): slot navigation and library keys, the slot / user maps and the go-to picker
// and notice overlays.

namespace ui {

// Library keys, handled before a view's own keys. Returns true when the event was a library
// key (the caller then stops). Any key press also clears a pending set notice.
//   Ctrl+Left / Ctrl+Right  previous / next occupied slot (wraps)
//   Ctrl+N                  new set      Ctrl+D  copy set      Ctrl+Delete  delete set
// Ctrl+G (go to pattern) is routed by state::reduce().
bool handleSetLibraryKey(state::UIState& state, state::SetType type, const events::Event& event);

// Overlay drawn after a view's own rendering:
//   row 15: slot map, one pixel per slot (occupied dim, selected bright)
//   row 14: user map, one pixel per pattern that references the selected set; several lit
//           pixels mean the set is shared
// While the go-to picker is open it replaces the whole display with the candidate list.
// A pending notice is written into the value area.
void drawSetOverlay(hardware::LedMatrix& matrix, const state::UIState& state, state::SetType type);

}  // namespace ui
