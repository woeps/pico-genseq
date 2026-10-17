#pragma once

#include "Types.h"

namespace ui {

// Human-readable name for tracing and labels. Returns "?" for unnamed keys.
// Not used by the reducer, which dispatches on combo() values.
const char* toName(KeyId id);

} // namespace ui
