#pragma once

#include <cstdint>

#include "../common/Scale.h"
#include "../common/TimeSignature.h"

// Global Transport Timeline (performance-safe-foundation task 3.2,
// Requirements 1.1-1.5).
//
// One monotonic musical timeline shared by every Pattern. It owns:
//   - BPM and PPQN;
//   - the active Time Signature (meter);
//   - the absolute logical tick index since the current Play;
//   - the intended monotonic timestamp of each tick;
//   - derived beat and bar indices;
//   - the global scale (root + template index), stored in this ONE place —
//     the single source every pattern reads its scale constraint from
//     (ui-integration T1b/T2: FR12, FR13, FR14, AC10, AC11).
//
// Phase preservation across BPM changes (Req 1.5): the duration of one tick in
// microseconds is `60e6 / (bpm * PPQN)`, which is rarely integral. Rather than
// discard the fractional microsecond each tick (which would accumulate drift),
// the timeline carries the remainder in fixed-point and folds it into the next
// tick. A BPM change takes effect from the NEXT tick onward; the already-chosen
// intended timestamp of the current tick and the logical tick index are
// preserved, so the musical phase does not jump and pattern positions are not
// restarted.
//
// SDK-free: integer microseconds only, no Pico SDK types.

namespace sequencer {

class TransportTimeline {
public:
    static constexpr uint32_t PPQN = common::time_signature::PPQN_VALUE;  // 24

    TransportTimeline() = default;

    // Configure meter. Returns false and leaves the timeline unchanged for an
    // unsupported signature (Req 1.3; rejection, not rounding — Req 1.7).
    bool setTimeSignature(common::TimeSignature ts) {
        if (!common::time_signature::isSupported(ts)) return false;
        meter_ = ts;
        return true;
    }
    common::TimeSignature timeSignature() const { return meter_; }

    // Set BPM. During playback the change applies from the next tick and the
    // current phase/tick index are preserved (Req 1.5). Returns false for a
    // zero/implausible BPM.
    bool setBpm(uint16_t bpm) {
        if (bpm == 0) return false;
        bpm_ = bpm;
        return true;
    }
    uint16_t bpm() const { return bpm_; }

    // Begin a new timeline at `startMicros` (Req 1.1, 1.6). Tick 0 is intended
    // exactly at startMicros; subsequent intended timestamps accumulate from it.
    void start(uint64_t startMicros) {
        running_ = true;
        tickIndex_ = 0;
        currentTickMicros_ = startMicros;
        remainderNum_ = 0;
    }

    void stop() { running_ = false; }
    bool running() const { return running_; }

    // --- Current logical position --------------------------------------------
    uint64_t tickIndex() const { return tickIndex_; }
    uint64_t currentTickMicros() const { return currentTickMicros_; }

    uint32_t ticksPerBeat() const { return common::time_signature::ticksPerBeat(meter_); }
    uint32_t ticksPerBar() const { return common::time_signature::ticksPerBar(meter_); }

    // Beat/bar indices derived from the tick index and meter (Req 1.2).
    uint64_t beatIndex() const {
        const uint32_t tpb = ticksPerBeat();
        return tpb ? tickIndex_ / tpb : 0;
    }
    uint64_t barIndex() const {
        const uint32_t tpBar = ticksPerBar();
        return tpBar ? tickIndex_ / tpBar : 0;
    }
    bool onBeatBoundary() const {
        const uint32_t tpb = ticksPerBeat();
        return tpb && (tickIndex_ % tpb) == 0;
    }
    bool onBarBoundary() const {
        const uint32_t tpBar = ticksPerBar();
        return tpBar && (tickIndex_ % tpBar) == 0;
    }

