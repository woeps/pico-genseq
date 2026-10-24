// generative-engine-expansion task 14 — Command Dispatch (FR5, FR9, FR10,
// AC4-AC7).
//
// Wires the Mutate / Undo / Revert / Keep commands through the bounded
// CommandInbox into the sequencer's working state, scheduled by the
// three-level timing policy:
//
//   - a Mutate command (operator id, pattern index or all, FR7 target mask,
//     FR6 intensity, optional level-1 timing override) resolves its target
//     tick via TimingPolicy::resolveTiming (AC7): an Immediate command
//     applies on the next serviced tick, a QuantizeBeat / QuantizeBar
//     command stages in a fixed-capacity pending slot list and applies at
//     the next beat/bar boundary;
//   - each APPLIED mutation pushes the global undo stack first
//     (WorkingStateManager::recordBeforeMutation, AC5); a NO_CHANGE or
//     REJECTED mutation leaves the history untouched;
//   - Undo / Revert / Keep behave exactly as the T12/T13 state model
//     defines them, across patterns: Undo pops LIFO per pattern, Revert
//     restores every pattern's baseline and clears the stack, Keep
//     re-anchors the baseline at the current working state (AC6);
//   - the inbox stays bounded: a full ring refuses new commands without
//     blocking, and the pending quantized slot list drops (and acks
//     Rejected) when it is full;
//   - pending quantized commands survive late-tick reconciliation and apply
//     exactly at their resolved boundary tick, deterministically;
//   - note ownership is preserved across a mutation that changes the pitch /
//     velocity material MID-NOTE: the sounding note is released later by its
//     recorded onset identity, so there are no hung notes and no duplicate
//     offs (Milestone 2 guarantees stay intact).
//
// The mutation runs the REAL built-in operator from the mutation registry by
// id, on the working state the sequencer dispatches generative steps from —
// so the mutated material is live at the very next Note On.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_mutation_commands".

#include "FakeMonotonicClock.h"
#include "CommandInbox.h"
#include "IMutationOperator.h"
#include "MutationConstraints.h"
#include "mutations/MutationOperatorRegistry.h"
#include "OperationTimingConfig.h"
#include "PatternGenerativeState.h"
#include "TimingPolicy.h"
#include "WorkingStateManager.h"
#include "command.h"
#include "midi_messages.h"
#include "sequencer.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

namespace midi = sequencer::midi;
using sequencer::GenerativeTimingOverride;
using sequencer::MutationTarget;
using sequencer::PatternGenerativeState;
using sequencer::SequenceId;

// Raw OperationTimingMode ordinals the inbox's override field carries
// (CommandInbox.h stays SDK-free, so the byte is documented there).
constexpr uint8_t kModeImmediate = 0;
constexpr uint8_t kModeBeat = 1;
constexpr uint8_t kModeBar = 2;

GenerativeTimingOverride overrideBeat() { return GenerativeTimingOverride{true, kModeBeat}; }
GenerativeTimingOverride overrideBar() { return GenerativeTimingOverride{true, kModeBar}; }
GenerativeTimingOverride overrideImmediate() {
    return GenerativeTimingOverride{true, kModeImmediate};
}

// Test-only fixed-capacity MIDI sink (the recording double the sibling
// generator tests use).
class FixedMidiOutput final : public sequencer::IMidiOutput {
public:
    static constexpr size_t kCapacity = 4096;

    void write(uint8_t byte) override {
        if (count_ < kCapacity) bytes_[count_++] = byte;
    }

    int countVoice(uint8_t status) const {
        int n = 0;
        for (size_t i = 0; i + 2 < count_; ++i) {
            if ((bytes_[i] & 0xF0) == status) {
                ++n;
                i += 2;
            }
        }
        return n;
    }

