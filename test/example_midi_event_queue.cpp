// performance-safe-foundation task 4.2 — fixed-capacity MIDI event queue.
//
// Validates Requirements 4.1, 4.2, 4.5, 4.7 against sequencer::MidiEventQueueT:
//   - 4.1: deterministic coincident ordering. Events sharing an intended
//     timestamp drain in the documented total order (class priority desc, then
//     canonical PatternId asc, then enqueue sequence) regardless of insertion
//     order.
//   - 4.2: real-time Clock/Stop priority and interleaving — a real-time event at
//     the same timestamp drains ahead of channel-voice note events.
//   - 4.5: fixed capacity, no growth. Enqueue past capacity is refused, never
//     allocated around.
//   - 4.7: reserved safety capacity. A flood of Note On events cannot fill the
//     reserve, so a Note Off / Stop / panic can always be staged; once even the
//     reserve is exhausted a safety event reports DirectRequired (bounded direct
//     path) instead of being dropped.
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "MidiEventQueue.h"
#include "midi_messages.h"
#include "../common/PatternId.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace midi = sequencer::midi;
using sequencer::MidiEvent;
using sequencer::MidiEventClass;
using sequencer::EnqueueResult;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

common::PatternId pid(uint8_t slot, uint32_t gen) { return common::PatternId(slot, gen); }

// Drain the whole queue into a flat byte stream in priority order.
template <typename Q>
std::vector<uint8_t> drainBytes(Q& q) {
    std::vector<uint8_t> out;
    MidiEvent e;
    while (q.dequeue(e)) {
        for (uint8_t i = 0; i < e.length; ++i) out.push_back(e.bytes[i]);
    }
    return out;
}

// 4.1 / 4.2: coincident ordering is deterministic and independent of insertion
// order; real-time outranks Note Off outranks Note On; PatternId breaks ties.
void test_coincident_ordering() {
    sequencer::MidiEventQueueT<16, 4> q;
    const uint64_t t = 1000;

    // Enqueue in a deliberately "wrong" order: an ordinary Note On from a high
    // PatternId first, then a Note Off, then a real-time Clock, then a Note On
    // from a low PatternId — all at the SAME timestamp.
    q.enqueue(MidiEvent::noteOn(pid(5, 1), midi::ChannelVoiceMessage::NOTE_ON, 72, 100, t));
    q.enqueue(MidiEvent::noteOff(pid(2, 1), midi::ChannelVoiceMessage::NOTE_OFF, 60, t));
    q.enqueue(MidiEvent::realTime(midi::SystemRealTimeMessage::TIMING_CLOCK, t));
    q.enqueue(MidiEvent::noteOn(pid(1, 1), midi::ChannelVoiceMessage::NOTE_ON, 64, 100, t));

    MidiEvent e;
    check(q.dequeue(e) && e.cls == MidiEventClass::RealTime &&
          e.bytes[0] == midi::SystemRealTimeMessage::TIMING_CLOCK,
          "coincident: real-time Clock drains first");
    check(q.dequeue(e) && e.cls == MidiEventClass::NoteOff && e.bytes[1] == 60,
          "coincident: Note Off (safety) drains before Note On");
    check(q.dequeue(e) && e.cls == MidiEventClass::NoteOn && e.bytes[1] == 64,
          "coincident: Note On from lower PatternId (1) before higher (5)");
    check(q.dequeue(e) && e.cls == MidiEventClass::NoteOn && e.bytes[1] == 72,
          "coincident: Note On from higher PatternId (5) last");
    check(!q.dequeue(e), "queue empty after draining all coincident events");
}

