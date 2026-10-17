#pragma once

#include <cstdint>

#include "UIState.h"

// Persistent LED-matrix Timing Fault indication policy (performance-safe-
// foundation task 11.2, Requirements 10.4, 10.5).
//
// The 16x16 matrix is the only display. A latched Timing Fault must show a
// PERSISTENT indication that is DISTINCT from the normal, save-success,
// save-failure, and load-failure presentations (Req 10.4), and it must NOT
// auto-dismiss while input polling stays active (Req 10.5).
//
// The pixel-level rendering lives in the firmware LedMatrix driver, but the
// POLICY — what color/presence the fault maps to, that it overrides the save
// banner while latched, and that it never clears itself — is pure and
// host-testable. These helpers encode that policy; the firmware UI calls
// matrixFaultColor()/shouldShowFault() each frame to drive LedMatrix::fill.
//
// Colors are 0xGGRRBB (the LedMatrix convention). The fault color is a solid
// RED chosen to be unmistakable and distinct from every SaveBanner color.

namespace ui::state {

// Distinct matrix colors for each feedback state (0xGGRRBB). The fault red is
// distinct from the save-banner greens/ambers so the four states never collide.
inline constexpr uint32_t kFaultColor = 0x00FF00;    // solid red (G=00,R=FF,B=00)
inline constexpr uint32_t kSaveSuccessColor = 0xFF0000;  // green
inline constexpr uint32_t kSaveFailureColor = 0x7FFF00;  // amber (G=7F,R=FF)
inline constexpr uint32_t kLoadFailureColor = 0x3F0040;  // dim purple

// While a Timing Fault is latched the matrix shows the fault indication and it
// takes PRECEDENCE over any save/load banner (Req 10.4) — a fault is the more
// important state to surface. It is shown for as long as the latch holds and
// never auto-dismisses (Req 10.5): only an explicit Play acknowledgement clears
// `timingFaultLatched`, after which this returns false.
inline bool shouldShowFault(const UIState& s) { return s.timingFaultLatched; }

// Explicit-Play fault acknowledgement (task 11.3, Req 10.6). When the performer
// presses Play while a fault is latched, the UI clears the fault latch and its
// cause. Returns true if a fault WAS latched and is now cleared (so the caller
// knows this Play was consumed as an acknowledgement and the sequencer should
// restart runtime positions / start a new timeline — the core1 side of this is
// Sequencer::play()'s fault.acknowledge(), task 5.3). A no-op (returns false)
// when no fault is latched, so an ordinary Play is unaffected.
inline bool acknowledgeFaultOnPlay(UIState& s) {
    if (!s.timingFaultLatched) return false;
    s.timingFaultLatched = false;
    s.timingFaultCause = 0;
    return true;
}

// The color the matrix fills with for the current feedback state, fault first.
// Returns 0 (no overlay) when neither a fault nor a save banner is active.
inline uint32_t matrixFeedbackColor(const UIState& s) {
    if (s.timingFaultLatched) return kFaultColor;  // fault wins (Req 10.4)
    switch (s.saveBanner) {
        case SaveBanner::SUCCESS:    return kSaveSuccessColor;
        case SaveBanner::FAILURE:    return kSaveFailureColor;
        case SaveBanner::NOT_LOADED: return kLoadFailureColor;
        case SaveBanner::NONE:
        default:                     return 0;
    }
}

// The four feedback colors are mutually distinct — a compile-time guarantee that
// the fault presentation can never be confused with a save/load one (Req 10.4).
static_assert(kFaultColor != kSaveSuccessColor && kFaultColor != kSaveFailureColor &&
                  kFaultColor != kLoadFailureColor &&
                  kSaveSuccessColor != kSaveFailureColor &&
                  kSaveSuccessColor != kLoadFailureColor &&
                  kSaveFailureColor != kLoadFailureColor,
              "matrix feedback colors must be mutually distinct (Req 10.4)");

}  // namespace ui::state
