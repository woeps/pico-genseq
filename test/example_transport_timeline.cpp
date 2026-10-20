// performance-safe-foundation task 3.2 — TransportTimeline.
//
// Validates Requirements 1.1-1.5:
//   - start() establishes one monotonic timeline from a given timestamp (1.1);
//   - beat and bar indices/boundaries derive from the tick index and meter (1.2, 1.3);
//   - the long-run tick timestamp stays drift-free because the division remainder is carried
//     (1.5), checked against an exact rational reference over many ticks and across BPMs;
//   - a mid-run BPM change preserves the tick index and current phase (does not restart), and
//     subsequent tick durations use the new BPM (1.5);
//   - an unsupported meter is rejected, leaving the timeline unchanged (1.3/1.7).
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "sequencer/TransportTimeline.h"
#include "common/TimeSignature.h"

#include <cstdint>
#include <cstdio>

using common::TimeSignature;
using sequencer::TransportTimeline;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr uint32_t PPQN = TransportTimeline::PPQN;  // 24

// start() establishes tick 0 at the given timestamp with zero indices.
void test_start_establishes_timeline() {
    TransportTimeline t;
    t.setBpm(120);
    t.start(1000);
    check(t.running(), "running after start");
    check(t.tickIndex() == 0, "tick index 0 at start");
    check(t.currentTickMicros() == 1000, "tick 0 intended at start timestamp");
    check(t.beatIndex() == 0 && t.barIndex() == 0, "beat/bar 0 at start");
    check(t.onBeatBoundary() && t.onBarBoundary(), "tick 0 is on a beat and bar boundary");
}

// Beat and bar boundaries land on the right tick indices for several meters.
void test_beat_bar_boundaries() {
    TransportTimeline t;
    t.setBpm(120);

    check(t.setTimeSignature(TimeSignature(4, 4)), "4/4 accepted");
    t.start(0);
    check(t.ticksPerBeat() == 24 && t.ticksPerBar() == 96, "4/4: 24/beat, 96/bar");
    for (int i = 0; i < 96; ++i) t.advance();  // one bar
    check(t.tickIndex() == 96, "advanced one bar of ticks");
    check(t.beatIndex() == 4, "beat index 4 after a 4/4 bar");
    check(t.barIndex() == 1, "bar index 1 after a 4/4 bar");
    check(t.onBeatBoundary() && t.onBarBoundary(), "bar end is a beat and bar boundary");

    check(t.setTimeSignature(TimeSignature(6, 8)), "6/8 accepted");
    t.start(0);
    check(t.ticksPerBeat() == 12 && t.ticksPerBar() == 72, "6/8: 12/beat, 72/bar");
    for (int i = 0; i < 12; ++i) t.advance();
    check(t.beatIndex() == 1 && t.onBeatBoundary(), "6/8: one beat after 12 ticks");
    check(!t.onBarBoundary(), "6/8: not a bar boundary mid-bar");
}

// Long-run drift-free timestamps: the accumulated tick time must equal the exact rational
// value floor(n * 60e6 / (bpm*PPQN)) within <= 1 us at every tick (the carried remainder makes
// the running total exact at tick boundaries). Checked across awkward BPMs where the per-tick
// duration is NOT integral.
void test_no_drift_across_bpms() {
    const uint16_t bpms[] = {40, 120, 255, 133, 97};
    for (uint16_t bpm : bpms) {
        TransportTimeline t;
        t.setBpm(bpm);
        t.start(0);
        const uint64_t denom = static_cast<uint64_t>(bpm) * PPQN;
        const int ticks = 100000;  // long run
        for (int n = 1; n <= ticks; ++n) {
            t.advance();
            // Exact rational reference: tick n is intended at floor(n * 60e6 / (bpm*PPQN)).
            const uint64_t expected = (static_cast<uint64_t>(n) * 60ULL * 1000000ULL) / denom;
            if (t.currentTickMicros() != expected) {
                std::printf("  drift at bpm=%u tick=%d: got %llu expected %llu\n",
                            bpm, n, (unsigned long long)t.currentTickMicros(),
                            (unsigned long long)expected);
                check(false, "tick timestamp matches exact rational reference (no drift)");
                break;
            }
        }
    }
    check(true, "no-drift sweep completed");
}

// A mid-run BPM change preserves the tick index and current intended timestamp (phase), and
// subsequent durations use the new BPM — it does NOT restart the timeline (Req 1.5).
void test_bpm_change_preserves_phase() {
    TransportTimeline t;
    t.setBpm(120);
    t.start(0);
    for (int i = 0; i < 50; ++i) t.advance();
    const uint64_t tickBefore = t.tickIndex();
    const uint64_t microsBefore = t.currentTickMicros();

    check(t.setBpm(140), "BPM change accepted");
    // Phase preserved: index and current timestamp unchanged by the BPM change itself.
    check(t.tickIndex() == tickBefore, "tick index preserved across BPM change");
    check(t.currentTickMicros() == microsBefore, "current timestamp preserved across BPM change");

    // Next tick uses the NEW bpm duration (~ 60e6/(140*24) = 17857 us).
    const uint64_t durAt140 = t.currentTickDurationMicros();
    check(durAt140 == 60000000ULL / (140ULL * PPQN) ||
          durAt140 == 60000000ULL / (140ULL * PPQN) + 1,
          "next tick duration reflects new BPM");
    t.advance();
    check(t.tickIndex() == tickBefore + 1, "advance continues from preserved index");
}

// Zero BPM and unsupported meter are rejected without mutating state.
void test_rejections() {
    TransportTimeline t;
    t.setBpm(120);
    check(!t.setBpm(0), "BPM 0 rejected");
    check(t.bpm() == 120, "BPM unchanged after rejected set");

    check(t.setTimeSignature(TimeSignature(4, 4)), "4/4 accepted");
    check(!t.setTimeSignature(TimeSignature(4, 64)), "4/64 rejected");
    check(t.timeSignature() == TimeSignature(4, 4), "meter unchanged after rejected set");
}

}  // namespace

int main() {
    test_start_establishes_timeline();
    test_beat_bar_boundaries();
    test_no_drift_across_bpms();
    test_bpm_change_preserves_phase();
    test_rejections();

    if (failures == 0) {
        std::printf("example_transport_timeline: all checks passed\n");
        return 0;
    }
    std::printf("example_transport_timeline: %d check(s) failed\n", failures);
    return 1;
}
