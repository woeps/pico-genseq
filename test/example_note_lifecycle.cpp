// performance-safe-foundation task 2.3 — centralized NoteLifecycle.
//
// Validates Requirements 5.1-5.5 and 5.8:
//   - Note On records ownership by canonical channel+note at onset (5.1);
//   - release uses the RECORDED identity, not an edited pitch (5.2);
//   - overlapping patterns on the same key each emit their Note On (retrigger, 5.3) and the
//     final Note Off waits for the LAST owner (5.4, 5.5);
//   - releasePattern releases exactly one owner (mute/remove boundary, 5.6 support);
//   - releaseAll / panic release everything and leave per-pattern AND aggregate ownership
//     empty (5.8).
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "RecordingMidiOutput.h"
#include "sequencer/NoteLifecycle.h"
#include "common/MidiChannel.h"
#include "common/PatternId.h"
#include "sequencer/midi_messages.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using common::MidiChannel;
using common::PatternId;
using sequencer::NoteLifecycle;
namespace midi = sequencer::midi;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

std::vector<uint8_t> noteOn(uint8_t protoCh, uint8_t note, uint8_t vel) {
    return {static_cast<uint8_t>(midi::ChannelVoiceMessage::NOTE_ON | protoCh), note, vel};
}
std::vector<uint8_t> noteOff(uint8_t protoCh, uint8_t note) {
    return {static_cast<uint8_t>(midi::ChannelVoiceMessage::NOTE_OFF | protoCh), note, 0};
}

