// separate-velocity-set-gate-set-and-note-set tasks 9.1-9.3 -- shared sets on the real sequencer.
//
// The reducer's commands:: seam is bridged straight into a real sequencer::Sequencer, so these
// tests run the whole path a user edit takes: UIState -> reducer fan-out -> core1 commands ->
// MIDI bytes. The bridge also models the core1 loop (one command, then one update()).
//
//   9.1  Three patterns sharing ONE note set under three different gate sets emit exactly the
//        stream they emit when each owns an identical private copy (R3.2, R9.3).
//   9.2  The same equality holds for PENDULUM and RANDOM orders, which only holds if every
//        pattern keeps its own cursor and random state (R3.4, R3.5).
//   9.3  An edit of a shared set is applied to all its patterns before the tick that is already
//        due, because of the batch frame (R8.2); the control sends the same edit unframed and
//        shows the tick DOES see mixed material, so the test would catch a missing frame.
//
// Compiles the real Reducer.cpp + SetLibrary.cpp and links genseq_emission (Sequencer). Own main().

#include "FakeMonotonicClock.h"
#include "RecordingMidiOutput.h"
#include "sequencer.h"
#include "command.h"
#include "midi_messages.h"
#include "ui/state/Reducer.h"
#include "ui/state/SetLibrary.h"
#include "ui/state/UIState.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace midi = sequencer::midi;
using namespace ui::state;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// --- Bridge: commands:: seam -> real Sequencer ------------------------------------------------
sequencer::Sequencer* g_seq = nullptr;

void deliver(commands::CommandMessage msg) {
    g_seq->processCommand(msg);
    g_seq->update();  // core1 loop: one command, then one update()
}

}  // namespace

namespace commands {
void sendCommand(Command cmd, uint8_t p1, uint8_t p2) {
    CommandMessage m;
    m.cmd = cmd;
    m.param1 = p1;
    m.param2 = p2;
    deliver(m);
}
void sendGateSet(uint8_t pattern, const std::vector<bool>& gates) {
    CommandMessage m;
    m.cmd = Command::PATTERN_GATE_SET;
    m.param1 = pattern;
    m.gateCount = static_cast<uint16_t>(gates.size());
    m.gates = gates;
    deliver(m);
}
void sendPitchSet(uint8_t pattern, uint8_t count, common::PlayingOrder order,
                  const std::vector<uint8_t>& pitches) {
    CommandMessage m;
    m.cmd = Command::PATTERN_PITCH_SET;
    m.param1 = pattern;
    m.pitchCount = count;
    m.pitchOrder = order;
    m.pitches = pitches;
    deliver(m);
}
void sendVelocitySet(uint8_t pattern, uint8_t count, common::PlayingOrder order,
                     const std::vector<uint8_t>& velocities) {
    CommandMessage m;
    m.cmd = Command::PATTERN_VELOCITY_SET;
    m.param1 = pattern;
    m.velocityCount = count;
    m.velocityOrder = order;
    m.velocities = velocities;
    deliver(m);
}
void sendGateSetMulti(uint16_t mask, const std::vector<bool>& gates) {
    CommandMessage m;
    m.cmd = Command::PATTERN_GATE_SET_MULTI;
    m.patternMask = mask;
    m.gateCount = static_cast<uint16_t>(gates.size());
    m.gates = gates;
    deliver(m);
}
void sendPitchSetMulti(uint16_t mask, uint8_t count, common::PlayingOrder order,
                       const std::vector<uint8_t>& pitches) {
    CommandMessage m;
    m.cmd = Command::PATTERN_PITCH_SET_MULTI;
    m.patternMask = mask;
    m.pitchCount = count;
    m.pitchOrder = order;
    m.pitches = pitches;
    deliver(m);
}
void sendVelocitySetMulti(uint16_t mask, uint8_t count, common::PlayingOrder order,
                          const std::vector<uint8_t>& velocities) {
    CommandMessage m;
    m.cmd = Command::PATTERN_VELOCITY_SET_MULTI;
    m.patternMask = mask;
    m.velocityCount = count;
    m.velocityOrder = order;
    m.velocities = velocities;
    deliver(m);
}
void sendGenParams(uint8_t pattern, const common::GenerationParameters& params) {
    g_seq->setPatternGenerationParameters(pattern, params);
    CommandMessage m;
    m.cmd = Command::GEN_PARAMS_SET;
    m.param1 = pattern;
    deliver(m);  // counts against an open batch exactly like the FIFO command does
}
}  // namespace commands

