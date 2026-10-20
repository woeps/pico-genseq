#pragma once

#include <cstdint>

#include "../common/PatternId.h"

// Fixed-capacity MIDI event queue (performance-safe-foundation task 4.2,
// Requirements 4.1, 4.2, 4.5, 4.7).
//
// The sequencer produces MIDI events faster, at a coincident timestamp, than a
// single 31,250-baud DIN wire can serialize them. This queue is the bounded
// staging area between event production and the UART. Its guarantees:
//
//   - FIXED CAPACITY (Req 4.5): a compile-time ring buffer. It never grows and
//     never heap-allocates, so a dense edit/pattern load cannot create an
//     unbounded delayed-event backlog.
//
//   - DETERMINISTIC COINCIDENT ORDERING (Req 4.1): events are drained in a total
//     order that does NOT depend on insertion race timing. The order key is
//     (intended timestamp, class priority, PatternId, enqueue sequence):
//       1. earlier intended timestamp first;
//       2. at the same timestamp, higher class priority first — real-time
//          (Clock/Start/Stop) outranks a safety release (Note Off), which
//          outranks an ordinary Note On (Req 4.2 real-time priority; releases
//          ahead of new notes so a key is freed before it is re-struck);
//       3. same timestamp and class: lower canonical PatternId first (a stable,
//          reproducible tiebreak independent of arrival order);
//       4. final tiebreak: enqueue sequence, so equal keys keep FIFO order.
//
//   - RESERVED SAFETY CAPACITY (Req 4.7): a slice of the capacity is reserved so
//     a flood of Note On events can never fill the queue to the point where a
//     Note Off, Stop, or panic cannot be staged. Ordinary events are rejected
//     once only the reserved slots remain; safety events may use every slot and,
//     if even those are exhausted, report that the caller must take the bounded
//     DIRECT path (emit immediately) rather than drop a release.
//
//   - BACKLOG METRICS (Req 4.4 support): current depth and high-water mark are
//     exposed for the validation-build instrumentation.
//
// This header is SDK-free (only <cstdint> and the canonical PatternId type) so it
// compiles unchanged in firmware and host tests. Sustained-overload fault
// detection (Req 4.6) and the UART drain live in later tasks (4.3 / 4.4); this
// component owns only bounded staging and deterministic ordering.

namespace sequencer {

// Event class, ordered by drain priority at a coincident timestamp. Higher
// numeric value drains first. Real-time transport bytes are single-byte MIDI
// real-time messages that may be legally interleaved and must not wait behind a
// channel message (Req 4.2). A release (Note Off) is safety-critical and drains
// ahead of a new Note On so a wire key is freed before it is re-struck.
enum class MidiEventClass : uint8_t {
    NoteOn = 0,     // ordinary new note — lowest priority
    NoteOff = 1,    // safety release
    RealTime = 2,   // Clock / Start / Stop / panic transport — highest priority
};

// A compact, fixed-size MIDI event. Up to three bytes covers every message this
// milestone emits (Note On/Off triples and single-byte real-time messages).
struct MidiEvent {
    uint64_t intendedMicros = 0;       // intended dispatch timestamp
    common::PatternId source{};        // owning pattern (invalid for transport)
    MidiEventClass cls = MidiEventClass::NoteOn;
    uint8_t length = 0;                // 1..3 valid bytes
    uint8_t bytes[3] = {0, 0, 0};

    static MidiEvent realTime(uint8_t statusByte, uint64_t micros) {
        MidiEvent e;
        e.cls = MidiEventClass::RealTime;
        e.intendedMicros = micros;
        e.length = 1;
        e.bytes[0] = statusByte;
        return e;
    }
    static MidiEvent noteOn(common::PatternId src, uint8_t status, uint8_t note,
                            uint8_t velocity, uint64_t micros) {
        MidiEvent e;
        e.cls = MidiEventClass::NoteOn;
        e.source = src;
        e.intendedMicros = micros;
        e.length = 3;
        e.bytes[0] = status;
        e.bytes[1] = note;
        e.bytes[2] = velocity;
        return e;
    }
    static MidiEvent noteOff(common::PatternId src, uint8_t status, uint8_t note,
                             uint64_t micros) {
        MidiEvent e;
        e.cls = MidiEventClass::NoteOff;
        e.source = src;
        e.intendedMicros = micros;
        e.length = 3;
        e.bytes[0] = status;
        e.bytes[1] = note;
        e.bytes[2] = 0;
        return e;
    }
};

// Result of trying to stage an event.
enum class EnqueueResult : uint8_t {
    Queued,          // accepted into the ring
    RejectedFull,    // ordinary event dropped: only reserved safety slots remain
    DirectRequired,  // safety event could not be staged; caller must emit directly
};

template <uint16_t Capacity, uint16_t SafetyReserve>
class MidiEventQueueT {
    static_assert(Capacity > SafetyReserve,
                  "capacity must exceed the reserved safety slice");

public:
    // Is `cls` a safety event allowed to consume the reserved slots?
    static constexpr bool isSafety(MidiEventClass cls) {
        return cls == MidiEventClass::NoteOff || cls == MidiEventClass::RealTime;
    }