    // (data1, data2) pairs of every voice message with this status byte, in
    // emission order — so tests can match each Note On with its Note Off.
    std::vector<std::pair<uint8_t, uint8_t>> voicePairs(uint8_t status) const {
        std::vector<std::pair<uint8_t, uint8_t>> out;
        for (size_t i = 0; i + 2 < count_; ++i) {
            if ((bytes_[i] & 0xF0) == status) {
                out.push_back({bytes_[i + 1], bytes_[i + 2]});
                i += 2;
            }
        }
        return out;
    }

private:
    uint8_t bytes_[kCapacity] = {};
    size_t count_ = 0;
};

// Drives the real Sequencer with a fake clock and a recording sink (the
// harness the sibling generator tests use; the mutation registry is the
// process-wide g_mutationRegistry the dispatch resolves ids through).
struct Harness {
    FakeMonotonicClock clock{1000};
    FixedMidiOutput out;
    sequencer::GeneratorRegistry registry;
    sequencer::Sequencer seq{out, clock, registry};
    uint16_t bpm = 120;
    uint64_t base = 0;

    void setGate(const std::vector<bool>& g) {
        commands::CommandMessage m;
        m.cmd = commands::Command::PATTERN_GATE_SET;
        m.param1 = 0;
        m.gateCount = static_cast<uint16_t>(g.size());
        m.gates = g;
        seq.processCommand(m);
    }
    void addPattern() {
        commands::CommandMessage m;
        m.cmd = commands::Command::PATTERN_ADD;
        seq.processCommand(m);
    }
    void play() {
        base = clock.nowMicros();
        commands::CommandMessage m;
        m.cmd = commands::Command::PLAY;
        seq.processCommand(m);
    }
    uint64_t deadline(uint64_t n) const {
        return base + (n * 60ULL * 1000000ULL) / (bpm * 24ULL);
    }
    // Advance to tick n's deadline and service: tick(n) services gate step
    // n-1 (the timeline advances to tick index n before the tick callback).
    void tick(uint64_t n) { clock.setMicros(deadline(n)); seq.update(); }
    // Service without advancing the clock: only inbox draining happens (no
    // tick is due), so an Immediate command pulled off the queue now applies
    // BEFORE the next serviced tick.
    void drain() { seq.update(); }

    // Stage a Mutate command in the sequencer's bounded inbox.
    SequenceId pushMutate(const char* opId, uint8_t patternIndex, uint16_t targets,
                          uint8_t intensity, const GenerativeTimingOverride& timing = {}) {
        return seq.commandInbox().tryPush(
            sequencer::makeMutateCommand(opId, patternIndex, targets, intensity,
                                         /*invariants=*/0, /*maxDistance=*/255, timing));
    }
    SequenceId pushUndo(const GenerativeTimingOverride& timing = {}) {
        return seq.commandInbox().tryPush(sequencer::makeUndoCommand(timing));
    }
    SequenceId pushRevert(const GenerativeTimingOverride& timing = {}) {
        return seq.commandInbox().tryPush(sequencer::makeRevertCommand(timing));
    }
    SequenceId pushKeep(const GenerativeTimingOverride& timing = {}) {
        return seq.commandInbox().tryPush(sequencer::makeKeepCommand(timing));
    }

    // Populate a pattern slot's working-state material through the public
    // generativeState accessor (the same path the performer's UI uses).
    PatternGenerativeState* state(uint8_t index) { return seq.generativeState(index); }

    void setStatePitch(uint8_t index, std::initializer_list<uint8_t> values) {
        PatternGenerativeState* st = state(index);
        uint8_t i = 0;
        for (uint8_t v : values) st->pitchSet.values[i++] = v;
        st->pitchSet.count = i;
    }
    void setStateVelocity(uint8_t index, std::initializer_list<uint8_t> values) {
        PatternGenerativeState* st = state(index);
        uint8_t i = 0;
        for (uint8_t v : values) st->velocitySet.values[i++] = v;
        st->velocitySet.count = i;
    }
    bool statePitchIs(uint8_t index, std::initializer_list<uint8_t> values) const {
        const PatternGenerativeState* st = seq.generativeState(index);
        if (st->pitchSet.count != values.size()) return false;
        uint8_t i = 0;
        for (uint8_t v : values) {
            if (st->pitchSet.values[i++] != v) return false;
        }
        return true;
    }
    bool stateVelocityIs(uint8_t index, std::initializer_list<uint8_t> values) const {
        const PatternGenerativeState* st = seq.generativeState(index);
        if (st->velocitySet.count != values.size()) return false;
        uint8_t i = 0;
        for (uint8_t v : values) {
            if (st->velocitySet.values[i++] != v) return false;
        }
        return true;
    }
    uint8_t undoSize() const { return seq.workingStateManager().undoStack().size(); }
    uint8_t pendingCount() const { return seq.pendingGenerativeCount(); }
};

