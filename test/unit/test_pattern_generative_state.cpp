// generative-engine-expansion task 3 — per-pattern generative state and
// registry-based dispatch (FR1, FR4, AC1).
//
// Validates PatternGenerativeState (the fixed POD every pattern owns) and
// the Sequencer's per-role generator dispatch:
//
//   1. PatternGenerativeState is trivially copyable + standard layout, with
//      a static_assert bounding its fixed size (global constraint);
//   2. default construction yields valid defaults (no generators assigned,
//      C-Major scale constraint, empty material sets, FORWARDS order);
//   3. the sequencer dispatches to a fake registered generator BY ID for the
//      right role (gate edges, pitch, velocity, duration each observe their
//      own role's generator, never another role's);
//   4. a role with no generator assigned (or an id that is not registered)
//      falls back to the stored pattern data unchanged — Milestone 2
//      behavior preserved;
//   5. per-step dispatch performs no heap allocation;
//   6. each pattern owns its own PatternGenerativeState (slot isolation,
//      add/remove keep the slots aligned).
//
// The fake generators live ONLY in this test file; the sequencer resolves
// them by id through a local GeneratorRegistry, proving the core never
// names a concrete generator type (AC1).
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_pattern_generative_state".

#include "FakeMonotonicClock.h"
#include "GeneratorRegistry.h"
#include "IGenerator.h"
#include "PatternGenerativeState.h"
#include "sequencer.h"
#include "command.h"
#include "midi_messages.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

// ---------------------------------------------------------------------------
// Global allocation counter (same technique as example_steady_state_allocation):
// counts every operator new/new[] call so a code region can be bracketed and
// asserted heap-free. The fixed-capacity MIDI sink below stores nothing on
// the heap, so the only allocations observed are the production code's.
// ---------------------------------------------------------------------------
namespace {
volatile uint64_t g_allocCount = 0;
}

