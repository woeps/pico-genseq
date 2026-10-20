#pragma once

#include <cstdint>

// Output backlog accounting and overload detection (performance-safe-foundation
// task 4.3, Requirements 4.4, 4.5, 4.6).
//
// The MidiEventQueue (task 4.2) is bounded, so demand beyond the 31,250-baud DIN
// wire cannot grow an unbounded delayed queue (Req 4.5) — the queue refuses
// ordinary events once full. This component turns that bounded backlog into a
// FAULT SIGNAL: it tracks the current and maximum backlog (Req 4.4) and declares
// a sustained overload when the backlog stays non-empty for ONE BEAT of
// continuous monotonic time (Req 4.6). The caller (the scheduler/sequencer) then
// enters the latched Timing Fault.
//
// "Sustained for one beat" is measured in monotonic microseconds, not tick
// counts, so it is independent of how often the output layer is serviced:
//   - the FIRST observation with a non-empty backlog starts an overload window;
//   - any later observation with an EMPTY backlog clears the window (the wire
//     caught up — not sustained);
//   - once a non-empty backlog has persisted for >= one beat of microseconds,
//     overloaded() latches true until reset().
//
// SDK-free: integer microseconds only. The one-beat duration is supplied by the
// caller from the TransportTimeline (ticksPerBeat * tick duration) so the
// detector itself holds no musical state.

namespace sequencer {

class OverloadDetector {
public:
    // Record one backlog observation at monotonic time `nowMicros`. `backlog`
    // is the queue's current depth; `oneBeatMicros` is the current musical beat
    // duration in microseconds (0 disables the time test — no fault).
    // Returns true once (and every call thereafter) the backlog has been
    // continuously non-empty for at least one beat.
    bool observe(uint32_t backlog, uint64_t nowMicros, uint64_t oneBeatMicros) {
        if (backlog > maxBacklog_) maxBacklog_ = backlog;
        currentBacklog_ = backlog;

        if (backlog == 0) {
            // Wire caught up — the overload window (if any) is broken.
            windowOpen_ = false;
            return overloaded_;
        }

        if (!windowOpen_) {
            windowOpen_ = true;
            windowStartMicros_ = nowMicros;
            return overloaded_;
        }

        // Backlog has been non-empty since windowStartMicros_. Latch a fault once
        // that has lasted a whole beat (Req 4.6).
        if (oneBeatMicros > 0 && (nowMicros - windowStartMicros_) >= oneBeatMicros) {
            overloaded_ = true;
        }
        return overloaded_;
    }

    bool overloaded() const { return overloaded_; }
    uint32_t currentBacklog() const { return currentBacklog_; }
    uint32_t maxBacklog() const { return maxBacklog_; }

    // Clear the latch and the window (e.g. after an explicit Play acknowledges a
    // fault). maxBacklog is retained for diagnostics unless resetMetrics() is
    // also called.
    void reset() {
        overloaded_ = false;
        windowOpen_ = false;
        windowStartMicros_ = 0;
    }
    void resetMetrics() { maxBacklog_ = currentBacklog_; }

private:
    bool overloaded_ = false;
    bool windowOpen_ = false;
    uint64_t windowStartMicros_ = 0;
    uint32_t currentBacklog_ = 0;
    uint32_t maxBacklog_ = 0;
};

}  // namespace sequencer