    // Microsecond duration of the current tick at the active BPM, using the
    // carried remainder so no fractional-microsecond drift accumulates (Req 1.5).
    // Advancing uses this value and folds the leftover into remainderNum_.
    uint64_t currentTickDurationMicros() const {
        // 60e6 / (bpm * PPQN) microseconds per tick, exact numerator preserved.
        const uint64_t numerator = 60ULL * 1000000ULL + remainderNum_;
        const uint64_t denom = static_cast<uint64_t>(bpm_) * PPQN;
        return numerator / denom;
    }

    // Advance to the next tick: bump the tick index and set the next intended
    // timestamp. The division remainder is carried so the long-run average tick
    // duration is exact and the phase is preserved across BPM changes.
    void advance() {
        const uint64_t numerator = 60ULL * 1000000ULL + remainderNum_;
        const uint64_t denom = static_cast<uint64_t>(bpm_) * PPQN;
        const uint64_t dur = numerator / denom;
        remainderNum_ = numerator % denom;  // carry the fractional microsecond
        currentTickMicros_ += dur;
        ++tickIndex_;
    }

    // Intended timestamp of logical tick `index` is not stored for every tick;
    // the scheduler advances one tick at a time via advance(). This helper
    // reports the intended timestamp of the NEXT tick for scheduling decisions.
    uint64_t nextTickMicros() const {
        return currentTickMicros_ + currentTickDurationMicros();
    }

    // --- Global scale (ui-integration T1b/T2: FR12, FR13, FR14, AC10, AC11) --
    // The global scale constraint, stored in ONE place (the transport
    // timeline) and read by every pattern from this single source. A Scale
    // pairs a root pitch class (0-11) with an index into the fixed built-in
    // template table (common::SCALE_TEMPLATES) and constrains pitch
    // generation and pitch mutation so every emitted note satisfies the
    // pattern's selected scale (FR4, AC2). The scale is a TRUE GLOBAL
    // setting stored in a single source (TransportTimeline) and modulatable
    // with Global scope alongside BPM (AC10/AC11); Scale root itself shall
    // not be modulated in M3 (pitch transpose provides the continuous
    // target while the saved root stays stable).
    uint8_t scaleRoot() const { return scaleRoot_; }
    uint8_t scaleTemplateIndex() const { return scaleTemplateIndex_; }
    // Set the global scale. Rejection, not rounding (Req 1.7): an
    // out-of-range root (not a pitch class) or a template index naming no
    // built-in template returns false and leaves the timeline unchanged.
    bool setScale(uint8_t root, uint8_t templateIndex) {
        if (root > 11) return false;  // root is a pitch class: 0-11 only
        if (templateIndex >= static_cast<uint8_t>(common::scaleTemplateCount())) {
            return false;  // must name a real built-in template
        }
        scaleRoot_ = static_cast<uint8_t>(root % 12);
        scaleTemplateIndex_ = templateIndex;
        scale_ = common::Scale(scaleRoot_, scaleTemplateIndex_);
        return true;
    }

    // The pattern's global scale constraint as the value type the generator
    // context carries (root + template index, normalized by common::Scale so
    // a Scale is always valid, whatever data flows in — deterministic, never
    // out of bounds).
    common::Scale scale() const { return scale_; }

private:
    // --- Global scale (ui-integration T1b/T2: FR12, FR13, FR14) -------------
    // The global scale constraint: root pitch class (0-11) and index into
    // the built-in template table, plus the derived common::Scale value.
    // A TRUE GLOBAL setting stored in a single source (TransportTimeline).
    uint8_t scaleRoot_ = 0;           // root pitch class 0-11 (default C)
    uint8_t scaleTemplateIndex_ = 0;  // index into common::SCALE_TEMPLATES
    common::Scale scale_{};           // the derived scale constraint value

    common::TimeSignature meter_{};  // 4/4 default
    uint16_t bpm_ = 120;
    bool running_ = false;
    uint64_t tickIndex_ = 0;
    uint64_t currentTickMicros_ = 0;
    uint64_t remainderNum_ = 0;  // carried fractional-microsecond numerator
};

}  // namespace sequencer