void* operator new(std::size_t n) {
    ++g_allocCount;
    if (n == 0) n = 1;
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

namespace midi = sequencer::midi;
using sequencer::GeneratorCapabilities;
using sequencer::GeneratorCapability;
using sequencer::GeneratorContext;
using sequencer::GeneratorOutput;
using sequencer::GeneratorParamSet;
using sequencer::GeneratorRegistry;
using sequencer::GeneratorRole;
using sequencer::GeneratorRng;
using sequencer::GenerativeValueSet;
using sequencer::IGenerator;
using sequencer::PatternGenerativeState;

constexpr GeneratorParamSet kNoParams{};

// --- 1: POD properties (compile-time) ----------------------------------------

static_assert(std::is_trivially_copyable<GenerativeValueSet>::value,
              "GenerativeValueSet must be trivially copyable");
static_assert(std::is_standard_layout<GenerativeValueSet>::value,
              "GenerativeValueSet must be standard layout");
static_assert(std::is_trivially_copyable<PatternGenerativeState>::value,
              "PatternGenerativeState must be trivially copyable");
static_assert(std::is_standard_layout<PatternGenerativeState>::value,
              "PatternGenerativeState must be standard layout");
static_assert(sizeof(PatternGenerativeState) <= 256,
              "PatternGenerativeState stays a small fixed POD");
static_assert(sequencer::kGeneratorRoleCount == 4,
              "exactly the four roles: gate, pitch, velocity, duration");
static_assert(sequencer::kGeneratorIdCapacity >= 20,
              "ids fit the builtin.* naming scheme");

// --- test-only fixed-capacity MIDI sink ---------------------------------------

// Records the byte stream without touching the heap so the allocation gate
// measures only the production code.
class FixedMidiOutput final : public sequencer::IMidiOutput {
public:
    static constexpr size_t kCapacity = 4096;

    void write(uint8_t byte) override {
        if (count_ < kCapacity) bytes_[count_++] = byte;
    }

    void clear() { count_ = 0; }
    size_t size() const { return count_; }
    uint8_t at(size_t i) const { return bytes_[i]; }

    // Number of NOTE_ON (or NOTE_OFF) triples on any channel.
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
    // The note number of the n-th (0-based) NOTE_ON/NOTE_OFF triple, or -1.
    int noteOf(uint8_t status, int nth) const {
        int seen = 0;
        for (size_t i = 0; i + 2 < count_; ++i) {
            if ((bytes_[i] & 0xF0) == status) {
                if (seen == nth) return bytes_[i + 1];
                ++seen;
                i += 2;
            }
        }
        return -1;
    }
    // The velocity byte of the n-th NOTE_ON triple, or -1.
    int velocityOf(int nth) const {
        int seen = 0;
        for (size_t i = 0; i + 2 < count_; ++i) {
            if ((bytes_[i] & 0xF0) == midi::ChannelVoiceMessage::NOTE_ON) {
                if (seen == nth) return bytes_[i + 2];
                ++seen;
                i += 2;
            }
        }
        return -1;
    }

private:
    uint8_t bytes_[kCapacity] = {};
    size_t count_ = 0;
};

// --- test-only fake generators ----------------------------------------------

// Canned deterministic generators: each produces its role's canned output as
// a PURE function of the step index, so the dispatch result identifies which
// generator the sequencer resolved for which role. The pitch fake also
// records the context plumbing (patternLength / scale) for assertion.

class FakeAlternatingGateGenerator final : public IGenerator {
public:
    const char* id() const override { return "fake.gate.alt"; }
    const char* name() const override { return "Fake Alternating Gate"; }
    GeneratorCapabilities capabilities() const override { return GeneratorCapability::GATE; }
    const GeneratorParamSet& paramDefs() const override { return kNoParams; }
    void generate(uint32_t stepIndex, const GeneratorContext&,
                  GeneratorOutput& output) const override {
        output = GeneratorOutput{};
        output.hasGate = true;
        output.gate = (stepIndex % 2u) == 0u;  // high on even steps
    }
};

class FakeHeldGateGenerator final : public IGenerator {
public:
    const char* id() const override { return "fake.gate.held"; }
    const char* name() const override { return "Fake Held Gate"; }
    GeneratorCapabilities capabilities() const override { return GeneratorCapability::GATE; }
    const GeneratorParamSet& paramDefs() const override { return kNoParams; }
    void generate(uint32_t, const GeneratorContext&,
                  GeneratorOutput& output) const override {
        output = GeneratorOutput{};
        output.hasGate = true;
        output.gate = true;  // held high: only a duration can end the note
    }
};

class FakePitchGenerator final : public IGenerator {
public:
    explicit FakePitchGenerator(const char* anId, uint8_t note) : id_(anId), note_(note) {}

    const char* id() const override { return id_; }
    const char* name() const override { return id_; }
    GeneratorCapabilities capabilities() const override { return GeneratorCapability::PITCH; }
    const GeneratorParamSet& paramDefs() const override { return kNoParams; }
    void generate(uint32_t, const GeneratorContext& context,
                  GeneratorOutput& output) const override {
        output = GeneratorOutput{};
        output.hasPitch = true;
        output.pitch = note_;
        lastPatternLength = context.patternLength;  // context plumbing record
        lastScaleRoot = (context.scale != nullptr) ? context.scale->root() : 0xFF;
        lastScaleTemplate = (context.scale != nullptr) ? context.scale->templateIndex() : 0xFF;
    }

    mutable uint8_t lastPatternLength = 0;
    mutable uint8_t lastScaleRoot = 0;
    mutable uint8_t lastScaleTemplate = 0;

private:
    const char* id_;
    uint8_t note_;
};

class FakeVelocityGenerator final : public IGenerator {
public:
    const char* id() const override { return "fake.velocity"; }
    const char* name() const override { return "Fake Velocity"; }
    GeneratorCapabilities capabilities() const override { return GeneratorCapability::VELOCITY; }
    const GeneratorParamSet& paramDefs() const override { return kNoParams; }
    void generate(uint32_t, const GeneratorContext&,
                  GeneratorOutput& output) const override {
        output = GeneratorOutput{};
        output.hasVelocity = true;
        output.velocity = 99;
    }
};

class FakeDurationGenerator final : public IGenerator {
public:
    const char* id() const override { return "fake.duration"; }
    const char* name() const override { return "Fake Duration"; }
    GeneratorCapabilities capabilities() const override { return GeneratorCapability::DURATION; }
    const GeneratorParamSet& paramDefs() const override { return kNoParams; }
    void generate(uint32_t, const GeneratorContext&,
                  GeneratorOutput& output) const override {
        output = GeneratorOutput{};
        output.hasDuration = true;
        output.duration = 3;  // three transport ticks
    }
};

// --- harness ------------------------------------------------------------------

commands::CommandMessage cmd(commands::Command c, uint8_t p1 = 0) {
    commands::CommandMessage m;
    m.cmd = c;
    m.param1 = p1;
    return m;
}

// Drives the real Sequencer with a fake clock, a heap-free recording sink and
// a LOCAL registry the test populates with its fake generators.
struct Harness {
    FakeMonotonicClock clock{1000};
    FixedMidiOutput out;
    GeneratorRegistry registry;
    sequencer::Sequencer seq{out, clock, registry};
    uint16_t bpm = 120;
    uint64_t base = 0;

    void setGate(const std::vector<bool>& g) {
        commands::CommandMessage m = cmd(commands::Command::PATTERN_GATE_SET, 0);
        m.gateCount = static_cast<uint16_t>(g.size());
        m.gates = g;
        seq.processCommand(m);
    }
    void setPitch(const std::vector<uint8_t>& pitches) {
        commands::CommandMessage m = cmd(commands::Command::PATTERN_PITCH_SET, 0);
        m.pitchCount = static_cast<uint8_t>(pitches.size());
        m.pitchOrder = common::PlayingOrder::FORWARDS;
        m.pitches = pitches;
        seq.processCommand(m);
    }
    void setVelocity(const std::vector<uint8_t>& velocities) {
        commands::CommandMessage m = cmd(commands::Command::PATTERN_VELOCITY_SET, 0);
        m.velocityCount = static_cast<uint8_t>(velocities.size());
        m.velocityOrder = common::PlayingOrder::FORWARDS;
        m.velocities = velocities;
        seq.processCommand(m);
    }
    void addPattern() { seq.processCommand(cmd(commands::Command::PATTERN_ADD)); }
    void removePattern(uint8_t index) {
        seq.processCommand(cmd(commands::Command::PATTERN_REMOVE, index));
    }
    void play() { base = clock.nowMicros(); seq.processCommand(cmd(commands::Command::PLAY)); }
    uint64_t deadline(uint64_t n) const {
        return base + (n * 60ULL * 1000000ULL) / (bpm * 24ULL);
    }
    void tick(uint64_t n) { clock.setMicros(deadline(n)); seq.update(); }
};

// --- 2: default construction yields valid defaults ----------------------------

void test_defaults() {
    const PatternGenerativeState s{};

    check(!s.hasGenerator(GeneratorRole::GATE), "default GATE role has no generator");
    check(!s.hasGenerator(GeneratorRole::PITCH), "default PITCH role has no generator");
    check(!s.hasGenerator(GeneratorRole::VELOCITY), "default VELOCITY role has no generator");
    check(!s.hasGenerator(GeneratorRole::DURATION), "default DURATION role has no generator");
    check(!s.anyGeneratorAssigned(), "no role is assigned by default");
    check(std::strcmp(s.generatorId(GeneratorRole::GATE), "") == 0,
          "an unassigned role stores an empty id");
    check(std::strcmp(s.generatorId(GeneratorRole::DURATION), "") == 0,
          "an unassigned DURATION role stores an empty id");

    // FR4: the default scale constraint is C Major (root 0, template 0).
    check(s.scaleRoot == 0, "default scale root is pitch class 0 (C)");
    check(s.scaleTemplateIndex == 0, "default scale template is 0 (Major)");
    const common::Scale scale = s.scale();
    check(scale.root() == 0 && scale.templateIndex() == 0,
          "scale() rebuilds the default C-Major constraint");

    // Material sets: empty, forwards traversal.
    check(s.pitchSet.count == 0 && s.pitchSet.empty(), "default pitch set is empty");
    check(s.velocitySet.count == 0 && s.velocitySet.empty(), "default velocity set is empty");
    check(s.durationSet.count == 0 && s.durationSet.empty(), "default duration set is empty");
    check(s.pitchSet.order == common::PlayingOrder::FORWARDS,
          "default pitch playing order is FORWARDS");
    check(s.velocitySet.order == common::PlayingOrder::FORWARDS,
          "default velocity playing order is FORWARDS");
    check(s.durationSet.order == common::PlayingOrder::FORWARDS,
          "default duration playing order is FORWARDS");

    // Parameters default to zero.
    bool paramsZero = true;
    for (uint8_t r = 0; r < sequencer::kGeneratorRoleCount; ++r) {
        for (uint8_t p = 0; p < sequencer::kMaxGeneratorParams; ++p) {
            if (s.generatorParam(static_cast<GeneratorRole>(r), p) != 0) paramsZero = false;
        }
    }
    check(paramsZero, "all generator parameters default to zero");
}

// --- 2b: id storage, parameters, copyability ----------------------------------

void test_id_storage() {
    PatternGenerativeState s{};

    check(s.setGeneratorId(GeneratorRole::GATE, "fake.gate.alt"),
          "assigning a well-formed id succeeds");
    check(s.hasGenerator(GeneratorRole::GATE), "the assigned role reports a generator");
    check(std::strcmp(s.generatorId(GeneratorRole::GATE), "fake.gate.alt") == 0,
          "the assigned id round-trips");
    check(!s.hasGenerator(GeneratorRole::PITCH), "other roles stay unassigned");

    check(!s.setGeneratorId(GeneratorRole::PITCH, nullptr),
          "assigning a null id fails");
    check(!s.setGeneratorId(GeneratorRole::PITCH, ""),
          "assigning an empty id fails (use clearGeneratorId)");
    check(!s.setGeneratorId(GeneratorRole::PITCH, "this-id-is-far-too-long-for-storage"),
          "an id longer than the fixed storage is rejected, not truncated");
    check(!s.hasGenerator(GeneratorRole::PITCH),
          "a rejected assignment leaves the role unassigned");

    s.setGeneratorId(GeneratorRole::DURATION, "fake.duration");
    s.clearGeneratorId(GeneratorRole::DURATION);
    check(!s.hasGenerator(GeneratorRole::DURATION), "clearGeneratorId unassigns the role");

    s.setGeneratorParam(GeneratorRole::PITCH, 2, 96);
    check(s.generatorParam(GeneratorRole::PITCH, 2) == 96,
          "parameter values round-trip");
    s.setGeneratorParam(GeneratorRole::PITCH, sequencer::kMaxGeneratorParams, 1);
    check(s.generatorParam(GeneratorRole::PITCH, sequencer::kMaxGeneratorParams) == 0,
          "out-of-range parameter ids are rejected");

    // Trivially copyable: a by-value copy carries the full configuration.
    const PatternGenerativeState copy = s;
    check(copy.hasGenerator(GeneratorRole::GATE) &&
              std::strcmp(copy.generatorId(GeneratorRole::GATE), "fake.gate.alt") == 0,
          "a by-value copy carries the assigned ids");
    check(copy.generatorParam(GeneratorRole::PITCH, 2) == 96,
          "a by-value copy carries the parameter values");

    // The fixed set storage holds contents and ordering.
    GenerativeValueSet set{};
    set.values[0] = 60;
    set.values[1] = 67;
    set.count = 2;
    set.order = common::PlayingOrder::PENDULUM;
    s.pitchSet = set;
    const PatternGenerativeState copy2 = s;
    check(copy2.pitchSet.count == 2 && copy2.pitchSet.values[1] == 67 &&
              copy2.pitchSet.order == common::PlayingOrder::PENDULUM,
          "a by-value copy carries the set contents and ordering");
}

// --- 3: the sequencer dispatches by id, for the right role --------------------

void test_dispatch_by_role() {
    Harness h;
    FakeAlternatingGateGenerator gateGen;
    FakePitchGenerator pitchGen{"fake.pitch.a", 72};
    FakeVelocityGenerator velGen;
    check(h.registry.add(&gateGen), "gate fake registers");
    check(h.registry.add(&pitchGen), "pitch fake registers");
    check(h.registry.add(&velGen), "velocity fake registers");

    // A 4-tick stored gate scaffold defines the pattern length the context
    // reports; the gate CONTENT comes from the gate generator.
    h.setGate({false, false, false, false});

    PatternGenerativeState* st = h.seq.generativeState(0);
    check(st != nullptr, "generativeState(0) is reachable");
    check(st->setGeneratorId(GeneratorRole::GATE, "fake.gate.alt"),
          "GATE role assignment accepted");
    check(st->setGeneratorId(GeneratorRole::PITCH, "fake.pitch.a"),
          "PITCH role assignment accepted");
    check(st->setGeneratorId(GeneratorRole::VELOCITY, "fake.velocity"),
          "VELOCITY role assignment accepted");

    h.play();

    // Step 0: generated gate high (first edge) -> Note On with the PITCH
    // fake's note (72) and the VELOCITY fake's velocity (99), proving both
    // roles resolved THEIR generator by id.
    h.tick(1);
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_ON) == 1,
          "step 0 fires exactly one Note On");
    check(h.out.noteOf(midi::ChannelVoiceMessage::NOTE_ON, 0) == 72,
          "the Note On pitch is the PITCH generator's 72, not stored material");
    check(h.out.velocityOf(0) == 99,
          "the Note On velocity is the VELOCITY generator's 99, not stored material");
    check(pitchGen.lastPatternLength == 4,
          "the context carries the pattern length from the stored gate set");
    check(pitchGen.lastScaleRoot == 0 && pitchGen.lastScaleTemplate == 0,
          "the context carries the state's default C-Major scale constraint");

    // Step 1: generated gate low -> falling edge -> Note Off of the same note.
    h.tick(2);
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 1,
          "step 1 fires exactly one Note Off (generated falling edge)");
    check(h.out.noteOf(midi::ChannelVoiceMessage::NOTE_OFF, 0) == 72,
          "the Note Off releases the generated pitch");

    // Steps 2/3 repeat the cycle.
    h.tick(3);
    h.tick(4);
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_ON) == 2,
          "step 2 re-fires the generated gate");
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 2,
          "step 3 releases it again");
}