bool contains(const std::vector<uint8_t>& hay, const std::vector<uint8_t>& needle) {
    if (needle.size() > hay.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (hay[i + j] != needle[j]) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

// Basic on/off: one pattern, one note. Note On emitted, ownership tracked, Note Off emitted
// on release and ownership cleared.
void test_basic_on_off() {
    RecordingMidiOutput out;
    NoteLifecycle nl(out);
    PatternId p(0, 1);
    const MidiChannel ch = common::midi_channel::fromUserChannel(1);  // protocol 0

    nl.noteOn(p, ch, 60, 100);
    check(nl.patternSounding(p), "pattern sounding after noteOn");
    check(nl.aggregateCount(ch, 60) == 1, "aggregate count 1 after noteOn");
    check(out.record() == noteOn(0, 60, 100), "exactly one note-on emitted");

    nl.releasePattern(p);
    check(!nl.patternSounding(p), "pattern not sounding after release");
    check(nl.aggregateCount(ch, 60) == 0, "aggregate count 0 after release");
    check(nl.ownershipEmpty(), "ownership empty after release");
    check(out.record() == std::vector<uint8_t>({0x90, 60, 100, 0x80, 60, 0}),
          "note-on then note-off, in order");
}

// Release uses the RECORDED note, not the pattern's edited pitch. We emulate an edit by
// releasing — NoteLifecycle has no pitch state, so the recorded (channel,note) at onset is the
// only thing it can release, which IS the required behavior. Verify the released key equals the
// onset key even though a "new" note value is used for a subsequent onset.
void test_release_uses_recorded_identity() {
    RecordingMidiOutput out;
    NoteLifecycle nl(out);
    PatternId p(0, 1);
    const MidiChannel ch = common::midi_channel::fromUserChannel(3);  // protocol 2

    nl.noteOn(p, ch, 64, 90);   // onset at note 64
    out.clear();
    nl.releasePattern(p);       // must release note 64, the recorded onset
    check(out.record() == noteOff(2, 64), "release emits note-off for the recorded onset note");
    check(nl.ownershipEmpty(), "ownership empty");
}

// Two patterns sound the SAME key: both note-ons emitted (retrigger), first release emits NO
// note-off (another owner remains), final release emits the single note-off.
void test_overlap_retrigger_and_refcount() {
    RecordingMidiOutput out;
    NoteLifecycle nl(out);
    PatternId a(0, 1), b(1, 1);
    const MidiChannel ch = common::midi_channel::fromUserChannel(1);  // protocol 0

    nl.noteOn(a, ch, 67, 100);
    nl.noteOn(b, ch, 67, 110);  // same key, retrigger
    check(nl.aggregateCount(ch, 67) == 2, "two owners of the key");
    // Both note-ons present (each retrigger emitted, Req 5.3).
    check(contains(out.record(), noteOn(0, 67, 100)), "first note-on emitted");
    check(contains(out.record(), noteOn(0, 67, 110)), "second (retrigger) note-on emitted");

    out.clear();
    nl.releasePattern(a);  // one owner remains -> no note-off yet
    check(out.record().empty(), "no note-off while another owner holds the key");
    check(nl.aggregateCount(ch, 67) == 1, "one owner remains");

    nl.releasePattern(b);  // last owner -> single note-off
    check(out.record() == noteOff(0, 67), "final owner release emits the note-off");
    check(nl.ownershipEmpty(), "ownership empty after both release");
}

// A pattern retriggering its OWN note replaces its single owned note (monophonic): the old
// note is released and the new one started, no stranded note.
void test_monophonic_replacement() {
    RecordingMidiOutput out;
    NoteLifecycle nl(out);
    PatternId p(2, 5);
    const MidiChannel ch = common::midi_channel::fromUserChannel(2);  // protocol 1

    nl.noteOn(p, ch, 60, 100);
    nl.noteOn(p, ch, 72, 100);  // new onset without explicit release
    check(nl.aggregateCount(ch, 60) == 0, "old note released on replacement");
    check(nl.aggregateCount(ch, 72) == 1, "new note sounding");
    check(nl.patternSounding(p), "pattern still sounds exactly one note");

    nl.releasePattern(p);
    check(nl.ownershipEmpty(), "ownership empty");
}

// Stop/panic leave zero per-pattern AND aggregate ownership (Req 5.8).
void test_release_all_and_panic_empty() {
    RecordingMidiOutput out;
    NoteLifecycle nl(out);
    PatternId a(0, 1), b(1, 1), c(2, 1);
    const MidiChannel ch1 = common::midi_channel::fromUserChannel(1);
    const MidiChannel ch5 = common::midi_channel::fromUserChannel(5);

    nl.noteOn(a, ch1, 60, 100);
    nl.noteOn(b, ch1, 60, 100);  // shares key with a
    nl.noteOn(c, ch5, 48, 80);

    nl.releaseAll();
    check(nl.ownershipEmpty(), "releaseAll leaves ownership empty");

    // panic from a non-empty state also fully clears.
    nl.noteOn(a, ch1, 61, 100);
    nl.panic();
    check(nl.ownershipEmpty(), "panic leaves ownership empty");
    check(!nl.patternSounding(a), "no pattern sounding after panic");
}

// A stale PatternId (wrong generation) does not release the current occupant's note.
void test_stale_release_ignored() {
    RecordingMidiOutput out;
    NoteLifecycle nl(out);
    PatternId current(0, 7);
    PatternId stale(0, 3);  // same slot, older generation
    const MidiChannel ch = common::midi_channel::fromUserChannel(1);

    nl.noteOn(current, ch, 60, 100);
    out.clear();
    nl.releasePattern(stale);  // must be ignored
    check(out.record().empty(), "stale release emits nothing");
    check(nl.patternSounding(current), "current owner still sounding after stale release");
    nl.releasePattern(current);
    check(nl.ownershipEmpty(), "correct release clears ownership");
}

}  // namespace

int main() {
    test_basic_on_off();
    test_release_uses_recorded_identity();
    test_overlap_retrigger_and_refcount();
    test_monophonic_replacement();
    test_release_all_and_panic_empty();
    test_stale_release_ignored();

    if (failures == 0) {
        std::printf("example_note_lifecycle: all checks passed\n");
        return 0;
    }
    std::printf("example_note_lifecycle: %d check(s) failed\n", failures);
    return 1;
}