// The rotation built-in mutates with intensity 127: the registered operator
// carries amount 1 and the intensity-scaled multiplier is 4, so a 3-element
// material set rotates right by 1: {a,b,c} -> {c,a,b}.
constexpr uint16_t kRotatePitch = 1u << 2;   // MutationTarget::PITCH_ORDERING
constexpr uint16_t kRotateVelocity = 1u << 4;  // MutationTarget::VELOCITY_ORDERING

// --- 1: Immediate Mutate applies on the next tick (AC7, level-1 override) ----

void test_mutate_immediate_applies_on_next_tick() {
    Harness h;
    h.setGate({true, false, false, false});  // rising ticks 1, 5, 9, ...
    h.setStatePitch(0, {60, 64, 67});
    h.setStateVelocity(0, {100, 40, 20});

    h.play();
    h.tick(1);  // Note On (60, 100) from working-state material
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_ON) == 1,
          "immediate: the first tick sounds one note");

    // Level-1 Immediate override beats Mutate's QuantizeBeat factory mode:
    // the command applies during this drain, i.e. before the NEXT tick.
    const SequenceId seq = h.pushMutate("builtin.rotate", 0, kRotatePitch, 127,
                                        overrideImmediate());
    check(seq != sequencer::kInvalidSequence, "immediate: the push is accepted");
    h.drain();

    check(h.statePitchIs(0, {67, 60, 64}),
          "immediate: the mutation is applied at drain time, before the next tick");
    check(h.undoSize() == 1, "immediate: the applied mutation pushed the undo stack (AC5)");
    check(h.seq.commandInbox().ackStatus(seq) == sequencer::AckStatus::Applied,
          "immediate: the command acks Applied");

    h.tick(2);  // falling edge: the pre-mutation onset is released
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 1,
          "immediate: the note started before the mutation is released");

    h.tick(3);
    h.tick(4);
    h.tick(5);  // next rising edge: Note On from the MUTATED material
    const auto ons = h.out.voicePairs(midi::ChannelVoiceMessage::NOTE_ON);
    check(ons.size() == 2, "immediate: the next tick's note fires after the mutation");
    check(ons.size() == 2 && ons[1].first == 60,
          "immediate: the next Note On plays the rotated material (values[1]=60)");
    check(ons.size() == 2 && ons[1].second == 40,
          "immediate: velocity material untouched by a pitch-only mutation");
}

// --- 2: QuantizeBeat (the Mutate factory mode, level 2) ----------------------

void test_mutate_quantizes_to_beat_boundary() {
    Harness h;
    h.setGate({true, false, false, false});
    h.setStatePitch(0, {60, 64, 67});

    h.play();
    h.tick(1);
    h.tick(2);
    h.tick(3);  // tickIndex 3; the next beat boundary is tick 24

    // No override: the operation's own configured mode (level 2) is
    // QuantizeBeat — the command must NOT apply immediately.
    const SequenceId seq = h.pushMutate("builtin.rotate", 0, kRotatePitch, 127);
    h.drain();
    check(h.pendingCount() == 1,
          "beat: the quantized command stages in the pending slot list, not applied");
    check(h.statePitchIs(0, {60, 64, 67}), "beat: the material is untouched before the boundary");

    bool changedBeforeBoundary = false;
    for (uint64_t n = 4; n < 24; ++n) {
        h.tick(n);
        if (!h.statePitchIs(0, {60, 64, 67})) changedBeforeBoundary = true;
    }
    check(!changedBeforeBoundary, "beat: nothing applies before the boundary tick");
    check(h.pendingCount() == 1, "beat: the command survives until its resolved tick");

    h.tick(24);  // the beat boundary tick itself
    check(h.statePitchIs(0, {67, 60, 64}),
          "beat: the mutation applies exactly at the next beat boundary (tick 24)");
    check(h.pendingCount() == 0, "beat: the applied command leaves the pending list");
    check(h.undoSize() == 1, "beat: the applied mutation pushed the undo stack");
    check(h.seq.commandInbox().ackStatus(seq) == sequencer::AckStatus::Applied,
          "beat: the command acks Applied once executed");
}