// --- 3b: the DURATION role dispatch drives the note length --------------------

void test_duration_dispatch() {
    Harness h;
    FakeHeldGateGenerator gateGen;
    FakePitchGenerator pitchGen{"fake.pitch.a", 72};
    FakeDurationGenerator durGen;
    check(h.registry.add(&gateGen), "held-gate fake registers");
    check(h.registry.add(&pitchGen), "pitch fake registers");
    check(h.registry.add(&durGen), "duration fake registers");

    PatternGenerativeState* st = h.seq.generativeState(0);
    st->setGeneratorId(GeneratorRole::GATE, "fake.gate.held");
    st->setGeneratorId(GeneratorRole::PITCH, "fake.pitch.a");
    st->setGeneratorId(GeneratorRole::DURATION, "fake.duration");
    // No VELOCITY generator: velocity falls back to the stored set (100).

    h.play();
    h.tick(1);
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_ON) == 1,
          "the held gate fires one Note On");
    check(h.out.velocityOf(0) == 100,
          "velocity falls back to the stored set while the DURATION role dispatches");

    h.tick(2);
    h.tick(3);
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 0,
          "the note still sounds before the generated duration elapses");

    h.tick(4);
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 1,
          "the generated duration (3 ticks) ends the note on the boundary");
    check(h.out.noteOf(midi::ChannelVoiceMessage::NOTE_OFF, 0) == 72,
          "the duration-driven release uses the recorded onset");

    h.tick(5);
    h.tick(6);
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_ON) == 1,
          "a held gate produces no re-fire after the duration release");
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 1,
          "no second Note Off is emitted");
}

