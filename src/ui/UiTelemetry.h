#pragma once

#include <cstdint>

// core0 UI timing telemetry for the H:dump literal metrics (task 12.3).
// Written by UIController on core0, read by HostControl (also core0) — same
// core, so a plain read is consistent. Kept as a tiny shared header so the
// dump reports real measured numbers, not a proxy.

namespace ui {

// Wall-clock duration (us) of the most recent live RecordStore::commit — the
// save-time service gap, to compare against the <=100 ms limit.
extern uint32_t g_lastSaveMicros;

// Longest observed interval (us) between UI-loop update() calls — the UI
// responsiveness gap, to compare against the <=50 ms limit.
extern uint32_t g_maxUiGapMicros;

}  // namespace ui
