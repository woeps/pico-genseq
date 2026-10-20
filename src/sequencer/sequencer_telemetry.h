#pragma once

#include <cstdint>

// Cross-core diagnostic telemetry (task 12.4 literal metrics).
//
// core1 (the sequencer) publishes a snapshot of its sounding-note count after
// every update(); core0 (host-control / UI) reads it for the H:dump stuck-note
// metric. A single aligned 32-bit load/store is atomic on the RP2040, so this
// needs no lock — it is advisory diagnostics, not control state. Declared in a
// dedicated SDK-free header so both cores share one definition.

namespace sequencer {

// Written ONLY by core1 (Sequencer::update), read by anyone. Number of patterns
// currently holding a sounding note. MUST be 0 after Stop / fail-closed fault
// (the post-Stop ownership invariant, Req 5.8) — the literal stuck-note signal.
extern volatile uint32_t g_soundingNotes;

}  // namespace sequencer