// --- 4: unassigned roles fall back to the stored pattern data ------------------

void test_fallback_to_stored_data() {
    // (a) No generators at all: exact Milestone 2 behavior.
    {
        Harness h;
        h.setGate({true, false});
        h.setPitch({60});
        h.setVelocity({80});
        h.play();
        h.tick(1);
        check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_ON) == 1,
              "stored gate fires without any generator");
        check(h.out.noteOf(midi::ChannelVoiceMessage::NOTE_ON, 0) == 60,
              "stored pitch drives the Note On");
        check(h.out.velocityOf(0) == 80,
              "stored velocity drives the Note On");
        h.tick(2);
        check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 1,
              "stored falling edge releases the note");
    }

    // (b) Only PITCH assigned: gate and velocity stay stored.
    {
        Harness h;
        FakePitchGenerator pitchGen{"fake.pitch.a", 72};
        h.registry.add(&pitchGen);
        h.setGate({true, false});
        h.setPitch({60});
        h.setVelocity({80});
        PatternGenerativeState* st = h.seq.generativeState(0);
        st->setGeneratorId(GeneratorRole::PITCH, "fake.pitch.a");
        h.play();
        h.tick(1);
        check(h.out.noteOf(midi::ChannelVoiceMessage::NOTE_ON, 0) == 72,
              "the assigned PITCH generator overrides the stored pitch");
        check(h.out.velocityOf(0) == 80,
              "the unassigned VELOCITY role keeps the stored velocity");
        h.tick(2);
        check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 1,
              "the unassigned GATE role keeps the stored edge behavior");
    }

    // (c) An id that is not registered falls back to the stored material.
    {
        Harness h;
        FakePitchGenerator pitchGen{"fake.pitch.a", 72};
        h.registry.add(&pitchGen);
        h.setGate({true, false});
        h.setPitch({60});
        h.setVelocity({80});
        PatternGenerativeState* st = h.seq.generativeState(0);
        check(st->setGeneratorId(GeneratorRole::PITCH, "no.such.generator"),
              "an unregistered id can still be assigned");
        h.play();
        h.tick(1);
        check(h.out.noteOf(midi::ChannelVoiceMessage::NOTE_ON, 0) == 60,
              "an unregistered generator id falls back to the stored pitch");
        h.tick(2);
        check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 1,
              "the fallback release behavior is unchanged");
    }
}