// --- 3: QuantizeBar through the level-3 global default -----------------------

void test_mutate_quantizes_to_bar_boundary_via_global_default() {
    Harness h;
    h.setGate({true, false, false, false});
    h.setStatePitch(0, {60, 64, 67});

    h.play();
    h.tick(1);
    h.tick(2);
    h.tick(3);

    // Level 3: configure Mutate to Inherit and set the global default to
    // QuantizeBar. Resolution must fall through to the global default.
    h.seq.operationTiming().setMode(sequencer::GenerativeOperation::Mutate,
                                    sequencer::OperationTimingMode::Inherit);
    h.seq.operationTiming().setGlobalDefault(sequencer::OperationTimingMode::QuantizeBar);

    check(h.pushMutate("builtin.rotate", 0, kRotatePitch, 127) !=
              sequencer::kInvalidSequence,
          "bar: the push is accepted");
    h.drain();
    check(h.pendingCount() == 1, "bar: the command stages, unapplied");

    // TicksPerBar is 96 (4/4 at PPQN 24). Beat boundaries (24, 48, 72) must
    // NOT apply a bar-quantized command.
    for (uint64_t n = 4; n <= 95; ++n) {
        h.tick(n);
        if (n == 24 || n == 48 || n == 72 || n == 95) {
            check(h.statePitchIs(0, {60, 64, 67}),
                  "bar: the beat boundaries do not apply the command");
        }
    }
    h.tick(96);  // the bar boundary tick
    check(h.statePitchIs(0, {67, 60, 64}),
          "bar: the Inherit + global-default command applies at the bar boundary (tick 96)");
    check(h.undoSize() == 1, "bar: the applied mutation pushed the undo stack");
}

// --- 4: Undo / Revert / Keep across patterns (AC5, AC6) -----------------------