namespace {

struct Rig {
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
    sequencer::Sequencer seq{out, clock};
    uint64_t base = 0;

    Rig() { g_seq = &seq; }
    ~Rig() { g_seq = nullptr; }

    uint64_t deadline(uint64_t n) const { return base + (n * 60ULL * 1000000ULL) / (120ULL * 24ULL); }
    void setTick(uint64_t n) { clock.setMicros(deadline(n)); }
    void tick(uint64_t n) { setTick(n); seq.update(); }
};

int countNoteOn(const std::vector<uint8_t>& s, int note) {
    int n = 0;
    for (size_t i = 0; i + 2 < s.size(); ++i) {
        if ((s[i] & 0xF0) == midi::ChannelVoiceMessage::NOTE_ON && s[i + 2] > 0 &&
            (note < 0 || s[i + 1] == note)) {
            ++n;
        }
    }
    return n;
}

// Commit `cfg` into gate slot `slot` with its per-step gate states seeded (as the editor does).
void storeGate(UIState& s, uint8_t slot, uint8_t steps, uint8_t pulses) {
    s.gateLib.slots[slot].steps = steps;
    s.gateLib.slots[slot].pulses = pulses;
    s.selectedGateSlot = slot;
    beginGateSetEdit(s);
    commitGateSetEdit(s);
}

// Three patterns with three different gate sets. `shared` = all patterns use note/velocity
// slot 0; otherwise pattern i owns an identical private copy in slot i.
UIState makeState(bool shared, common::PlayingOrder order) {
    UIState s;
    s.patternCount = 3;
    s.activePatterns = 0b111;
    uint8_t slot = 0;
    createSet(s, SetType::GATE, slot);
    createSet(s, SetType::GATE, slot);
    storeGate(s, 0, 8, 3);
    storeGate(s, 1, 4, 1);
    storeGate(s, 2, 16, 5);

    const uint8_t sets = shared ? 1 : 3;
    for (uint8_t i = 1; i < sets; ++i) {
        createSet(s, SetType::NOTE, slot);
        createSet(s, SetType::VELOCITY, slot);
    }
    for (uint8_t i = 0; i < sets; ++i) {
        PitchSetConfig& n = s.noteLib.slots[i];
        n.count = 5;
        n.order = order;
        const uint8_t pitches[5] = {60, 62, 65, 67, 71};
        for (uint8_t j = 0; j < 5; ++j) n.pitches[j] = pitches[j];
        VelocitySetConfig& v = s.velocityLib.slots[i];
        v.count = 3;
        v.order = order;
        v.velocities[0] = 100;
        v.velocities[1] = 70;
        v.velocities[2] = 90;
    }
    for (uint8_t p = 0; p < 3; ++p) {
        s.patternRefs[p].gate = p;
        s.patternRefs[p].note = shared ? 0 : p;
        s.patternRefs[p].velocity = shared ? 0 : p;
    }
    return s;
}

std::vector<uint8_t> runStream(bool shared, common::PlayingOrder order) {
    Rig rig;
    UIState s = makeState(shared, order);
    restoreSequencerFromState(s);  // seed core1 exactly as boot does
    rig.seq.applyInstrument(0, 0, 0b111);
    rig.base = rig.clock.nowMicros();
    setPlaying(s, true);
    for (uint64_t n = 1; n <= 192; ++n) rig.tick(n);
    setPlaying(s, false);
    return rig.out.record();
}

void checkSharedEqualsCopies(common::PlayingOrder order, const char* name) {
    const std::vector<uint8_t> shared = runStream(true, order);
    const std::vector<uint8_t> copies = runStream(false, order);
    check(countNoteOn(shared, -1) > 10, name);  // the patterns actually play
    check(shared == copies, name);
}

// 9.1: three gate sets over one note set play each pattern's own rhythm with the shared pitches.
void test_shared_note_set_under_three_rhythms() {
    const std::vector<uint8_t> shared = runStream(true, common::PlayingOrder::FORWARDS);
    const std::vector<uint8_t> copies = runStream(false, common::PlayingOrder::FORWARDS);
    check(shared == copies, "shared note set == private copies (forward order)");
    for (int note : {60, 62, 65, 67, 71}) {
        check(countNoteOn(shared, note) > 0, "every pitch of the shared note set is played");
    }
    check(countNoteOn(shared, 61) == 0 && countNoteOn(shared, 72) == 0,
          "no pitch outside the shared note set is played");
    // 192 ticks at 6 ticks/step: 8-step/3-pulse, 4-step/1-pulse and 16-step/5-pulse cycles
    // differ in length, so the per-pattern onset counts differ and sum to the stream total.
    check(countNoteOn(shared, -1) > 0, "stream contains onsets");
}

// 9.2: cursors and random state are per pattern.
void test_independent_cursors() {
    checkSharedEqualsCopies(common::PlayingOrder::FORWARDS, "forward cursors independent");
    checkSharedEqualsCopies(common::PlayingOrder::BACKWARDS, "backward cursors independent");
    checkSharedEqualsCopies(common::PlayingOrder::PENDULUM, "pendulum cursors independent");
    checkSharedEqualsCopies(common::PlayingOrder::RANDOM, "random sequences independent");
    // Sensitivity: the comparison would notice a difference (orders produce different streams).
    check(runStream(true, common::PlayingOrder::FORWARDS) != runStream(true, common::PlayingOrder::PENDULUM),
          "different playing orders give different streams");
    check(runStream(true, common::PlayingOrder::FORWARDS) != runStream(true, common::PlayingOrder::RANDOM),
          "forward and random orders give different streams");
}

// Two patterns, one shared note set {60}; both gates rise on tick 1. The user edits the shared
// pitch to 72 while tick 1 is already due.
struct EditRig {
    Rig rig;
    UIState s;
    EditRig() {
        s.patternCount = 2;
        s.activePatterns = 0b11;
        s.noteLib.slots[0].count = 1;
        s.noteLib.slots[0].pitches[0] = 60;
        s.gateLib.slots[0].steps = 1;
        s.gateLib.slots[0].pulses = 1;
        s.selectedGateSlot = 0;
        beginGateSetEdit(s);
        commitGateSetEdit(s);
        s.patternRefs[1] = s.patternRefs[0];  // patterns 0 and 1 share every slot-0 set
        restoreSequencerFromState(s);
        rig.seq.applyInstrument(0, 0, 0b11);
        rig.base = rig.clock.nowMicros();
        setPlaying(s, true);
        rig.out.clear();
        rig.setTick(1);  // tick 1 is due from here on; each delivered command is followed by update()
    }
};

// 9.3: framed edit -> both patterns change before the due tick fires.
void test_batched_edit_is_atomic() {
    EditRig e;
    e.s.selectedNoteSlot = 0;
    beginPitchSetEdit(e.s);
    e.s.selectedPitchSetField = PITCH_SET_FIELD_PITCH_BASE;
    adjustPitchSetValue(e.s, 1, /*coarse=*/true);  // +12 semitones: 60 -> 72, fans out to 0 and 1
    e.rig.seq.update();

    check(countNoteOn(e.rig.out.record(), 72) == 2, "both patterns sound the edited pitch");
    check(countNoteOn(e.rig.out.record(), 60) == 0, "no pattern sounds the old pitch");
    check(e.rig.seq.performanceMetrics().batchTimeouts == 0, "the batch closed cleanly");
}

// Control: the same edit without the frame lets the due tick see pattern 0 new and pattern 1 old.
void test_unframed_edit_would_mix() {
    EditRig e;
    commands::sendPitchSet(0, 1, common::PlayingOrder::FORWARDS, {72});
    commands::sendPitchSet(1, 1, common::PlayingOrder::FORWARDS, {72});
    e.rig.seq.update();

    check(countNoteOn(e.rig.out.record(), 60) == 1 && countNoteOn(e.rig.out.record(), 72) == 1,
          "without the frame the due tick plays mixed old and new material");
}

}  // namespace

int main() {
    test_shared_note_set_under_three_rhythms();
    test_independent_cursors();
    test_batched_edit_is_atomic();
    test_unframed_edit_would_mix();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("sequencer sharing OK\n");
    return 0;
}