// 4.1: earlier intended timestamp always drains before a later one, even when a
// later-timestamp real-time event was enqueued first.
void test_timestamp_ordering() {
    sequencer::MidiEventQueueT<16, 4> q;
    q.enqueue(MidiEvent::realTime(midi::SystemRealTimeMessage::TIMING_CLOCK, 2000));
    q.enqueue(MidiEvent::noteOn(pid(1, 1), midi::ChannelVoiceMessage::NOTE_ON, 60, 100, 1000));

    MidiEvent e;
    check(q.dequeue(e) && e.intendedMicros == 1000 && e.cls == MidiEventClass::NoteOn,
          "earlier timestamp drains first even vs an earlier-enqueued real-time event");
    check(q.dequeue(e) && e.intendedMicros == 2000, "later timestamp drains second");
}

// 4.5 / 4.7: fixed capacity with a reserved safety slice. Ordinary Note On
// events are refused once only the reserve remains; safety events may use the
// reserve; once even that is gone a safety event reports DirectRequired.
void test_fixed_capacity_and_safety_reserve() {
    sequencer::MidiEventQueueT<8, 3> q;  // 8 slots, 3 reserved for safety
    check(q.capacity() == 8 && q.safetyReserve() == 3, "capacity/reserve reported");

    // Fill the 5 ordinary (non-reserved) slots with Note On events.
    for (int i = 0; i < 5; ++i) {
        const EnqueueResult r =
            q.enqueue(MidiEvent::noteOn(pid(1, 1), midi::ChannelVoiceMessage::NOTE_ON,
                                        60, 100, 1000 + i));
        check(r == EnqueueResult::Queued, "ordinary Note On accepted up to capacity-reserve");
    }
    check(q.size() == 5, "5 ordinary slots used");

    // The 6th ordinary Note On must be refused — only the reserve remains.
    check(q.enqueue(MidiEvent::noteOn(pid(1, 1), midi::ChannelVoiceMessage::NOTE_ON,
                                      61, 100, 2000)) == EnqueueResult::RejectedFull,
          "ordinary Note On refused once only the safety reserve remains (4.7)");

    // Safety events (Note Off / Stop) may still consume the reserved slots.
    check(q.enqueue(MidiEvent::noteOff(pid(1, 1), midi::ChannelVoiceMessage::NOTE_OFF,
                                       60, 2000)) == EnqueueResult::Queued,
          "safety Note Off uses a reserved slot");
    check(q.enqueue(MidiEvent::realTime(midi::SystemRealTimeMessage::STOP, 2000)) ==
          EnqueueResult::Queued, "safety Stop uses a reserved slot");
    check(q.enqueue(MidiEvent::noteOff(pid(2, 1), midi::ChannelVoiceMessage::NOTE_OFF,
                                       62, 2000)) == EnqueueResult::Queued,
          "safety fills the last reserved slot");
    check(q.size() == 8, "queue now full (5 ordinary + 3 safety)");

    // Even a safety event cannot be staged now — report the bounded direct path.
    check(q.enqueue(MidiEvent::realTime(midi::SystemRealTimeMessage::STOP, 3000)) ==
          EnqueueResult::DirectRequired,
          "safety event reports DirectRequired when the queue is completely full (4.7)");

    // Fixed capacity: it never grew beyond 8.
    check(q.size() == 8, "fixed capacity: size never exceeds capacity (4.5)");
}

// Backlog high-water metric tracks the peak occupancy (Req 4.4 support).
void test_high_water() {
    sequencer::MidiEventQueueT<16, 4> q;
    for (int i = 0; i < 6; ++i)
        q.enqueue(MidiEvent::noteOn(pid(1, 1), midi::ChannelVoiceMessage::NOTE_ON,
                                    60, 100, 1000 + i));
    check(q.highWater() == 6, "high-water reaches peak depth");
    MidiEvent e;
    q.dequeue(e); q.dequeue(e);
    check(q.size() == 4 && q.highWater() == 6, "high-water retained after drain");
}

}  // namespace

int main() {
    test_coincident_ordering();
    test_timestamp_ordering();
    test_fixed_capacity_and_safety_reserve();
    test_high_water();

    if (failures == 0) {
        std::printf("example_midi_event_queue: all checks passed\n");
        return 0;
    }
    std::printf("example_midi_event_queue: %d check(s) failed\n", failures);
    return 1;
}