void test_undo_revert_keep_across_patterns() {
    Harness h;
    h.addPattern();  // two patterns
    h.setStatePitch(0, {60, 64, 67});
    h.setStatePitch(1, {72, 76, 79});
    // The setup writes simulate a pattern set arriving from the load path:
    // capture them as the baseline Revert returns to (the load path
    // re-anchors the same way; Keep is the performer's explicit re-anchor
    // while running).
    h.seq.workingStateManager().captureBaseline();

    // Two mutations, one per pattern: the single global stack holds both.
    check(h.pushMutate("builtin.rotate", 0, kRotatePitch, 127, overrideImmediate()) !=
              sequencer::kInvalidSequence,
          "undo: the pattern-0 mutation is accepted");
    check(h.pushMutate("builtin.rotate", 1, kRotatePitch, 127, overrideImmediate()) !=
              sequencer::kInvalidSequence,
          "undo: the pattern-1 mutation is accepted");
    h.drain();
    check(h.statePitchIs(0, {67, 60, 64}), "undo: pattern 0 mutated");
    check(h.statePitchIs(1, {79, 72, 76}), "undo: pattern 1 mutated");
    check(h.undoSize() == 2, "undo: each applied mutation pushed its own snapshot");

    // Undo pops LIFO across patterns: pattern 1 (the most recent) first.
    h.pushUndo();
    h.drain();
    check(h.statePitchIs(1, {72, 76, 79}), "undo: the most recent pattern is restored");
    check(h.statePitchIs(0, {67, 60, 64}), "undo: the earlier pattern stays mutated");
    check(h.undoSize() == 1, "undo: one snapshot consumed");

    h.pushUndo();
    h.drain();
    check(h.statePitchIs(0, {60, 64, 67}), "undo: the second undo restores pattern 0");
    check(h.undoSize() == 0, "undo: the stack is empty again");

    // Undo with an empty stack is a no-op, still acked Applied.
    const SequenceId emptyUndo = h.pushUndo();
    h.drain();
    check(h.statePitchIs(0, {60, 64, 67}) && h.statePitchIs(1, {72, 76, 79}),
          "undo: an empty-stack undo changes nothing");
    check(h.seq.commandInbox().ackStatus(emptyUndo) == sequencer::AckStatus::Applied,
          "undo: an empty-stack undo still acks Applied");

    // Revert restores EVERY pattern's baseline and clears the stack.
    h.pushMutate("builtin.rotate", 0, kRotatePitch, 127, overrideImmediate());
    h.pushMutate("builtin.rotate", 1, kRotatePitch, 127, overrideImmediate());
    h.drain();
    check(h.undoSize() == 2, "revert: two mutations to walk back from");
    h.pushRevert();
    h.drain();
    check(h.statePitchIs(0, {60, 64, 67}) && h.statePitchIs(1, {72, 76, 79}),
          "revert: all patterns return to the baseline");
    check(h.undoSize() == 0, "revert: the undo stack is cleared");

    // Keep promotes the working state: a later Revert returns to the KEPT
    // state, not the original.
    h.pushMutate("builtin.rotate", 0, kRotatePitch, 127, overrideImmediate());
    h.drain();
    h.pushKeep();
    h.drain();
    check(h.undoSize() == 0, "keep: the undo stack is cleared (the kept state is the anchor)");
    h.pushMutate("builtin.rotate", 0, kRotatePitch, 127, overrideImmediate());
    h.drain();
    check(h.statePitchIs(0, {64, 67, 60}), "keep: pattern 0 mutated past the kept state");
    h.pushRevert();
    h.drain();
    check(h.statePitchIs(0, {67, 60, 64}),
          "keep: revert restores the KEPT state (the first rotation)");
    check(h.statePitchIs(1, {72, 76, 79}), "keep: the other pattern keeps its baseline");

    // "All patterns" selection: one command mutates every live pattern, in
    // slot order, each pushing its own snapshot.
    h.pushMutate("builtin.rotate", sequencer::kAllPatterns, kRotatePitch, 127,
                 overrideImmediate());
    h.drain();
    check(h.statePitchIs(0, {64, 67, 60}), "all: pattern 0 rotated");
    check(h.statePitchIs(1, {79, 72, 76}), "all: pattern 1 rotated");
    check(h.undoSize() == 2, "all: one snapshot per live pattern");
    h.pushUndo();
    h.drain();
    check(h.statePitchIs(1, {72, 76, 79}), "all: undo restores the last pattern first");
    h.pushUndo();
    h.drain();
    check(h.statePitchIs(0, {67, 60, 64}), "all: the second undo restores pattern 0");

    // Recovery commands can be quantized too (level-1 override): a beat-
    // quantized Undo waits for the boundary.
    h.play();
    h.tick(1);
    h.tick(2);
    h.tick(3);
    h.pushMutate("builtin.rotate", 0, kRotatePitch, 127, overrideImmediate());
    h.drain();
    check(h.statePitchIs(0, {64, 67, 60}),
          "quantized-undo setup: pattern 0 mutated past its kept state");
    check(h.undoSize() == 1, "quantized-undo setup: one snapshot pushed");
    h.pushUndo(overrideBeat());
    h.drain();
    check(h.statePitchIs(0, {64, 67, 60}),
          "quantized-undo: the undo does NOT apply before the boundary");
    for (uint64_t n = 4; n < 24; ++n) h.tick(n);
    check(h.statePitchIs(0, {64, 67, 60}),
          "quantized-undo: still not undone before the boundary tick");
    h.tick(24);
    check(h.statePitchIs(0, {67, 60, 64}),
          "quantized-undo: the undo applies at the beat boundary");
}

