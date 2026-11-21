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

// MIDI output load and timing fault (Requirement 11). Written only by core1, read by core0 for
// the load indicator pixel and the fault mirror in UIState.
//   g_midiLateMs        : ms since boot when a byte last waited >= 10 ms in the output buffer
//                         (0 = never).
//   g_timingFaultLatched: 1 while core1's timing fault is latched (cleared by Play).
//   g_timingFaultCause  : PerformanceFaultCause of the latched fault (0 = NONE).
extern volatile uint32_t g_midiLateMs;
extern volatile uint32_t g_timingFaultLatched;
extern volatile uint32_t g_timingFaultCause;

#ifndef GENSEQ_HOST_CONTROL
#define GENSEQ_HOST_CONTROL 0
#endif
#if GENSEQ_HOST_CONTROL
// Task 11.3 bench MIDI tap (host-control builds only). core1 appends every byte
// handed to the MIDI UART while armed; core0 reads it only after disarming.
constexpr uint32_t kMidiTapCapacity = 4096;
extern volatile uint32_t g_midiTapArmed;
extern volatile uint32_t g_midiTapCount;
extern uint8_t g_midiTap[kMidiTapCapacity];

// Scheduler metrics snapshot (host-control builds only). core1 republishes its private
// PerformanceMetrics here every update(); core0 reads them for H:dump. core0 asks for a
// fresh measurement window by setting g_resetMetricsRequest; core1 clears the metrics and
// the flag on its next update(). Single aligned 32-bit accesses, advisory diagnostics only.
extern volatile uint32_t g_maxLatenessUs;     // max scheduler lateness since the last reset
extern volatile uint32_t g_missedTicks;       // logical ticks missed since the last reset
extern volatile uint32_t g_batchTimeouts;     // command batches abandoned since the last reset
extern volatile uint32_t g_resetMetricsRequest;
#endif

}  // namespace sequencer