// --- 6: each pattern owns its own state ---------------------------------------

void test_state_per_pattern() {
    Harness h;
    FakeAlternatingGateGenerator gateGen;
    FakePitchGenerator pitchA{"fake.pitch.a", 72};
    FakePitchGenerator pitchB{"fake.pitch.b", 64};
    h.registry.add(&gateGen);
    h.registry.add(&pitchA);
    h.registry.add(&pitchB);

    PatternGenerativeState* st0 = h.seq.generativeState(0);
    st0->setGeneratorId(GeneratorRole::GATE, "fake.gate.alt");
    st0->setGeneratorId(GeneratorRole::PITCH, "fake.pitch.a");

    // Out-of-range slots report nullptr instead of aliasing storage.
    check(h.seq.generativeState(99) == nullptr,
          "an out-of-range pattern slot reports nullptr");
    check(h.seq.generativeState(common::PATTERN_CAPACITY) == nullptr,
          "the slot bound is the fixed pattern capacity");

    // A second pattern gets its own state, configured independently.
    h.addPattern();
    PatternGenerativeState* st1 = h.seq.generativeState(1);
    check(st1 != nullptr, "generativeState(1) is reachable after PATTERN_ADD");
    check(!st1->hasGenerator(GeneratorRole::GATE),
          "a fresh pattern slot starts with no generators assigned");
    st1->setGeneratorId(GeneratorRole::GATE, "fake.gate.alt");
    st1->setGeneratorId(GeneratorRole::PITCH, "fake.pitch.b");

    h.play();
    h.tick(1);
    const int on0 = h.out.noteOf(midi::ChannelVoiceMessage::NOTE_ON, 0);
    const int on1 = h.out.noteOf(midi::ChannelVoiceMessage::NOTE_ON, 1);
    check(on0 == 72 && on1 == 64,
          "each pattern's Note On carries its own state's pitch generator");

    h.tick(2);
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 2,
          "both patterns release at the generated falling edge");

    // Removing a pattern keeps the remaining states aligned with their slots.
    h.removePattern(0);
    PatternGenerativeState* st = h.seq.generativeState(0);
    check(st != nullptr && std::strcmp(st->generatorId(GeneratorRole::PITCH), "fake.pitch.b") == 0,
          "after removal the surviving pattern's state keeps its configuration");
    check(st != nullptr && !st->hasGenerator(GeneratorRole::DURATION),
          "the surviving state is otherwise untouched");
}