// --- 5: NO_CHANGE / REJECTED mutations push no undo (AC4/AC5) -----------------

void test_no_change_and_rejected_mutations_push_no_undo() {
    Harness h;
    h.setStatePitch(0, {60, 64, 67});

    // Intensity 0 is a NO_CHANGE mutation: no undo entry, material intact.
    const SequenceId noChange =
        h.pushMutate("builtin.rotate", 0, kRotatePitch, 0, overrideImmediate());
    h.drain();
    check(h.statePitchIs(0, {60, 64, 67}),
          "no-change: an intensity-0 mutation leaves the material intact");
    check(h.undoSize() == 0, "no-change: a NO_CHANGE mutation pushes no undo entry");

    // An operator that does not support the requested targets is REJECTED:
    // state restored, no undo entry.
    const SequenceId rejected = h.pushMutate("builtin.rotate", 0,
                                             1u << 0 /* GATE_DENSITY */, 127,
                                             overrideImmediate());
    h.drain();
    check(h.statePitchIs(0, {60, 64, 67}),
          "rejected: an unsupported-target mutation restores the state");
    check(h.undoSize() == 0, "rejected: a REJECTED mutation pushes no undo entry");
    check(h.seq.commandInbox().ackStatus(rejected) == sequencer::AckStatus::Rejected,
          "rejected: the command acks Rejected");

    // An unknown operator id is rejected before anything is touched.
    const SequenceId unknown = h.pushMutate("no.such.operator", 0, kRotatePitch, 127,
                                            overrideImmediate());
    h.drain();
    check(h.seq.commandInbox().ackStatus(unknown) == sequencer::AckStatus::Rejected,
          "unknown: an unregistered operator id acks Rejected");
    check(h.statePitchIs(0, {60, 64, 67}),
          "unknown: an unregistered operator id changes nothing");
    check(h.undoSize() == 0, "unknown: no undo entry is created");

    // An out-of-range pattern index is a bounded no-op, acked Rejected.
    const SequenceId outOfRange =
        h.pushMutate("builtin.rotate", 42, kRotatePitch, 127, overrideImmediate());
    h.drain();
    check(h.seq.commandInbox().ackStatus(outOfRange) == sequencer::AckStatus::Rejected,
          "range: an out-of-range pattern index acks Rejected");
    check(h.undoSize() == 0, "range: no undo entry is created");

    // A real mutation after all that still works and pushes exactly one.
    const SequenceId applied =
        h.pushMutate("builtin.rotate", 0, kRotatePitch, 127, overrideImmediate());
    h.drain();
    check(h.seq.commandInbox().ackStatus(applied) == sequencer::AckStatus::Applied,
          "no-change: a genuine mutation acks Applied");
    check(h.undoSize() == 1, "no-change: a genuine mutation pushes exactly one entry");
    check(h.seq.commandInbox().ackStatus(noChange) == sequencer::AckStatus::Applied,
          "no-change: a NO_CHANGE execution is still an applied command");
}

// --- 6: the full inbox rejects without blocking (bounded queue) ---------------

void test_full_inbox_rejects_without_blocking() {
    Harness h;
    h.setStatePitch(0, {60, 64, 67});

    // Fill the ring without draining: capacity is 32 slots.
    bool allAccepted = true;
    for (int i = 0; i < 32; ++i) {
        if (h.pushMutate("builtin.rotate", 0, kRotatePitch, 0, overrideImmediate()) ==
            sequencer::kInvalidSequence) {
            allAccepted = false;
        }
    }
    check(allAccepted, "full: 32 commands fit the bounded ring");
    check(h.seq.commandInbox().count() == 32, "full: the ring holds exactly 32");

    // The 33rd push is REFUSED — returned immediately (no blocking), and
    // the ring is left untouched.
    const SequenceId refused = h.pushMutate("builtin.rotate", 0, kRotatePitch, 0,
                                            overrideImmediate());
    check(refused == sequencer::kInvalidSequence,
          "full: the 33rd push is refused with kInvalidSequence");
    check(h.seq.commandInbox().count() == 32, "full: the refused push enqueues nothing");

    // Draining is bounded work: at most 8 commands leave the ring per
    // update() call, and the calls return (never hang).
    for (int i = 0; i < 4; ++i) h.drain();
    check(h.seq.commandInbox().empty(), "full: a bounded number of updates drains the ring");

    // The ring is usable again afterwards.
    check(h.pushMutate("builtin.rotate", 0, kRotatePitch, 0, overrideImmediate()) !=
              sequencer::kInvalidSequence,
          "full: a slot is free again after the drain");
}