    // Stage one event. Ordinary (Note On) events are refused once only the
    // reserved safety slots remain free (Req 4.7). Safety events (Note Off /
    // real-time Stop / panic) may use every slot; when even those are gone the
    // queue reports DirectRequired so the caller takes the bounded direct path
    // rather than dropping a release.
    EnqueueResult enqueue(const MidiEvent& ev) {
        const uint16_t used = size();
        if (isSafety(ev.cls)) {
            if (used >= Capacity) return EnqueueResult::DirectRequired;
        } else {
            if (used >= static_cast<uint16_t>(Capacity - SafetyReserve)) {
                return EnqueueResult::RejectedFull;
            }
        }
        slots_[tail_] = ev;
        seq_[tail_] = nextSeq_++;
        tail_ = static_cast<uint16_t>((tail_ + 1) % RingSize);
        ++count_;
        if (count_ > highWater_) highWater_ = count_;
        return EnqueueResult::Queued;
    }

    // Remove and return the highest-priority event per the documented total
    // order. Returns false (and leaves `out` untouched) when empty. O(n) scan of
    // a fixed ring — bounded by Capacity, so the per-drain cost is bounded.
    bool dequeue(MidiEvent& out) {
        if (count_ == 0) return false;
        uint16_t bestRing = RingSize;  // sentinel
        for (uint16_t k = 0; k < count_; ++k) {
            const uint16_t ring = static_cast<uint16_t>((head_ + k) % RingSize);
            if (bestRing == RingSize || less(slots_[ring], seq_[ring],
                                             slots_[bestRing], seq_[bestRing])) {
                bestRing = ring;
            }
        }
        out = slots_[bestRing];
        // Compact: shift the ring entries after bestRing down by one so the
        // buffer stays contiguous between head_ and tail_.
        removeAt(bestRing);
        return true;
    }

    uint16_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    static constexpr uint16_t capacity() { return Capacity; }
    static constexpr uint16_t safetyReserve() { return SafetyReserve; }
    uint16_t highWater() const { return highWater_; }
    void resetHighWater() { highWater_ = count_; }

private:
    // One extra slot keeps head_/tail_ distinguishable at full occupancy without
    // a separate flag; `count_` is the authoritative occupancy.
    static constexpr uint16_t RingSize = Capacity + 1;

    // Strict-weak ordering: is event A (seq sa) ordered BEFORE event B (seq sb)?
    // (timestamp asc, class priority desc, PatternId asc, enqueue seq asc).
    static bool less(const MidiEvent& a, uint32_t sa, const MidiEvent& b, uint32_t sb) {
        if (a.intendedMicros != b.intendedMicros)
            return a.intendedMicros < b.intendedMicros;
        if (a.cls != b.cls)
            return static_cast<uint8_t>(a.cls) > static_cast<uint8_t>(b.cls);
        // Lower canonical PatternId first; a transport event has an invalid id
        // (index 0, generation 0) and already sorts by class above.
        if (a.source.index != b.source.index) return a.source.index < b.source.index;
        if (a.source.generation != b.source.generation)
            return a.source.generation < b.source.generation;
        return sa < sb;  // stable FIFO for otherwise-equal keys
    }

    void removeAt(uint16_t ring) {
        // Shift every entry after `ring` (in logical order) one position toward
        // head, then retract the tail. Bounded by count_.
        uint16_t cur = ring;
        while (true) {
            const uint16_t nxt = static_cast<uint16_t>((cur + 1) % RingSize);
            if (nxt == tail_) break;
            slots_[cur] = slots_[nxt];
            seq_[cur] = seq_[nxt];
            cur = nxt;
        }
        tail_ = static_cast<uint16_t>((tail_ + RingSize - 1) % RingSize);
        --count_;
    }

    MidiEvent slots_[RingSize];
    uint32_t seq_[RingSize] = {0};
    uint16_t head_ = 0;
    uint16_t tail_ = 0;
    uint16_t count_ = 0;
    uint16_t highWater_ = 0;
    uint32_t nextSeq_ = 0;
};

// Default production sizing: 64 events with 8 reserved for safety traffic. These
// bounds are documented and host-tested; the hardware validation (task 12.3)
// confirms the high-water mark stays within them under worst-case load.
using MidiEventQueue = MidiEventQueueT<64, 8>;

}  // namespace sequencer
