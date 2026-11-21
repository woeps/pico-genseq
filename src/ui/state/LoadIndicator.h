#pragma once

#include <cstdint>

// MIDI output load indicator policy (help-me-brainstorm-an-additional, Requirement 11.8). Pure
// and SDK-free; UIController draws the result as one pixel at (15,4) in every view.

namespace ui::state {

enum class LoadLevel : uint8_t {
    OFF,    // normal
    AMBER,  // within the last 2 s a MIDI byte waited >= 10 ms in the output buffer
    RED,    // a timing fault is latched (until Play acknowledges it)
};

constexpr uint32_t kLoadAmberHoldMs = 2000;

// `lastLateMs` is core1's g_midiLateMs (0 = never late); `nowMs` is ms since boot.
constexpr LoadLevel loadLevel(bool faultLatched, uint32_t lastLateMs, uint32_t nowMs) {
    if (faultLatched) return LoadLevel::RED;
    if (lastLateMs != 0 && nowMs - lastLateMs < kLoadAmberHoldMs) return LoadLevel::AMBER;
    return LoadLevel::OFF;
}

// Pixel and colours (LedMatrix 0xXXGGRRBB).
constexpr uint8_t kLoadPixelX = 15;
constexpr uint8_t kLoadPixelY = 4;
constexpr uint32_t loadColor(LoadLevel level) {
    return level == LoadLevel::RED     ? 0x0000FF00u   // red
           : level == LoadLevel::AMBER ? 0x0080FF00u   // amber
                                       : 0u;
}

// Mirror core1's published timing fault into the UI state fields. Returns true when anything
// changed. A newly latched fault also marks the transport stopped (core1 already stopped it), so
// the next Space is a Play that acknowledges the fault (Requirement 11.7).
template <class State>
bool mirrorTimingFault(State& s, bool latched, uint8_t cause) {
    if (latched == s.timingFaultLatched && (!latched || cause == s.timingFaultCause)) return false;
    s.timingFaultLatched = latched;
    s.timingFaultCause = latched ? cause : 0;
    if (latched) s.playing = false;
    return true;
}

}  // namespace ui::state