// --- 7: the pending quantized slot list is bounded ----------------------------

void test_pending_quantized_slot_list_is_bounded() {
    Harness h;
    h.setGate({true, false, false, false});
    h.setStatePitch(0, {60, 64, 67});

    h.play();
    h.tick(1);
    h.tick(2);
    h.tick(3);

    // 32 beat-quantized commands: the drain pops all 32 across a bounded
    // number of updates, but the pending slot list holds only the first 8;
    // the surplus is DROPPED and acked Rejected (bounded, never blocking,
    // never unbounded memory).
    for (int i = 0; i < 32; ++i) {
        h.pushMutate("builtin.rotate", 0, kRotatePitch, 127);
    }
    check(h.seq.commandInbox().count() == 32, "pending: all 32 commands queue");

    for (int i = 0; i < 4; ++i) h.drain();
    check(h.seq.commandInbox().empty(), "pending: the bounded drain empties the inbox");
    check(h.pendingCount() == 8,
          "pending: the fixed-capacity pending list holds at most 8 staged commands");

    // Eight rotations of one step each: {60,64,67} rotated 8 mod 3 == 2
    // positions right -> {64,67,60}. The 8 staged commands apply together at
    // the boundary, in order.
    //
    // The scheduler's documented per-call budget (Req 3.3) advances at most
    // 8 ticks per update(): the 21-tick backlog from deadline(3) to
    // deadline(24) is caught up over two more BOUNDED update() calls, each
    // returning promptly. Tick 24 itself is due exactly at `now`, so it is
    // SERVICED authoritatively at the end of the catch-up (not reconciled).
    h.tick(24);
    for (int i = 0; i < 2; ++i) h.seq.update();
    check(h.statePitchIs(0, {64, 67, 60}),
          "pending: every staged command applies at the boundary, in order");
    check(h.undoSize() == 8, "pending: each of the 8 applications pushed the undo stack");
}

// --- 8: pending quantized commands survive late-tick reconciliation ----------

void test_quantized_survives_late_tick_reconciliation() {
    auto runScenario = [](Harness& h) {
        h.setGate({true, false, false, false});
        h.setStatePitch(0, {60, 64, 67});
        h.play();
        h.tick(1);
        h.tick(2);
        h.tick(3);

        // Stage a beat-quantized mutation (applies at tick 24).
        h.pushMutate("builtin.rotate", 0, kRotatePitch, 127);
        h.drain();
        check(h.pendingCount() == 1, "reconcile: the quantized command is staged");

        // Jump forward 11 tick periods: ticks 3..12 were missed and are
        // RECONCILED (below the one-beat fail-closed threshold); tick 13 is
        // serviced authoritatively. The boundary tick has not been reached.
        // The scheduler's documented budget advances at most 8 ticks per
        // call (Req 3.3), so the catch-up takes two bounded update() calls.
        h.clock.setMicros(h.deadline(13));
        for (int i = 0; i < 2; ++i) h.seq.update();
        check(h.statePitchIs(0, {60, 64, 67}),
              "reconcile: the command does not apply before its boundary");
        check(h.pendingCount() == 1,
              "reconcile: the pending command survives the reconciliation pass");

        // Jump again across the boundary: ticks 14..25 are reconciled
        // (tick 24 among them) and tick 26 is serviced. The command must
        // apply exactly at the reconciled boundary tick — deterministically,
        // whenever the transport catches up (two bounded update() calls).
        h.clock.setMicros(h.deadline(26));
        for (int i = 0; i < 2; ++i) h.seq.update();
        check(h.statePitchIs(0, {67, 60, 64}),
              "reconcile: the command applies at its boundary tick even when that "
              "tick is reconciled");
        check(h.pendingCount() == 0, "reconcile: the applied command leaves the list");
        check(h.undoSize() == 1, "reconcile: the mutation pushed the undo stack");
    };

    Harness h;
    runScenario(h);

    // Determinism: the identical command sequence on a fresh transport
    // reproduces the identical final working state, byte for byte.
    Harness h2;
    runScenario(h2);
    const PatternGenerativeState* a = h.seq.generativeState(0);
    const PatternGenerativeState* b = h2.seq.generativeState(0);
    check(std::memcmp(a, b, sizeof(PatternGenerativeState)) == 0,
          "reconcile: two identical runs produce byte-identical working state");
}

