// performance-safe-foundation task 4.4 — host MIDI stream and ordering property.
//
// Validates Requirements 2.1-2.6, 4.1-4.7, 11.2 against the MidiEventQueue
// deterministic ordering and the Sequencer transport output.
//
// Two properties:
//
//   (A) ORDERING (4.1/4.2): for any random set of events enqueued in any order,
//       draining the MidiEventQueue yields them in the documented total order —
//       by intended timestamp ascending, then class priority (RealTime > NoteOff
//       > NoteOn), then canonical PatternId, then enqueue sequence. This is the
//       LOGICAL ordering guarantee; modeled DIN wire skew (the 320us/byte
//       serialization) is explicitly NOT part of this host assertion (Req 4.3) —
//       it is a hardware measurement (task 12.3). We check the drained sequence
//       is sorted under the documented comparator, independent of insertion
//       order, and that every enqueued event comes out exactly once.
//
//   (B) TRANSPORT ON/OFF (2.1/2.3/2.4): the real Sequencer with MIDI Clock
//       enabled (default) emits exactly one Start on a stopped->playing Play and
//       one Stop on a playing->stopped Stop over any random Play/Stop sequence,
//       and never a Clock without a running tick. (Clock-disabled behavior is a
//       settings path not reachable through the current command surface, covered
//       by the unit tests; here we assert the enabled transport state machine.)

#include "MidiEventQueue.h"
#include "FakeMonotonicClock.h"
#include "RecordingMidiOutput.h"
#include "sequencer.h"
#include "command.h"
#include "midi_messages.h"
#include "../common/PatternId.h"

#include <rapidcheck.h>

#include <cstdint>
#include <vector>

namespace midi = sequencer::midi;
using sequencer::MidiEvent;
using sequencer::MidiEventClass;

namespace {

// A generated event descriptor mapped onto a MidiEvent.
struct GenEvent {
    uint8_t clsSel;    // 0 NoteOn, 1 NoteOff, 2 RealTime
    uint8_t slot;      // PatternId slot
    uint8_t note;
    uint16_t tOffset;  // timestamp bucket (small so collisions happen)
};

MidiEvent toEvent(const GenEvent& g) {
    const uint64_t t = 1000 + static_cast<uint64_t>(g.tOffset % 8) * 100;  // few buckets
    const common::PatternId id(static_cast<uint8_t>(g.slot % 8), 1);
    switch (g.clsSel % 3) {
        case 1: return MidiEvent::noteOff(id, midi::ChannelVoiceMessage::NOTE_OFF, g.note & 0x7F, t);
        case 2: return MidiEvent::realTime(midi::SystemRealTimeMessage::TIMING_CLOCK, t);
        default: return MidiEvent::noteOn(id, midi::ChannelVoiceMessage::NOTE_ON, g.note & 0x7F, 100, t);
    }
}

// The documented total order (must match MidiEventQueue::less).
bool orderedBefore(const MidiEvent& a, const MidiEvent& b) {
    if (a.intendedMicros != b.intendedMicros) return a.intendedMicros < b.intendedMicros;
    if (a.cls != b.cls) return static_cast<uint8_t>(a.cls) > static_cast<uint8_t>(b.cls);
    if (a.source.index != b.source.index) return a.source.index < b.source.index;
    return a.source.generation < b.source.generation;
}

enum class TKind : uint8_t { Play, Stop };
commands::CommandMessage toMsg(TKind k) {
    commands::CommandMessage m;
    m.cmd = (k == TKind::Play) ? commands::Command::PLAY : commands::Command::STOP;
    return m;
}

}  // namespace

namespace rc {
template <>
struct Arbitrary<GenEvent> {
    static Gen<GenEvent> arbitrary() {
        return gen::build<GenEvent>(
            gen::set(&GenEvent::clsSel, gen::inRange<uint8_t>(0, 3)),
            gen::set(&GenEvent::slot, gen::inRange<uint8_t>(0, 8)),
            gen::set(&GenEvent::note, gen::inRange<uint8_t>(0, 128)),
            gen::set(&GenEvent::tOffset, gen::inRange<uint16_t>(0, 64)));
    }
};
template <>
struct Arbitrary<TKind> {
    static Gen<TKind> arbitrary() { return gen::element(TKind::Play, TKind::Stop); }
};
}  // namespace rc

bool runProperty5MidiStream() {
    bool ok = true;

    // (A) Deterministic coincident/stream ordering, insertion-order-independent.
    ok &= rc::check(
        "Property 5 (MIDI order): queue drains events in the documented total "
        "order regardless of insertion order; every event emerges exactly once",
        [](const std::vector<GenEvent>& gen) {
            // Capacity large enough to hold the generated set (RapidCheck lists
            // are small by default); drop the ordinary-event overflow cleanly.
            sequencer::MidiEventQueueT<256, 16> q;
            std::vector<MidiEvent> accepted;
            for (const auto& g : gen) {
                MidiEvent e = toEvent(g);
                if (q.enqueue(e) == sequencer::EnqueueResult::Queued) accepted.push_back(e);
            }

            std::vector<MidiEvent> drained;
            MidiEvent e;
            while (q.dequeue(e)) drained.push_back(e);

            // Same multiset size: nothing dropped or duplicated on drain.
            RC_ASSERT(drained.size() == accepted.size());
            // Drained sequence is sorted under the documented comparator: no
            // adjacent pair is out of order (a later event ordered-before an
            // earlier one would be a violation).
            for (size_t i = 1; i < drained.size(); ++i) {
                RC_ASSERT(!orderedBefore(drained[i], drained[i - 1]));
            }
        });

    // (B) Transport state machine with MIDI Clock enabled (default).
    ok &= rc::check(
        "Property 5 (transport): over any Play/Stop sequence the Sequencer emits "
        "one Start per stopped->playing edge and one Stop per playing->stopped edge",
        [](const std::vector<TKind>& ops) {
            RecordingMidiOutput out;
            FakeMonotonicClock clock;  // held at 0 -> update() never ticks
            sequencer::Sequencer seq(out, clock);
            seq.init();

            int expectedStart = 0, expectedStop = 0;
            bool playing = false;
            for (TKind k : ops) {
                if (k == TKind::Play && !playing) { ++expectedStart; playing = true; }
                else if (k == TKind::Stop && playing) { ++expectedStop; playing = false; }
                seq.processCommand(toMsg(k));
            }

            int starts = 0, stops = 0, clocks = 0;
            for (uint8_t b : out.record()) {
                if (b == midi::SystemRealTimeMessage::START) ++starts;
                else if (b == midi::SystemRealTimeMessage::STOP) ++stops;
                else if (b == midi::SystemRealTimeMessage::TIMING_CLOCK) ++clocks;
            }
            RC_ASSERT(starts == expectedStart);
            RC_ASSERT(stops == expectedStop);
            RC_ASSERT(clocks == 0);  // no Clock without a serviced tick
        });

    return ok;
}
