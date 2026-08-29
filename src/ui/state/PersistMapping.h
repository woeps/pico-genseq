#pragma once

#include "UIState.h"
#include "../../persistence/PersistableConfig.h"

// Pure, SDK-free translation between the core0 UIState mirror and the durable
// persistence::PersistableConfig snapshot (persist-settings feature, task 6.3).
//
// These functions perform ONLY the in-memory field mapping. They deliberately
// do NOT touch flash, the multicore FIFO, or commands:: - so this translation
// unit is host-compilable independent of the Pico SDK. The command-replay side
// of restore (which does pull the SDK) lives in Reducer.cpp
// (restoreSequencerFromState).
namespace ui::state {

// Map the durable fields of a UIState mirror into a PersistableConfig snapshot.
// Only [0, patternCount) patterns are populated; patternCount is clamped to
// persistence::MAX_PATTERNS. Transient draft/dirty/selection fields are ignored.
persistence::PersistableConfig toPersistableConfig(const UIState& state);

// Inverse mapping: populate a UIState from a loaded PersistableConfig. Rebuilds
// patternCount, bpm, the activePatterns bitmask, and the per-pattern gate/pitch/
// velocity configs. Draft/dirty/selection fields are left at their defaults.
void applyPersistableConfig(UIState& state, const persistence::PersistableConfig& cfg);

} // namespace ui::state