// --- 9: no hung notes when a mutation changes the material mid-note ----------

void test_mutation_mid_note_leaves_no_hung_notes() {
    Harness h;
    // A held-gate scaffold: the note sounds ticks 1..4 and again 7..10.
    h.setGate({true, true, true, false, false, false});
    h.setStatePitch(0, {60, 64, 67});
    h.setStateVelocity(0, {100, 40, 20});

    h.play();
    h.tick(1);  // Note On (60, 100): cursor position 0
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_ON) == 1, "hung: one note on");

    // MID-NOTE: rotate BOTH material sets (shift 4 -> right by 1 on a
    // 3-element set) while the note is still sounding.
    h.pushMutate("builtin.rotate", 0,
                 kRotatePitch | kRotateVelocity, 127, overrideImmediate());
    h.drain();
    check(h.statePitchIs(0, {67, 60, 64}) && h.stateVelocityIs(0, {20, 100, 40}),
          "hung: the mid-note mutation rewrites both material sets");
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_ON) == 1 &&
              h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 0,
          "hung: the mutation does not interrupt the sounding note");

    h.tick(2);
    h.tick(3);
    h.tick(4);  // falling edge: the release fires for the RECORDED onset
    const auto offs = h.out.voicePairs(midi::ChannelVoiceMessage::NOTE_OFF);
    check(offs.size() == 1, "hung: exactly one Note Off after the falling edge");
    check(offs.size() == 1 && offs[0].first == 60,
          "hung: the note started before the mutation is released at its onset pitch");

    h.tick(5);
    h.tick(6);
    h.tick(7);  // next rising edge: the note plays from the MUTATED material
    const auto ons = h.out.voicePairs(midi::ChannelVoiceMessage::NOTE_ON);
    check(ons.size() == 2, "hung: the second note fires");
    check(ons.size() == 2 && ons[1].first == 60 && ons[1].second == 100,
          "hung: the new note plays the mutated pitch AND velocity material");

    h.tick(8);
    h.tick(9);
    h.tick(10);  // falling edge again
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 2,
          "hung: the second note is released too");

    // Global balance: every Note On got exactly one Note Off — no hung
    // notes after a mutation that changed gates' material mid-note, and no
    // duplicate offs.
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_ON) == 2,
          "hung: exactly two Note Ons fired");
}

}  // namespace

int main() {
    // The dispatch resolves operator ids through the process-wide mutation
    // registry; register the built-in operators once for this test binary.
    sequencer::registerBuiltInMutations(sequencer::g_mutationRegistry);

    test_mutate_immediate_applies_on_next_tick();
    test_mutate_quantizes_to_beat_boundary();
    test_mutate_quantizes_to_bar_boundary_via_global_default();
    test_undo_revert_keep_across_patterns();
    test_no_change_and_rejected_mutations_push_no_undo();
    test_full_inbox_rejects_without_blocking();
    test_pending_quantized_slot_list_is_bounded();
    test_quantized_survives_late_tick_reconciliation();
    test_mutation_mid_note_leaves_no_hung_notes();

    if (failures == 0) {
        std::printf("test_mutation_commands: all checks passed\n");
        return 0;
    }
    std::printf("test_mutation_commands: %d check(s) failed\n", failures);
    return 1;
}