// --- 5: dispatch performs no heap allocation -----------------------------------

void test_dispatch_no_allocation() {
    Harness h;
    FakeAlternatingGateGenerator gateGen;
    FakePitchGenerator pitchGen{"fake.pitch.a", 72};
    FakeVelocityGenerator velGen;
    FakeDurationGenerator durGen;
    h.registry.add(&gateGen);
    h.registry.add(&pitchGen);
    h.registry.add(&velGen);
    h.registry.add(&durGen);

    PatternGenerativeState* st = h.seq.generativeState(0);
    st->setGeneratorId(GeneratorRole::GATE, "fake.gate.alt");
    st->setGeneratorId(GeneratorRole::PITCH, "fake.pitch.a");
    st->setGeneratorId(GeneratorRole::VELOCITY, "fake.velocity");
    st->setGeneratorId(GeneratorRole::DURATION, "fake.duration");

    h.play();
    h.tick(1);  // warm-up: one full dispatching tick before measuring

    const uint64_t before = g_allocCount;
    for (uint64_t n = 2; n <= 41; ++n) {
        h.tick(n);
    }
    check(g_allocCount == before,
          "40 dispatching ticks perform zero heap allocations");
}

}  // namespace

int main() {
    test_defaults();
    test_id_storage();
    test_dispatch_by_role();
    test_duration_dispatch();
    test_fallback_to_stored_data();
    test_state_per_pattern();
    test_dispatch_no_allocation();

    if (failures == 0) {
        std::printf("test_pattern_generative_state: all checks passed\n");
        return 0;
    }
    std::printf("test_pattern_generative_state: %d check(s) failed\n", failures);
    return 1;
}
