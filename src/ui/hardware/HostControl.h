#pragma once

// -----------------------------------------------------------------------------
// HostControl - a text command channel for autonomous bench testing.
//
// Lets a host (the agent / a test script) drive GenSeq WITHOUT the physical USB
// keyboard, by reading newline-framed "H:" commands from stdin (the UART0 /
// picoprobe bridge on this firmware, since the device USB is a HOST port for
// the keyboard) and translating them into exactly the ui::events::Event values
// a real keypress would produce. Those events are dispatched through the SAME
// StateManager::dispatch() seam the keyboard uses, so the state machine cannot
// tell a host command from a keypress: no new path into the UI, no new risk.
//
// It exists to make hardware verification hands-off (persist-settings tasks
// 12.3/12.4): set a known config, trigger a save at a controlled instant, reset
// the target, and read the restored state back - all over one serial line.
//
// Compile-gated behind GENSEQ_HOST_CONTROL (OFF by default) so the shipping and
// timing firmwares are byte-identical to a build without it. Everything here is
// core0-only, allocation-free (fixed line buffer), and polled from the UI loop.
//
// Command grammar (one per line, case-insensitive verb):
//   H:key <name> [mods]   synth KEY_PRESSED+KEY_RELEASED (e.g. "H:key s ctrl")
//   H:save                = Ctrl+S  (sets saveRequested; the real commit path)
//   H:play                = SPACE   (toggles transport)
//   H:view <1..3>         = F1/F2/F3 (INIT/SETTINGS/PATTERNS)
//   H:up|down|left|right [coarse]   navigation in the active view
//   H:bpm <40..255>       go to SETTINGS and step BPM to the target value
//   H:pattern <i> on|off  activate / deactivate pattern i (PATTERNS view)
//   H:dump                print the current UIState as "D:" key=value lines
//   H:reset               watchdog self-reboot (autonomous power-cycle)
//   H:ping                print "H:pong" (liveness probe)
//
// All replies are line-framed and prefixed so a host parser can lock onto them:
//   "H:ok <verb>"      command accepted
//   "H:err <reason>"   command rejected
//   "D:..."            a dump line
// -----------------------------------------------------------------------------

#ifndef GENSEQ_HOST_CONTROL
#define GENSEQ_HOST_CONTROL 0
#endif

namespace hardware {

class HostControl {
public:
    // Poll stdin for at most one complete command line and, if present, apply
    // it. Non-blocking: reads only characters already buffered, so it adds a
    // few microseconds to the UI loop when idle.
#if GENSEQ_HOST_CONTROL
    void poll();
#else
    // Compiled away to nothing when host-control is disabled: an inline empty
    // body so the call site in UIController::update() needs no linked symbol
    // and the shipping firmware carries zero host-control code.
    void poll() {}
#endif

private:
#if GENSEQ_HOST_CONTROL
    static constexpr unsigned LINE_MAX = 64;
    char line_[LINE_MAX] = {};
    unsigned len_ = 0;

    void dispatchLine(const char* line);
    void emitDump();
#endif
};

} // namespace hardware
