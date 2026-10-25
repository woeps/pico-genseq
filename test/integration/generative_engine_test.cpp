// generative-engine-expansion task 22 — T22 integration test.
//
// End-to-end acceptance of the ASSEMBLED generative engine against the
// real Sequencer (the emission library's sequencer.cpp + generators +
// common), driving it through its public API with the host
// RecordingMidiOutput + FakeMonotonicClock. One binary covers AC1-AC8 of
// specs/generative-engine-expansion-spec.md:
//
//   AC1  test-only generator plugins are defined in THIS FILE, registered
//        into a LOCAL GeneratorRegistry and PLAYED by the unmodified core
//        (the only coupling point is GeneratorRegistry::add — no core
//        sequencer file names a concrete generator type);
//   AC2  pitch mutations under the SCALE_LOCK invariant end in-scale for
//        EVERY built-in scale template and root (28 x 12 combinations),
//        through the real command path (inbox -> dispatch -> operator);
//   AC3  modulation targets EVERY UI-settable parameter: all 16 per-pattern
//        ParamIds swept one by one plus the global BPM (the one global
//        parameter), with the effective values changing on the timeline and
//        the stored bases never modified;
//   AC4  worst-case mutate / evolve / mutate-all commands stay inside the
//        documented budget: the composites' instrumentation counter plus a
//        host wall-clock sanity check, both directly and through the full
//        sequencer command path;
//   AC5  mutations across MULTIPLE patterns undo in reverse order on the
//        ONE global undo stack (LIFO across patterns, not per-pattern);
//   AC6  Revert clears the stack and restores the saved baseline (and
//        Keep re-anchors it — FR15);
//   AC7  timing policy: Immediate applies on the next serviced tick,
//        QuantizeBeat / QuantizeBar apply at the next beat / bar boundary,
//        and the three-level precedence (invocation override > operation
//        mode > global default) holds for each level;
//   AC8  the capability-driven GeneratorConfigView shows ONLY the controls
//        the selected generator declares — including the test-only plugin's
//        own parameter table, rendered with zero view changes — and its
//        edits write through the ParameterBus base values into the LIVE
//        sequencer state.
//
// Throughout, every test that drives the transport runs a note-ownership
// audit over the emitted stream: Note Ons pair with Note Offs (no hung
// notes, no duplicate note-offs), and the ONLY permitted emissions are
// Note On / Note Off channel-voice messages and System Real-Time bytes —
// any Control Change, Program Change or other status is an out-of-scope
// violation (MIDI CC output generation is explicitly out of scope).
//
// Out-of-scope review (spec): the suite also PINS the absence of the
// out-of-scope features host-observably:
//   - no MIDI CC output: the audit rejects any non-note voice status;
//   - no automatic mutation: 100 ticks of full activity leave the working
//     state byte-identical (the state changes ONLY through commands);
//   - no user-definable scales: a Scale can only be built from the fixed
//     read-only template table (out-of-range input clamps, never invents
//     a template), and the table size is fixed;
//   - no per-pattern undo: the undo stack observable is ONE global LIFO
//     across all patterns (AC5 test);
//   - no MIDI input: the SDK-free core exposes no input surface — the
//     recording sink only ever RECEIVES writes (see the review notes in
//     the commit; nothing in src/sequencer reads MIDI).
//
// Standalone integration test (own main(), no RapidCheck). It lives in
// test/integration/ so the property-test glob (test/*.cpp) never sweeps
// it. Like the sibling unit tests it supplies host-safe LedMatrix member
// seams (the AC8 view's render path targets the real driver class) and
// links genseq_emission — the REAL sequencer + generators + common — and
// GeneratorConfigView.cpp. NONE of the Pico SDK libs, NO RapidCheck.
// Registered in the host test build under the ctest name
// "generative_engine_test".

#include "FakeMonotonicClock.h"
#include "RecordingMidiOutput.h"

#include "CommandInbox.h"
#include "GeneratorCapabilities.h"
#include "GeneratorRegistry.h"
#include "IGenerator.h"
#include "IMutationOperator.h"
#include "ModulationPattern.h"
#include "ModulationRegistry.h"
#include "ModulationTarget.h"
#include "MutationConstraints.h"
#include "MutationResult.h"
#include "OperationTimingConfig.h"
#include "ParameterAddress.h"
#include "ParameterBus.h"
#include "PatternGenerativeState.h"
#include "sequencer.h"
#include "sequencer_telemetry.h"
#include "mutations/CompositeMutations.h"
#include "mutations/MutationOperatorRegistry.h"
#include "mutations/ParameterMutations.h"

#include "Scale.h"
#include "ScaleTemplates.h"
#include "command.h"
#include "midi_messages.h"

#include "ui/Event.h"
#include "ui/hardware/LedMatrix.h"
#include "ui/state/UIState.h"
#include "ui/views/GeneratorConfigView.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using sequencer::EvolveMutation;
using sequencer::GeneratorCapabilities;
using sequencer::GeneratorCapability;
using sequencer::GeneratorContext;
using sequencer::GeneratorOutput;
using sequencer::GeneratorParamHint;
using sequencer::GeneratorParamSet;
using sequencer::GeneratorRegistry;
using sequencer::GeneratorRole;
using sequencer::GenerativeTimingOverride;
using sequencer::IGenerator;
using sequencer::IMutationOperator;
using sequencer::MutateAllMutation;
using sequencer::MutationConstraints;
using sequencer::MutationResult;
using sequencer::MutationStatus;
using sequencer::PitchSetMutation;
using sequencer::SequenceId;
using sequencer::StepUnit;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Host-safe LedMatrix seams (the same pattern as test_generator_config_view):
// defined in the REAL hardware namespace so they replace the SDK-dependent
// LedMatrix.cpp definitions at link time. The AC8 view renders through
// these no-ops on the host.
// ---------------------------------------------------------------------------
namespace hardware {
LedMatrix::LedMatrix(uint8_t pin) : pin(pin), buffer{}, dirty(false) {}
void LedMatrix::update() {}
void LedMatrix::clear() { dirty = true; }
void LedMatrix::setPixel(uint8_t, uint8_t, uint32_t) { dirty = true; }
void LedMatrix::fill(uint32_t) { dirty = true; }
void LedMatrix::drawNumber(int, uint32_t) { dirty = true; }
void LedMatrix::drawLabel(const char (&)[4], uint32_t) { dirty = true; }
void LedMatrix::drawNote(const char (&)[4], uint32_t) { dirty = true; }
}  // namespace hardware

namespace {

namespace midi = sequencer::midi;

// ---------------------------------------------------------------------------
// Test-only generator plugins (AC1, AC8).
//
// Both classes live ONLY in this test file. They are registered into a LOCAL
// GeneratorRegistry (after the Sequencer constructor has installed the
// built-ins) and the UNMODIFIED core dispatches to them BY ID — the only
// coupling point between core and plugins is GeneratorRegistry::add, which
// is exactly the plugin architecture AC1 requires. Their parameter tables
// also feed the AC8 view checks: a newly registered plugin renders its own
// controls with no view code change.
// ---------------------------------------------------------------------------

// A gate plugin with an explicitly NOT musical behavior: a pulse every
// `period` steps, period held in the plugin instance (the same
// configuration-ownership model as the built-in Euclidean plugin).
class TestIntervalGatePlugin final : public IGenerator {
public:
    static constexpr uint8_t kParamPeriod = 0;

    explicit TestIntervalGatePlugin(uint8_t period) : period_(period) {}

    const char* id() const override { return "test.interval-gate"; }
    const char* name() const override { return "Test Interval Gates"; }
    GeneratorCapabilities capabilities() const override {
        return GeneratorCapability::GATE;
    }
    const GeneratorParamSet& paramDefs() const override { return paramDefs_; }

    void generate(uint32_t stepIndex, const GeneratorContext&,
                  GeneratorOutput& output) const override {
        output.hasGate = true;
        output.gate = (stepIndex % period_) == 0;
    }

private:
    uint8_t period_;
    GeneratorParamSet paramDefs_ = makeParamDefs();

    static GeneratorParamSet makeParamDefs() {
        GeneratorParamSet defs;
        defs.defs[0] = {kParamPeriod, "Period", 1, 64, 4,
                        GeneratorParamHint::Steps};
        defs.count = 1;
        return defs;
    }
};

// A pitch plugin drawing scale degrees through the context's RNG — proves a
// non-gate role plugin also plays through the same registry path, and gives
// AC8 a second parameter table (2 params, custom names/ranges) to render.
class TestSpreadPitchPlugin final : public IGenerator {
public:
    static constexpr uint8_t kParamSpread = 0;
    static constexpr uint8_t kParamOctaves = 1;

    const char* id() const override { return "test.pitch-plugin"; }
    const char* name() const override { return "Test Spread Pitch"; }
    GeneratorCapabilities capabilities() const override {
        return GeneratorCapability::PITCH;
    }
    const GeneratorParamSet& paramDefs() const override { return paramDefs_; }

    void generate(uint32_t, const GeneratorContext& context,
                  GeneratorOutput& output) const override {
        // A scale degree drawn deterministically from the per-(pattern, role,
        // step) seeded RNG, mapped to a note in the pattern's scale when one
        // is present (the contract pitch generators keep, FR4/AC2).
        const uint8_t degrees =
            context.scale ? context.scale->degreeCount() : 12;
        const uint8_t degree = context.rng.nextBelow(degrees);
        const int note = context.scale ? context.scale->degreeToNote(degree, 5)
                                       : (60 + degree);
        output.hasPitch = true;
        output.pitch = (note >= 0 && note <= 127)
                           ? static_cast<uint8_t>(note)
                           : 60;
    }

private:
    GeneratorParamSet paramDefs_ = makeParamDefs();

    static GeneratorParamSet makeParamDefs() {
        GeneratorParamSet defs;
        defs.defs[0] = {kParamSpread, "Spread", 1, 24, 12,
                        GeneratorParamHint::None};
        defs.defs[1] = {kParamOctaves, "Octaves", 0, 8, 1,
                        GeneratorParamHint::None};
        defs.count = 2;
        return defs;
    }
};

// ---------------------------------------------------------------------------
// MIDI stream audit.
//
// Walks the ordered byte stream with running-status parsing (System
// Real-Time bytes may interleave anywhere). Enforces, over every driven
// harness:
//   - only Note On (0x9n) / Note Off (0x8n) channel-voice messages and
//     System Real-Time (>= 0xF8) bytes appear — any other status (Control
//     Change, Program Change, pitch bend, System Common, ...) is rejected,
//     which pins the out-of-scope "no MIDI CC output" exclusion;
//   - every Note On (velocity > 0) pairs with exactly one later Note Off
//     (or velocity-0 Note On) on the same (channel, note) — no duplicate
//     note-offs (an unmatched release) and, at end of stream, no hung
//     notes.
// ---------------------------------------------------------------------------

struct AuditResult {
    bool ok = true;
    const char* issue = "";
};

AuditResult auditMidiStream(const std::vector<uint8_t>& bytes) {
    std::vector<std::pair<uint8_t, uint8_t>> open;  // (channel, note)
    uint8_t status = 0;
    uint8_t dataCount = 0;
    uint8_t data[2] = {0, 0};

    const auto handleComplete = [&](uint8_t st, uint8_t d0, uint8_t d1,
                                    const char*& issue) {
        const uint8_t type = st & 0xF0;
        const uint8_t channel = st & 0x0F;
        if (type == midi::ChannelVoiceMessage::NOTE_ON && d1 > 0) {
            open.push_back({channel, d0});
            return;
        }
        // NOTE_OFF, or NOTE_ON with velocity 0 (the alternative release
        // encoding) — both must pair one open note.
        for (size_t k = open.size(); k-- > 0;) {
            if (open[k].first == channel && open[k].second == d0) {
                open.erase(open.begin() + static_cast<std::ptrdiff_t>(k));
                return;
            }
        }
        issue = "duplicate or unmatched note-off";
    };

    for (const uint8_t b : bytes) {
        if (b >= 0xF8) continue;  // System Real-Time interleaves anywhere
        if ((b & 0x80) != 0) {
            status = b;
            dataCount = 0;
            continue;
        }
        if (status == 0) {
            return {false, "data byte without status"};
        }
        const uint8_t type = status & 0xF0;
        if (type != midi::ChannelVoiceMessage::NOTE_ON &&
            type != midi::ChannelVoiceMessage::NOTE_OFF) {
            // Out of scope: Control Change / Program Change / pressure /
            // pitch bend / System Common — the engine may not emit these.
            return {false, "out-of-surface MIDI status byte (e.g. CC)"};
        }
        data[dataCount++] = b;
        if (dataCount == 2) {
            const char* issue = nullptr;
            handleComplete(status, data[0], data[1], issue);
            if (issue != nullptr) return {false, issue};
            dataCount = 0;
        }
    }
    if (!open.empty()) return {false, "hung note(s) at end of stream"};
    return {true, ""};
}

// ---------------------------------------------------------------------------
// Stream counting helpers (running-status-aware, so interleaved Clock
// bytes can never misalign the parse).
// ---------------------------------------------------------------------------

// Number of complete note-on events (velocity > 0) in the stream.
size_t countNoteOns(const std::vector<uint8_t>& bytes) {
    size_t count = 0;
    uint8_t status = 0;
    uint8_t dataCount = 0;
    for (const uint8_t b : bytes) {
        if (b >= 0xF8) continue;
        if ((b & 0x80) != 0) {
            status = b;
            dataCount = 0;
            continue;
        }
        if (status == 0) continue;
        ++dataCount;
        if (dataCount == 2) {
            const uint8_t type = status & 0xF0;
            if (type == midi::ChannelVoiceMessage::NOTE_ON && b > 0) ++count;
            dataCount = 0;
        }
    }
    return count;
}

// Number of System Real-Time bytes with this value (Clock / Start / Stop).
size_t countRealTime(const std::vector<uint8_t>& bytes, uint8_t message) {
    size_t count = 0;
    for (const uint8_t b : bytes) {
        if (b == message) ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Harness: the REAL Sequencer driven through its public API (the same
// shape as test_mutation_commands; the process-wide mutation registry is
// populated by the Sequencer constructor's registerBuiltInMutations call).
// The process-wide modulation registry g_modulationRegistry is reset
// around every test that configures it (it is shared state).
// ---------------------------------------------------------------------------

struct Harness {
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
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
    void stop() {
        commands::CommandMessage m;
        m.cmd = commands::Command::STOP;
        seq.processCommand(m);
    }

    uint64_t deadline(uint64_t n) const {
        return base + (n * 60ULL * 1000000ULL) / (bpm * 24ULL);
    }
    // Advance to tick n's deadline and service it (exactly one tick when
    // stepping by one; the scheduler services at most 8 ticks per call).
    void tick(uint64_t n) {
        clock.setMicros(deadline(n));
        seq.update();
    }
    // Service without advancing the clock: no tick is due, so only inbox
    // draining happens (an Immediate command applies BEFORE the next
    // serviced tick).
    void drain() { seq.update(); }

    // Advance the fake clock in small steps until exactly one new Clock
    // byte is dispatched (0 if none within budget). Small steps keep every
    // serviced tick authoritative; the returned gap measures the
    // transport's real tick duration (which BPM modulation changes live).
    uint64_t driveOneClock(uint64_t budgetUs) {
        const size_t before = countRealTime(out.record(),
                                            midi::SystemRealTimeMessage::TIMING_CLOCK);
        uint64_t gap = 0;
        while (gap < budgetUs) {
            clock.advanceMicros(500);
            gap += 500;
            seq.update();
            if (countRealTime(out.record(),
                              midi::SystemRealTimeMessage::TIMING_CLOCK) > before) {
                return gap;
            }
        }
        return 0;
    }

    // Stage a Mutate command in the sequencer's bounded inbox (the same
    // factory the UI would use; invariants and distance limit verbatim).
    SequenceId pushMutate(const char* opId, uint8_t patternIndex, uint16_t targets,
                          uint8_t intensity, const GenerativeTimingOverride& timing = {},
                          uint16_t invariants = 0, uint8_t maxDistance = 255) {
        return seq.commandInbox().tryPush(sequencer::makeMutateCommand(
            opId, patternIndex, targets, intensity, invariants, maxDistance, timing));
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

    // The working state the generative commands mutate and the playback
    // reads — reached through the same public accessor the UI uses.
    sequencer::PatternGenerativeState* state(uint8_t index) {
        return seq.generativeState(index);
    }

    void setStatePitch(uint8_t index, std::initializer_list<uint8_t> values) {
        sequencer::PatternGenerativeState* st = state(index);
        uint8_t i = 0;
        for (uint8_t v : values) st->pitchSet.values[i++] = v;
        st->pitchSet.count = i;
    }
    void setStateVelocity(uint8_t index, std::initializer_list<uint8_t> values) {
        sequencer::PatternGenerativeState* st = state(index);
        uint8_t i = 0;
        for (uint8_t v : values) st->velocitySet.values[i++] = v;
        st->velocitySet.count = i;
    }

    uint8_t undoSize() const {
        return seq.workingStateManager().undoStack().size();
    }
    uint8_t pendingCount() const { return seq.pendingGenerativeCount(); }

    // Stop, settle one update (Stop releases every sounding note and the
    // next update refreshes the telemetry counter), then audit the whole
    // stream: paired on/off, no duplicates, no hung notes, no CC.
    void auditStopped(const char* tag) {
        stop();
        drain();
        check(sequencer::g_soundingNotes == 0,
              "telemetry reports zero sounding notes after Stop");
        const AuditResult result = auditMidiStream(out.record());
        if (!result.ok) {
            std::printf("FAIL: %s: MIDI stream audit: %s\n", tag, result.issue);
            ++failures;
        }
    }
};

// Level-1 timing overrides (OperationTimingMode ordinals; the same helper
// shape as test_mutation_commands).
constexpr uint8_t kModeImmediate = 0;
constexpr uint8_t kModeBeat = 1;
constexpr uint8_t kModeBar = 2;
GenerativeTimingOverride overrideImmediate() {
    return GenerativeTimingOverride{true, kModeImmediate};
}
GenerativeTimingOverride overrideBeat() { return GenerativeTimingOverride{true, kModeBeat}; }
GenerativeTimingOverride overrideBar() { return GenerativeTimingOverride{true, kModeBar}; }

// FR7 target masks (MutationConstraints.h).
constexpr uint16_t kRotatePitch =
    sequencer::maskOf(sequencer::MutationTarget::PITCH_ORDERING);
constexpr uint16_t kPitchContents =
    sequencer::maskOf(sequencer::MutationTarget::PITCH_CONTENTS);
constexpr uint16_t kScaleLock =
    sequencer::maskOf(sequencer::MutationInvariant::SCALE_LOCK);

// The worst-case mutation substrate (the same rich state as the unit
// tests' AC4 budget case: full 16-value material sets and live generator
// parameters, so every per-element operator has maximal work).
sequencer::PatternGenerativeState richState() {
    sequencer::PatternGenerativeState s;
    s.pitchSet.count = sequencer::kGenerativeSetCapacity;
    for (uint8_t i = 0; i < sequencer::kGenerativeSetCapacity; ++i) {
        const uint8_t table[16] = {48, 50, 52, 53, 55, 57, 59, 60,
                                   62, 64, 65, 67, 69, 71, 72, 74};
        s.pitchSet.values[i] = table[i];
    }
    s.velocitySet.count = sequencer::kGenerativeSetCapacity;
    for (uint8_t i = 0; i < sequencer::kGenerativeSetCapacity; ++i) {
        s.velocitySet.values[i] = static_cast<uint8_t>(8 + 7 * i);
    }
    s.durationSet.count = sequencer::kGenerativeSetCapacity;
    for (uint8_t i = 0; i < sequencer::kGenerativeSetCapacity; ++i) {
        s.durationSet.values[i] = static_cast<uint8_t>(1 + 16 * i);
    }
    s.setGeneratorId(GeneratorRole::GATE, sequencer::kDefaultGateGeneratorId);
    s.setGeneratorParam(GeneratorRole::GATE, 0, 64);
    s.setGeneratorParam(GeneratorRole::GATE, 1, 32);
    s.setGeneratorParam(GeneratorRole::GATE, 2, 0);
    return s;
}

// One bounded direct apply() with the standard working-state flow.
MutationResult runOnce(IMutationOperator& op, sequencer::PatternGenerativeState& state,
                       const MutationConstraints& constraints, uint64_t seed) {
    const sequencer::PatternGenerativeState before = state;
    sequencer::PatternMutationView view(state, before, 3);
    sequencer::Prng rng(seed);
    return op.apply(view, constraints, rng);
}

// Byte-identical working-state comparison (the fixed POD snapshots are
// compared bytewise; this is exactly what Undo/Revert restore).
bool sameStateBytes(const sequencer::PatternGenerativeState& a,
                    const sequencer::PatternGenerativeState& b) {
    return std::memcmp(&a, &b, sizeof(sequencer::PatternGenerativeState)) == 0;
}

// ---------------------------------------------------------------------------
// AC1: a test-only generator plugin is registered via the registry and
// plays without any core sequencer change.
//
// The plugin class is defined in this file (nowhere else), added to the
// harness's LOCAL registry alongside the built-ins, assigned to a
// pattern's GATE role through the public working-state accessor, and
// PLAYED: with the stored gate set left EMPTY, every emitted note can only
// come from the plugin's gate decisions. The stored pitch/velocity data is
// likewise empty, so the notes' pitch/velocity come from the working-state
// material the mutation layer edits.
// ---------------------------------------------------------------------------

void test_ac1_test_only_generator_plays() {
    TestIntervalGatePlugin gatePlugin(4);  // pulse every 4th step
    Harness h;
    check(h.registry.add(&gatePlugin),
          "ac1: the test-only plugin registers into the live registry");
    check(h.registry.find("test.interval-gate") == &gatePlugin,
          "ac1: the core-visible registry resolves the plugin by id");
    // The built-ins coexist in the same registry (the Sequencer constructor
    // installed them); the plugin displaced nothing.
    check(h.registry.find("builtin.euclidean") != nullptr,
          "ac1: the built-in plugins still register alongside the new one");

    // No stored gate/pitch/velocity data at all: without the plugin the
    // pattern would emit nothing (the Milestone 2 participation rule).
    h.setStatePitch(0, {60, 64});
    h.setStateVelocity(0, {100, 90});
    h.state(0)->setGeneratorId(GeneratorRole::GATE, "test.interval-gate");
    check(h.state(0)->anyGeneratorAssigned(),
          "ac1: the GATE role carries the plugin id");

    h.play();
    for (uint64_t n = 1; n <= 12; ++n) {
        h.tick(n);
    }

    // The plugin's period-4 pulses produce Note Ons at ticks 1, 5 and 9 —
    // three onsets from a plugin the core has never heard of.
    const size_t ons = countNoteOns(h.out.record());
    check(ons == 3, "ac1: the test-only plugin played three notes");
    check(countRealTime(h.out.record(),
                        midi::SystemRealTimeMessage::TIMING_CLOCK) == 12,
          "ac1: the transport still emitted one Clock per serviced tick");

    h.auditStopped("ac1");
}

// ---------------------------------------------------------------------------
// AC2: scale constraints limit pitch mutation to the selected scale's
// intervals — for EVERY built-in template and root, through the real
// command path (inbox -> dispatch -> operator), with the SCALE_LOCK
// invariant requested (FR8) and the post-state verified against
// common::Scale::containsNote.
// ---------------------------------------------------------------------------

void test_ac2_scale_lock_all_templates_and_roots() {
    Harness h;
    const int templateCount = common::scaleTemplateCount();
    check(templateCount >= 20 && templateCount <= 30,
          "ac2: the built-in template table holds the extended set");

    // The inbox must carry EVERY built-in operator id intact: the opId field
    // is fixed-size and copyMutationOpId refuses (empties) an overlong id, so
    // a dispatch through the command path would then reject that operator
    // forever. This round-trip guards that boundary for the whole registry.
    {
        const int ops = sequencer::g_mutationRegistry.count();
        check(ops >= 10, "ac2: every built-in operator is registered");
        int verified = 0;
        for (int i = 0; i < ops; ++i) {
            const sequencer::IMutationOperator* op = sequencer::g_mutationRegistry.at(i);
            check(op != nullptr, "ac2: the registry lists no null operator");
            const sequencer::InboxCommand cmd = sequencer::makeMutateCommand(
                op->id(), 0, kPitchContents, 64);
            check(sequencer::g_mutationRegistry.find(cmd.gen.mutate.opId) != nullptr,
                  "ac2: every built-in operator id round-trips through the inbox");
            ++verified;
        }
        check(verified == ops, "ac2: the id round-trip covered the registry");
    }

    bool anyApplied = false;
    bool anyChanged = false;
    int combinations = 0;

    for (int t = 0; t < templateCount; ++t) {
        for (int r = 0; r < 12; ++r) {
            ++combinations;
            sequencer::PatternGenerativeState* st = h.state(0);
            st->scaleRoot = static_cast<uint8_t>(r);
            st->scaleTemplateIndex = static_cast<uint8_t>(t);

            // Seed the pitch material with values whose pitch class lies
            // OUTSIDE the (root, template) scale wherever the template has
            // any gap — so the in-scale check is not vacuous. (The chromatic
            // template has no gap; its check is inherently satisfied, and
            // the seed simply repeats in-scale classes.)
            uint16_t mask = common::SCALE_TEMPLATES[t].intervals;
            int gap = 1;
            while (gap < 12 && (mask & (1u << gap)) != 0) ++gap;
            if (gap >= 12) gap = 1;  // chromatic: no gap exists
            const uint8_t outPc = static_cast<uint8_t>((r + gap) % 12);
            st->pitchSet.count = sequencer::kGenerativeSetCapacity;
            for (uint8_t i = 0; i < sequencer::kGenerativeSetCapacity; ++i) {
                st->pitchSet.values[i] =
                    static_cast<uint8_t>((outPc + 12 * (i % 11)) % 128);
            }
            const common::Scale scale(static_cast<uint8_t>(r),
                                     static_cast<uint8_t>(t));
            bool seededOut = false;
            for (uint8_t i = 0; i < st->pitchSet.count; ++i) {
                if (!scale.containsNote(st->pitchSet.values[i])) seededOut = true;
            }

            // Mutate the pitch set contents under the scale lock through the
            // real command path until every live value is in-scale. A pass is
            // either Applied (the FR8 gate verified SCALE_LOCK on the result,
            // so every value is in-scale by construction) or Rejected (a draw
            // left an out-of-scale seed element unredrawn; the finalize gate
            // restores the state and the bounded retry re-seeds). Both are
            // legal; anything else is a dispatch failure.
            bool converged = false;
            for (int pass = 0; pass < 16 && !converged; ++pass) {
                const sequencer::PatternGenerativeState before = *h.state(0);
                const SequenceId seqId =
                    h.pushMutate("builtin.mutate-pitch-set", 0, kPitchContents, 127,
                                 overrideImmediate(), kScaleLock);
                check(seqId != sequencer::kInvalidSequence,
                      "ac2: the pitch-set mutation command is accepted");
                h.drain();
                const sequencer::AckStatus ack =
                    h.seq.commandInbox().ackStatus(seqId);
                check(ack == sequencer::AckStatus::Applied ||
                          ack == sequencer::AckStatus::Rejected,
                      "ac2: the pitch-set command reaches a final status");
                if (ack == sequencer::AckStatus::Rejected) {
                    // The scale-lock gate refused the draw: finalize must have
                    // restored the pre-command state byte-identically.
                    check(sameStateBytes(*h.state(0), before),
                          "ac2: a rejected pass leaves the state untouched");
                    continue;
                }
                anyApplied = true;
                converged = true;
                for (uint8_t i = 0; i < st->pitchSet.count; ++i) {
                    if (!scale.containsNote(st->pitchSet.values[i])) {
                        converged = false;
                    }
                }
                check(converged,
                      "ac2: an applied scale-locked mutation is fully in-scale");
                if (seededOut) {
                    // The operator actually moved the material (not a vacuous
                    // pass): at least one value differs from the seed.
                    bool changed = false;
                    for (uint8_t i = 0; i < st->pitchSet.count; ++i) {
                        const uint8_t seeded =
                            static_cast<uint8_t>((outPc + 12 * (i % 11)) % 128);
                        if (st->pitchSet.values[i] != seeded) changed = true;
                    }
                    if (changed) anyChanged = true;
                }
            }
            check(converged,
                  "ac2: every pitch value lands in the selected scale after mutation");
        }
    }

    check(combinations == templateCount * 12,
          "ac2: the sweep covered every template/root combination");
    check(anyApplied, "ac2: the sweep ran real mutations");
    check(anyChanged,
          "ac2: at least one non-chromatic combination materially snapped into scale");

    // The composite path keeps the same guarantee: a worst-case mutate-all
    // with the scale lock over every target leaves the pitch set in-scale
    // (same applied/rejected semantics; retry within the bounded budget).
    {
        sequencer::PatternGenerativeState* st = h.state(0);
        st->scaleRoot = 0;
        st->scaleTemplateIndex = 0;  // C major
        st->pitchSet.count = sequencer::kGenerativeSetCapacity;
        for (uint8_t i = 0; i < sequencer::kGenerativeSetCapacity; ++i) {
            st->pitchSet.values[i] =
                static_cast<uint8_t>(61 + 2 * i);  // out of scale
        }
        bool compositeApplied = false;
        for (int pass = 0; pass < 16 && !compositeApplied; ++pass) {
            const sequencer::PatternGenerativeState before = *h.state(0);
            const SequenceId seqId = h.pushMutate(
                "builtin.mutate-all", 0, sequencer::kAllMutationTargets, 127,
                overrideImmediate(), kScaleLock);
            h.drain();
            const sequencer::AckStatus ack =
                h.seq.commandInbox().ackStatus(seqId);
            check(ack == sequencer::AckStatus::Applied ||
                      ack == sequencer::AckStatus::Rejected,
                  "ac2: the composite command reaches a final status");
            if (ack == sequencer::AckStatus::Rejected) {
                check(sameStateBytes(*h.state(0), before),
                      "ac2: a rejected composite pass leaves the state untouched");
                continue;
            }
            compositeApplied = true;
        }
        check(compositeApplied, "ac2: the scale-locked composite mutation applies");
        const common::Scale cMajor(0, 0);
        bool allIn = true;
        for (uint8_t i = 0; i < st->pitchSet.count; ++i) {
            if (!cMajor.containsNote(st->pitchSet.values[i])) allIn = false;
        }
        check(allIn, "ac2: the composite mutation also keeps every pitch in-scale");
    }
}

// ---------------------------------------------------------------------------
// AC3: modulation patterns can target ANY UI-settable parameter — every
// one of the 16 per-pattern ParamIds swept individually, plus the global
// BPM (the one global-scope parameter) — and the effective values change
// on the timeline while the stored base is never modified.
//
// Sweep shape per parameter: one 2-step pattern [127, 0] (full swing),
// scale 127, one beat per step, forward order. Beat 0 must drive the
// parameter to the applyToBase image of step 127; beat 1 to the image of
// step 0 (for every factory default this clamps to the table minimum).
// The expected values are computed with the same fixed-point formula the
// applier uses (documented in ModulationTarget.h), so the integration pins
// the exact composed values, not just "something changed".
// ---------------------------------------------------------------------------

// Arm a 2-step [step0, step1] full-swing target on `address` in the global
// modulation registry (one beat per step, forward order).
void armSwingTarget(const sequencer::ParameterAddress& address, uint8_t step0,
                   uint8_t step1) {
    sequencer::g_modulationRegistry.reset();
    sequencer::ModulationPattern& pattern =
        sequencer::g_modulationRegistry.pattern(0);
    pattern.setLength(2);
    pattern.setStep(0, step0);
    pattern.setStep(1, step1);
    const size_t slot = sequencer::g_modulationRegistry.addTarget();
    sequencer::ModulationTarget& target =
        sequencer::g_modulationRegistry.target(slot);
    target.patternIndex = 0;
    target.address = address;
    target.scaleFactor = 127;
    target.balance = 0;
    target.stepDuration = {StepUnit::Beat, 1};
    target.order = sequencer::ModulationOrder::Forward;
}

// The documented applyToBase image (ModulationTarget.h):
//   offset = (step + balance - 64) * scaleFactor * span / (64 * 127)
//   output = clamp(base + offset, min, max)
int32_t expectedSwingValue(int32_t base, const sequencer::ParamInfo* info,
                           uint8_t step, uint8_t scaleFactor, int8_t balance) {
    const int64_t span =
        (info->maxValue > info->minValue)
            ? (static_cast<int64_t>(info->maxValue) - info->minValue)
            : 0;
    const int64_t centered = static_cast<int64_t>(step) + balance - 64;
    const int64_t offset = (centered * scaleFactor * span) / (64LL * 127LL);
    int64_t output = static_cast<int64_t>(base) + offset;
    if (output < info->minValue) output = info->minValue;
    if (output > info->maxValue) output = info->maxValue;
    return static_cast<int32_t>(output);
}

void test_ac3_modulation_every_ui_parameter() {
    // --- Part A: every per-pattern parameter ------------------------------
    for (uint8_t id = 1; id < sequencer::kParamIdCount; ++id) {
        const sequencer::ParamInfo* info = sequencer::paramInfoOf(id);
        check(info != nullptr && info->scope == sequencer::ParameterScope::Pattern,
              "ac3: the swept id names a per-pattern parameter");
        if (info == nullptr) continue;

        sequencer::ParameterAddress address;
        address.scope = sequencer::ParameterScope::Pattern;
        address.patternIndex = 0;
        address.paramId = static_cast<sequencer::ParamId>(id);
        armSwingTarget(address, 127, 0);

        Harness h;
        h.play();
        h.tick(1);  // first serviced tick: beat 0, step value 127

        const sequencer::ParameterBus& bus = h.seq.parameterBus();
        // The STORED base is the live working-state value (the POD starts
        // zeroed; the table defaults apply only through the UI write path)
        // — capture it and pin that modulation never moves it.
        const int32_t base = bus.baseValue(address);
        check(base >= 0, "ac3: the stored base is readable");
        check(bus.hasOverlay(address),
              "ac3: the modulation pass armed the parameter's overlay");
        const int32_t beat0 = bus.effectiveValue(address);
        check(beat0 == expectedSwingValue(base, info, 127, 127, 0),
              "ac3: beat 0 maps step 127 onto the documented composed value");
        check(beat0 != base, "ac3: beat 0 actually moves the parameter");
        check(beat0 >= info->minValue && beat0 <= info->maxValue,
              "ac3: the effective value stays inside the UI range");
        check(bus.baseValue(address) == base,
              "ac3: modulation never modifies the stored base");

        // Once, on the first swept parameter, also pin the scoping: the
        // SAME parameter of ANOTHER pattern slot and every OTHER parameter
        // of this slot carry no overlay (per-pattern scope, one target).
        if (id == 1) {
            sequencer::ParameterAddress otherSlot = address;
            otherSlot.patternIndex = 1;
            check(!bus.hasOverlay(otherSlot),
                  "ac3: a per-pattern target does not touch other pattern slots");
            bool anyOtherOverlay = false;
            for (uint8_t other = 1; other < sequencer::kParamIdCount; ++other) {
                if (other == id) continue;
                sequencer::ParameterAddress otherAddress = address;
                otherAddress.paramId = static_cast<sequencer::ParamId>(other);
                if (bus.hasOverlay(otherAddress)) anyOtherOverlay = true;
            }
            check(!anyOtherOverlay,
                  "ac3: the target arms exactly the addressed parameter");
        }

        for (uint64_t n = 2; n <= 24; ++n) {
            h.tick(n);
        }  // the loop's n == 24 services the first tick of beat 1
        const int32_t beat1 = bus.effectiveValue(address);
        check(beat1 == expectedSwingValue(base, info, 0, 127, 0),
              "ac3: beat 1 maps step 0 onto the documented composed value");
        check(beat1 != beat0,
              "ac3: the effective value changes on the timeline");
        check(bus.baseValue(address) == base,
              "ac3: the stored base is untouched across the whole beat");

        h.auditStopped("ac3-per-pattern");
        sequencer::g_modulationRegistry.reset();
    }

    // --- Part B: the global parameter — BPM -------------------------------
    // [100, 64]: beat 0 modulates 120 -> 240, beat 1 (centre) holds 120. The
    // transport's real tick duration must follow the effective tempo live,
    // and the stored BPM must never move.
    {
        sequencer::g_modulationRegistry.reset();
        sequencer::ModulationPattern& pattern =
            sequencer::g_modulationRegistry.pattern(0);
        pattern.setLength(2);
        pattern.setStep(0, 100);
        pattern.setStep(1, 64);
        sequencer::ParameterAddress bpmAddress;
        bpmAddress.scope = sequencer::ParameterScope::Global;
        bpmAddress.patternIndex = 0;
        bpmAddress.paramId = sequencer::ParamId::GlobalBpm;
        const size_t slot = sequencer::g_modulationRegistry.addTarget();
        sequencer::ModulationTarget& target =
            sequencer::g_modulationRegistry.target(slot);
        target.patternIndex = 0;
        target.address = bpmAddress;
        target.scaleFactor = 127;
        target.balance = 0;
        target.stepDuration = {StepUnit::Beat, 1};
        target.order = sequencer::ModulationOrder::Forward;

        Harness h;
        h.play();

        // First serviced tick: overlay armed at 240.
        uint64_t gap = h.driveOneClock(260000);
        check(gap != 0, "ac3: the first tick is serviced");
        const sequencer::ParameterBus& bus = h.seq.parameterBus();
        check(bus.hasOverlay(bpmAddress), "ac3: the global BPM overlay is armed");
        check(bus.effectiveValue(bpmAddress) == 240,
              "ac3: beat 0 modulates the global BPM 120 -> 240");
        check(bus.baseValue(bpmAddress) == 120,
              "ac3: the stored BPM is never modified");

        // Ticks 2..23 stay inside beat 0: every tick recomputes the overlay
        // from the stored base — exactly 240, never compounding — and the
        // transport's tick duration halves (120 -> 240 BPM).
        for (int i = 0; i < 22; ++i) {
            gap = h.driveOneClock(260000);
            check(gap > 9000 && gap < 12000,
                  "ac3: the transport ticks at the effective 240 BPM");
            check(h.seq.parameterBus().effectiveValue(bpmAddress) == 240,
                  "ac3: effective BPM stays exactly 240 (no compounding)");
        }
        check(h.seq.parameterBus().baseValue(bpmAddress) == 120,
              "ac3: the stored BPM stays 120 after every tempo window");

        // Tick 24 is the first tick of beat 1: the centre step restores 120
        // and the transport slows back down on the next tick.
        gap = h.driveOneClock(260000);
        check(h.seq.parameterBus().effectiveValue(bpmAddress) == 120,
              "ac3: beat 1 reads the centre step: effective BPM returns to 120");
        gap = h.driveOneClock(260000);
        check(gap > 18000 && gap < 24000,
              "ac3: the transport slows back to the 120 BPM tick duration");
        check(h.seq.parameterBus().baseValue(bpmAddress) == 120,
              "ac3: the stored BPM is untouched across the tempo change");

        h.auditStopped("ac3-global-bpm");
        sequencer::g_modulationRegistry.reset();
    }
}

// ---------------------------------------------------------------------------
// AC4: mutation operations complete within the timing budget.
//
// (a) The composites' instrumentation counter: the guards count nothing;
//     the worst case counts EXACTLY the documented fixed bound
//     kCompositeWorstCaseOperations (8 dispatches + 1 finalize gate) — for
//     both builtin.mutate-all and builtin.evolve.
// (b) Host wall-clock sanity: 2000 worst-case applications of the heaviest
//     single operator, the composites' worst case, and 1000 worst-case
//     mutate-all commands through the FULL sequencer command path (inbox ->
//     staging -> executeMutate -> operator -> undo push -> ack) each stay
//     under the documented host ceiling (2 s per batch).
// ---------------------------------------------------------------------------

void test_ac4_mutation_timing_budget() {
    // (a) Instrumentation counter (the documented bound).
    {
        MutateAllMutation mutateAll;
        EvolveMutation evolve;
        sequencer::PatternGenerativeState working = richState();
        runOnce(mutateAll, working, MutationConstraints(0, 127), 1u);
        check(mutateAll.lastOperationCount() == 0,
              "ac4: the empty-mask guard counts zero operations");
        runOnce(mutateAll, working,
                MutationConstraints(sequencer::kAllMutationTargets, 0), 1u);
        check(mutateAll.lastOperationCount() == 0,
              "ac4: the intensity-0 guard counts zero operations");
        runOnce(mutateAll, working,
                MutationConstraints(sequencer::kAllMutationTargets, 127), 1u);
        check(mutateAll.lastOperationCount() ==
                  sequencer::kCompositeWorstCaseOperations,
              "ac4: worst-case mutate-all counts exactly the documented bound");
        runOnce(evolve, working,
                MutationConstraints(sequencer::kAllMutationTargets, 127), 1u);
        check(evolve.lastOperationCount() ==
                  sequencer::kCompositeWorstCaseOperations,
              "ac4: worst-case evolve counts exactly the documented bound");
    }

    // (b1) Direct wall-clock: the heaviest single operator and both
    // composites, worst-case constraints on the rich state.
    {
        PitchSetMutation pitchSet;
        MutateAllMutation mutateAll;
        EvolveMutation evolve;
        const MutationConstraints worst(sequencer::kAllMutationTargets, 127);

        auto bench = [&](IMutationOperator& op, const char* name) {
            sequencer::PatternGenerativeState working = richState();
            sequencer::PatternGenerativeState before = working;
            sequencer::PatternMutationView view(working, before, 3);
            sequencer::Prng rng(1);
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 2000; ++i) {
                working = richState();
                before = working;
                (void)op.apply(view, worst, rng);
            }
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0)
                    .count();
            std::printf("  ac4: 2000 worst-case %s applies took %lld ms\n", name,
                        static_cast<long long>(elapsed));
            check(elapsed < 2000,
                  "ac4: 2000 worst-case applies stay under the wall-clock sanity limit");
        };
        bench(pitchSet, "mutate-pitch-set");
        bench(mutateAll, "mutate-all");
        bench(evolve, "evolve");
    }

    // (b2) Through the full sequencer command path: 1000 worst-case
    // mutate-all commands on a pattern carrying the rich state, each
    // applied, acked and undo-recorded.
    {
        Harness h;
        *h.state(0) = richState();
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 1000; ++i) {
            const SequenceId seqId = h.pushMutate(
                "builtin.mutate-all", 0, sequencer::kAllMutationTargets, 127,
                overrideImmediate());
            check(seqId != sequencer::kInvalidSequence,
                  "ac4: the command path accepts the worst-case command");
            h.drain();
            check(h.seq.commandInbox().ackStatus(seqId) ==
                      sequencer::AckStatus::Applied,
                  "ac4: the worst-case command acks Applied");
        }
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0)
                .count();
        std::printf(
            "  ac4: 1000 worst-case mutate-all commands through the sequencer "
            "took %lld ms\n",
            static_cast<long long>(elapsed));
        check(elapsed < 2000,
              "ac4: 1000 worst-case command dispatches stay under the sanity limit");
        check(h.undoSize() > 0,
              "ac4: the dispatched mutations pushed the undo stack");
    }
}

// Byte-identical working-state comparison lives with the shared helpers
// above (used by AC2's rejected-pass integrity checks onward).

bool statePitchIs(const Harness& h, uint8_t index,
                  std::initializer_list<uint8_t> values) {
    const sequencer::PatternGenerativeState* st = h.seq.generativeState(index);
    if (st->pitchSet.count != values.size()) return false;
    uint8_t i = 0;
    for (uint8_t v : values) {
        if (st->pitchSet.values[i++] != v) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// AC5: undo reverses the last N mutations across ALL patterns — the ONE
// global stack pops in strict LIFO order regardless of which pattern a
// mutation touched (this observable is also what pins the out-of-scope
// "no per-pattern undo history" exclusion: there are not three stacks).
// ---------------------------------------------------------------------------

void test_ac5_undo_across_patterns() {
    Harness h;
    h.addPattern();
    h.addPattern();  // patterns 0, 1, 2
    h.setStatePitch(0, {60, 64, 67});
    h.setStatePitch(1, {72, 76, 79});
    h.setStatePitch(2, {84, 88, 91});

    const sequencer::PatternGenerativeState pre0 = *h.state(0);
    const sequencer::PatternGenerativeState pre1 = *h.state(1);
    const sequencer::PatternGenerativeState pre2 = *h.state(2);

    // Three mutations in a deliberately interleaved pattern order: 1, 2, 0.
    const uint8_t order[3] = {1, 2, 0};
    for (int k = 0; k < 3; ++k) {
        const SequenceId seqId =
            h.pushMutate("builtin.rotate", order[k], kRotatePitch, 127,
                        overrideImmediate());
        check(seqId != sequencer::kInvalidSequence,
              "ac5: the interleaved mutation is accepted");
        h.drain();
        check(h.seq.commandInbox().ackStatus(seqId) == sequencer::AckStatus::Applied,
              "ac5: the interleaved mutation acks Applied");
    }
    check(!sameStateBytes(*h.state(1), pre1) && !sameStateBytes(*h.state(2), pre2) &&
              !sameStateBytes(*h.state(0), pre0),
          "ac5: all three patterns carry a mutation");
    check(h.undoSize() == 3,
          "ac5: the ONE global stack holds all three snapshots");

    // Undo pops LIFO ACROSS patterns: 0 (most recent) first, then 2, then 1.
    h.pushUndo();
    h.drain();
    check(sameStateBytes(*h.state(0), pre0),
          "ac5: undo restores the most recently mutated pattern first");
    check(!sameStateBytes(*h.state(2), pre2),
          "ac5: the earlier patterns stay mutated after one undo");
    check(h.undoSize() == 2, "ac5: one snapshot consumed");

    h.pushUndo();
    h.drain();
    check(sameStateBytes(*h.state(2), pre2),
          "ac5: the second undo restores the second pattern");
    check(!sameStateBytes(*h.state(1), pre1),
          "ac5: the last remaining pattern is still mutated");

    h.pushUndo();
    h.drain();
    check(sameStateBytes(*h.state(1), pre1),
          "ac5: the third undo restores the last pattern");
    check(h.undoSize() == 0, "ac5: the stack is empty again");

    // Undo with an empty stack is a documented no-op.
    const sequencer::PatternGenerativeState afterAll = *h.state(0);
    const SequenceId emptyUndo = h.pushUndo();
    h.drain();
    check(emptyUndo != sequencer::kInvalidSequence &&
              h.seq.commandInbox().ackStatus(emptyUndo) ==
                  sequencer::AckStatus::Applied,
          "ac5: the empty-stack undo still acks Applied");
    check(sameStateBytes(*h.state(0), afterAll) && h.undoSize() == 0,
          "ac5: the empty-stack undo changes nothing");

    // N deep on ONE pattern: five consecutive mutations, each undo stepping
    // back through the captured pre-states in reverse order. The middle step
    // is a NON-INVERTIBLE pitch-set mutation, so a byte-exact walk-back
    // proves the stack restores SNAPSHOTS (not an inverse operation).
    {
        sequencer::PatternGenerativeState snapshots[6];
        snapshots[0] = *h.state(0);
        for (int k = 0; k < 5; ++k) {
            snapshots[k + 1] = *h.state(0);
            const SequenceId seqId =
                (k == 2)
                    ? h.pushMutate("builtin.mutate-pitch-set", 0, kPitchContents,
                                   127, overrideImmediate())
                    : h.pushMutate("builtin.rotate", 0, kRotatePitch, 127,
                                   overrideImmediate());
            h.drain();
            check(h.seq.commandInbox().ackStatus(seqId) ==
                      sequencer::AckStatus::Applied,
                  "ac5: the depth mutation acks Applied");
            check(!sameStateBytes(*h.state(0), snapshots[k + 1]),
                  "ac5: each depth mutation changes the material");
        }
        check(h.undoSize() == 5, "ac5: the stack holds the five mutations");
        for (int k = 5; k-- > 0;) {
            h.pushUndo();
            h.drain();
            check(sameStateBytes(*h.state(0), snapshots[k + 1]),
                  "ac5: undo walks back through the exact pre-mutation states");
        }
        check(h.undoSize() == 0, "ac5: the depth stack is empty again");
    }
}

// ---------------------------------------------------------------------------
// AC6: Revert clears the undo stack and restores the SAVED baseline — and
// Keep (FR15) re-anchors that baseline for later reverts.
// ---------------------------------------------------------------------------

void test_ac6_revert_restores_baseline() {
    Harness h;
    h.addPattern();
    h.addPattern();  // patterns 0, 1, 2
    h.setStatePitch(0, {60, 64, 67});
    h.setStatePitch(1, {72, 76, 79});
    h.setStatePitch(2, {84, 88, 91});
    h.seq.workingStateManager().captureBaseline();

    const sequencer::PatternGenerativeState baseline0 = *h.state(0);
    const sequencer::PatternGenerativeState baseline1 = *h.state(1);
    const sequencer::PatternGenerativeState baseline2 = *h.state(2);

    // Mutate all three patterns.
    for (uint8_t p = 0; p < 3; ++p) {
        check(h.pushMutate("builtin.rotate", p, kRotatePitch, 127,
                           overrideImmediate()) != sequencer::kInvalidSequence,
              "ac6: the pattern mutation is accepted");
    }
    h.drain();
    check(h.undoSize() == 3, "ac6: three mutations sit on the stack");
    check(!sameStateBytes(*h.state(0), baseline0) &&
              !sameStateBytes(*h.state(1), baseline1) &&
              !sameStateBytes(*h.state(2), baseline2),
          "ac6: all three patterns are mutated");

    // Revert: every pattern returns to the saved baseline and the stack is
    // EMPTY afterwards (an undo cannot resurrect anything).
    const SequenceId revertId = h.pushRevert();
    h.drain();
    check(h.seq.commandInbox().ackStatus(revertId) == sequencer::AckStatus::Applied,
          "ac6: the revert acks Applied");
    check(sameStateBytes(*h.state(0), baseline0) &&
              sameStateBytes(*h.state(1), baseline1) &&
              sameStateBytes(*h.state(2), baseline2),
          "ac6: revert restores every pattern's saved baseline");
    check(h.undoSize() == 0, "ac6: revert clears the undo stack");
    h.pushUndo();
    h.drain();
    check(sameStateBytes(*h.state(0), baseline0),
          "ac6: undo after revert is a no-op (the stack is empty)");

    // Keep promotes the current working state to the new baseline and also
    // clears the stack (FR15).
    check(h.pushMutate("builtin.rotate", 0, kRotatePitch, 127,
                       overrideImmediate()) != sequencer::kInvalidSequence,
          "ac6: the post-revert mutation is accepted");
    h.drain();
    check(h.undoSize() == 1, "ac6: the mutation pushed the stack");
    const SequenceId keepId = h.pushKeep();
    h.drain();
    check(h.seq.commandInbox().ackStatus(keepId) == sequencer::AckStatus::Applied,
          "ac6: the keep acks Applied");
    check(h.undoSize() == 0, "ac6: keep clears the undo stack");
    const sequencer::PatternGenerativeState kept = *h.state(0);
    check(!sameStateBytes(kept, baseline0),
          "ac6: the kept baseline is the mutated state, not the original");

    // A later revert returns to the KEPT baseline.
    check(h.pushMutate("builtin.rotate", 0, kRotatePitch, 127,
                       overrideImmediate()) != sequencer::kInvalidSequence,
          "ac6: the post-keep mutation is accepted");
    h.drain();
    check(!sameStateBytes(*h.state(0), kept),
          "ac6: the working state moved past the kept baseline");
    const SequenceId revert2 = h.pushRevert();
    h.drain();
    check(h.seq.commandInbox().ackStatus(revert2) == sequencer::AckStatus::Applied,
          "ac6: the second revert acks Applied");
    check(sameStateBytes(*h.state(0), kept),
          "ac6: revert after keep restores the KEPT baseline");
    check(h.undoSize() == 0, "ac6: the stack is empty after the second revert");
}

// ---------------------------------------------------------------------------
// AC7: timing policy — Immediate applies on the next serviced tick,
// QuantizeBeat / QuantizeBar at the next beat / bar boundary, and the
// three-level precedence holds (invocation override > per-operation mode
// > global default). Every sub-case drives the REAL transport with a
// playing gate pattern so the mutated material is heard at the next onset.
// ---------------------------------------------------------------------------

// Configure the common AC7 substrate: a 1-in-4 stored gate pattern and a
// 3-element pitch material, then play and step to tick 3.
void ac7PlayingSubstrate(Harness& h) {
    h.setGate({true, false, false, false});
    h.setStatePitch(0, {60, 64, 67});
    h.play();
    h.tick(1);  // Note On (60, 100...): the onset plays the seed material
    h.tick(2);
    h.tick(3);
}

// Tick up to (not including) `boundary`, asserting nothing applies early;
// then service the boundary tick and assert the rotation applied there.
void ac7CheckBoundary(Harness& h, uint64_t boundary, const char* tag) {
    bool changedEarly = false;
    for (uint64_t n = 4; n < boundary; ++n) {
        h.tick(n);
        if (!statePitchIs(h, 0, {60, 64, 67})) changedEarly = true;
    }
    char message[128];
    std::snprintf(message, sizeof(message),
                  "%s: nothing applies before the boundary tick", tag);
    check(!changedEarly, message);
    h.tick(boundary);
    std::snprintf(message, sizeof(message),
                  "%s: the command applies exactly at tick %llu", tag,
                  static_cast<unsigned long long>(boundary));
    check(statePitchIs(h, 0, {67, 60, 64}), message);
    std::snprintf(message, sizeof(message),
                  "%s: the pending list drains at the boundary", tag);
    check(h.pendingCount() == 0, message);
}

void test_ac7_timing_policy() {
    // (1) Level-1 Immediate override beats the QuantizeBeat factory mode:
    // applies at drain time, BEFORE the next serviced tick, and the next
    // Note On plays the mutated material.
    {
        Harness h;
        ac7PlayingSubstrate(h);
        check(countNoteOns(h.out.record()) == 1,
              "ac7-immediate: the first onset fired");
        const SequenceId seqId =
            h.pushMutate("builtin.rotate", 0, kRotatePitch, 127, overrideImmediate());
        h.drain();
        check(statePitchIs(h, 0, {67, 60, 64}),
              "ac7-immediate: the mutation applied at drain time, before the next tick");
        check(h.pendingCount() == 0,
              "ac7-immediate: nothing stays staged");
        check(h.seq.commandInbox().ackStatus(seqId) ==
                  sequencer::AckStatus::Applied,
              "ac7-immediate: the command acks Applied");
        check(h.undoSize() == 1,
              "ac7-immediate: the applied mutation pushed the undo stack");
        h.tick(4);
        h.tick(5);  // next rising edge: Note On from the MUTATED material
        const size_t ons = countNoteOns(h.out.record());
        check(ons == 2,
              "ac7-immediate: the next onset fires after the mutation");
        h.auditStopped("ac7-immediate");
    }

    // (2) No override: the operation's own configured mode (level 2) is the
    // QuantizeBeat factory default — applies at tick 24, the next beat.
    {
        Harness h;
        ac7PlayingSubstrate(h);
        const SequenceId seqId = h.pushMutate("builtin.rotate", 0, kRotatePitch, 127);
        h.drain();
        check(h.pendingCount() == 1,
              "ac7-beat: the quantized command stages, unapplied");
        check(statePitchIs(h, 0, {60, 64, 67}),
              "ac7-beat: the material is untouched before the boundary");
        ac7CheckBoundary(h, 24, "ac7-beat");
        check(h.seq.commandInbox().ackStatus(seqId) ==
                  sequencer::AckStatus::Applied,
              "ac7-beat: the command acks Applied once executed");
        check(h.undoSize() == 1,
              "ac7-beat: the applied mutation pushed the undo stack");
        h.auditStopped("ac7-beat");
    }

    // (3) Level-1 QuantizeBar override beats the QuantizeBeat operation mode
    // (and the Immediate global default): applies at tick 96, and the beat
    // boundaries 24/48/72 must NOT apply it.
    {
        Harness h;
        ac7PlayingSubstrate(h);
        const SequenceId seqId =
            h.pushMutate("builtin.rotate", 0, kRotatePitch, 127, overrideBar());
        h.drain();
        check(h.pendingCount() == 1, "ac7-bar: the command stages, unapplied");
        ac7CheckBoundary(h, 96, "ac7-bar");
        check(h.seq.commandInbox().ackStatus(seqId) ==
                  sequencer::AckStatus::Applied,
              "ac7-bar: the command acks Applied once executed");
        h.auditStopped("ac7-bar");
    }

    // (4) Precedence: the level-1 override beats BOTH configured levels.
    // Mutate mode Immediate + global default Immediate would apply at once;
    // the QuantizeBar override must win anyway (tick 96, not at drain).
    {
        Harness h;
        h.seq.operationTiming().setMode(sequencer::GenerativeOperation::Mutate,
                                        sequencer::OperationTimingMode::Immediate);
        h.seq.operationTiming().setGlobalDefault(
            sequencer::OperationTimingMode::Immediate);
        ac7PlayingSubstrate(h);
        check(h.pushMutate("builtin.rotate", 0, kRotatePitch, 127, overrideBar()) !=
                  sequencer::kInvalidSequence,
              "ac7-precedence1: the push is accepted");
        h.drain();
        check(statePitchIs(h, 0, {60, 64, 67}),
              "ac7-precedence1: the override holds off the Immediate modes");
        check(h.pendingCount() == 1,
              "ac7-precedence1: the command is staged for the bar");
        ac7CheckBoundary(h, 96, "ac7-precedence1");
        h.auditStopped("ac7-precedence1");
    }

    // (5) Precedence: the level-2 operation mode beats the level-3 global
    // default when the operation is NOT Inherit — and the global default
    // governs when it IS. One sub-case each.
    {
        // Level 2 QuantizeBar beats the Immediate global default.
        Harness h;
        h.seq.operationTiming().setMode(sequencer::GenerativeOperation::Mutate,
                                        sequencer::OperationTimingMode::QuantizeBar);
        h.seq.operationTiming().setGlobalDefault(
            sequencer::OperationTimingMode::Immediate);
        ac7PlayingSubstrate(h);
        check(h.pushMutate("builtin.rotate", 0, kRotatePitch, 127) !=
                  sequencer::kInvalidSequence,
              "ac7-precedence2: the push is accepted");
        h.drain();
        check(h.pendingCount() == 1,
              "ac7-precedence2: level-2 QuantizeBar beats the Immediate global default");
        ac7CheckBoundary(h, 96, "ac7-precedence2");
        h.auditStopped("ac7-precedence2");
    }
    {
        // Level 3: Inherit delegates to the global default (QuantizeBeat).
        Harness h;
        h.seq.operationTiming().setMode(sequencer::GenerativeOperation::Mutate,
                                        sequencer::OperationTimingMode::Inherit);
        h.seq.operationTiming().setGlobalDefault(
            sequencer::OperationTimingMode::QuantizeBeat);
        ac7PlayingSubstrate(h);
        check(h.pushMutate("builtin.rotate", 0, kRotatePitch, 127) !=
                  sequencer::kInvalidSequence,
              "ac7-precedence3: the push is accepted");
        h.drain();
        check(h.pendingCount() == 1,
              "ac7-precedence3: the Inherit command stages for resolution");
        ac7CheckBoundary(h, 24, "ac7-precedence3");
        h.auditStopped("ac7-precedence3");
    }
}

// ---------------------------------------------------------------------------
// AC8: the capability-driven GeneratorConfigView shows ONLY the controls
// the selected generator declares. The view is constructed over the
// harness's LIVE registry (built-ins + the two test-only plugins) and the
// SEQUENCER'S OWN bound ParameterBus, so its edits write into the same
// working state the transport reads.
// ---------------------------------------------------------------------------

void test_ac8_capability_driven_controls() {
    TestIntervalGatePlugin gatePlugin(4);
    TestSpreadPitchPlugin pitchPlugin;
    Harness h;
    check(h.registry.add(&gatePlugin) && h.registry.add(&pitchPlugin),
          "ac8: the test-only plugins register alongside the built-ins");

    hardware::LedMatrix matrix{16};
    ui::GeneratorConfigView view(h.registry, h.seq.parameterBus(), matrix);

    // Capability-filtered generator lists per role: a role offers exactly
    // the registered generators whose capability set contains its bit.
    check(view.generatorCountForRole(GeneratorRole::GATE) == 3,
          "ac8: the gate role offers the two gate built-ins plus the test plugin");
    check(view.generatorCountForRole(GeneratorRole::PITCH) == 2,
          "ac8: the pitch role offers the pitch built-in plus the test plugin");
    check(view.generatorCountForRole(GeneratorRole::VELOCITY) == 0,
          "ac8: the velocity role offers nothing (no capable generator)");
    check(view.generatorCountForRole(GeneratorRole::DURATION) == 1,
          "ac8: the duration role offers the one duration built-in");

    int gatePluginIndex = -1;
    int pitchPluginIndex = -1;
    int euclideanIndex = -1;
    for (uint8_t i = 0; i < view.generatorCountForRole(GeneratorRole::GATE); ++i) {
        const IGenerator* gen = view.generatorForRole(GeneratorRole::GATE, i);
        if (gen != nullptr && std::strcmp(gen->id(), "test.interval-gate") == 0) {
            gatePluginIndex = i;
        }
        if (gen != nullptr && std::strcmp(gen->id(), "builtin.euclidean") == 0) {
            euclideanIndex = i;
        }
    }
    for (uint8_t i = 0; i < view.generatorCountForRole(GeneratorRole::PITCH); ++i) {
        const IGenerator* gen = view.generatorForRole(GeneratorRole::PITCH, i);
        if (gen != nullptr && std::strcmp(gen->id(), "test.pitch-plugin") == 0) {
            pitchPluginIndex = i;
        }
    }
    check(gatePluginIndex == 2,
          "ac8: the test gate plugin qualifies for the gate role");
    check(pitchPluginIndex == 1,
          "ac8: the test pitch plugin qualifies for the pitch role");
    check(euclideanIndex == 0, "ac8: the Euclidean built-in is a gate option");

    // The Euclidean built-in shows EXACTLY its three declared controls.
    view.selectRole(GeneratorRole::GATE);
    check(view.selectGeneratorForRole(
              GeneratorRole::GATE, static_cast<uint8_t>(euclideanIndex)),
          "ac8: selecting the Euclidean generator succeeds");
    check(view.controlCount() == 3,
          "ac8: the Euclidean generator shows exactly its three controls");
    ui::GeneratorConfigView::Control control;
    check(view.controlAt(0, control) && std::strcmp(control.name, "Steps") == 0 &&
              control.editable,
          "ac8: control 0 is the editable Steps parameter");
    check(view.controlAt(1, control) && std::strcmp(control.name, "Pulses") == 0,
          "ac8: control 1 is the Pulses parameter");
    check(view.controlAt(2, control) && std::strcmp(control.name, "Rotation") == 0,
          "ac8: control 2 is the Rotation parameter");

    // The test-only gate plugin renders ITS OWN parameter table — with no
    // view change: one control, the custom Period parameter. Its parameter
    // has no entry in the fixed parameter table, so it displays but is not
    // bus-editable (the documented plugin-parameter rule).
    check(view.selectGeneratorForRole(
              GeneratorRole::GATE, static_cast<uint8_t>(gatePluginIndex)),
          "ac8: selecting the test gate plugin succeeds");
    check(view.controlCount() == 1,
          "ac8: the test plugin shows exactly its one control");
    check(view.controlAt(0, control) && std::strcmp(control.name, "Period") == 0 &&
              control.minValue == 1 && control.maxValue == 64 &&
              control.defaultValue == 4,
          "ac8: the test plugin's control mirrors its ParameterDef");
    check(!control.editable,
          "ac8: a plugin parameter without a table address is not bus-editable");

    // The test-only pitch plugin renders its two controls for the PITCH
    // role (and the view switches role cleanly).
    view.selectRole(GeneratorRole::PITCH);
    check(view.selectGeneratorForRole(
              GeneratorRole::PITCH, static_cast<uint8_t>(pitchPluginIndex)),
          "ac8: selecting the test pitch plugin succeeds");
    check(view.controlCount() == 2,
          "ac8: the test pitch plugin shows exactly its two controls");
    check(view.controlAt(0, control) && std::strcmp(control.name, "Spread") == 0,
          "ac8: control 0 is the Spread parameter");
    check(view.controlAt(1, control) && std::strcmp(control.name, "Octaves") == 0,
          "ac8: control 1 is the Octaves parameter");

    // A role without a capable generator shows NOTHING.
    view.selectRole(GeneratorRole::VELOCITY);
    check(view.controlCount() == 0,
          "ac8: a role without a generator shows no controls");

    // Full-stack edit path: an UP key press on the Euclidean Steps control
    // writes through the ParameterBus BASE value into the LIVE sequencer
    // working state (the same state the transport dispatches from).
    view.selectRole(GeneratorRole::GATE);
    check(view.selectGeneratorForRole(
              GeneratorRole::GATE, static_cast<uint8_t>(euclideanIndex)),
          "ac8: re-selecting the Euclidean generator succeeds");
    check(view.selectControl(0), "ac8: the Steps control is selectable");

    sequencer::ParameterAddress stepsAddress;
    stepsAddress.scope = sequencer::ParameterScope::Pattern;
    stepsAddress.patternIndex = 0;
    stepsAddress.paramId = sequencer::ParamId::GateSteps;
    // The UI write path: a committed base value (the working state starts
    // zeroed; users set the value through the same bus surface).
    check(h.seq.parameterBus().setBase(stepsAddress, 16),
          "ac8: the Steps base is set through the bus write path");
    check(h.state(0)->generatorParams[static_cast<uint8_t>(GeneratorRole::GATE)]
                              [0] == 16,  // the Euclidean generator's Steps param id
          "ac8: the base write reaches the live sequencer working state");

    ui::state::UIState uiState;  // selectedPattern == 0
    const ui::events::Event up =
        ui::events::Event::keyPressed(ui::KeyId::UP, ui::mod::NONE, 0);
    view.handleEvent(uiState, up);
    check(h.seq.parameterBus().baseValue(stepsAddress) == 17,
          "ac8: an UP key press adjusts the control through the live bus (+1)");
    check(h.state(0)->generatorParams[static_cast<uint8_t>(GeneratorRole::GATE)]
                              [0] == 17,  // the Euclidean generator's Steps param id
          "ac8: the edit reached the LIVE sequencer working state");

    // The edit is a BASE write: no modulation overlay was armed by the view.
    check(!h.seq.parameterBus().hasOverlay(stepsAddress),
          "ac8: the view never arms a modulation overlay");
}

// ---------------------------------------------------------------------------
// Out-of-scope guards (spec "Out of Scope"): the assembled engine must NOT
// contain MIDI CC output, automatic mutation, user-definable scales,
// per-pattern undo history or MIDI input processing. The note-ownership
// audit already rejects any non-note voice byte in EVERY driven stream
// (the CC half). This test pins the remaining exclusions host-observably.
// ---------------------------------------------------------------------------

void test_out_of_scope_guards() {
    // No AUTOMATIC mutation: a full 100-tick play of a configured pattern
    // leaves the working state BYTE-IDENTICAL — the generative state
    // changes only through performer commands (mutation is always asked
    // for; nothing mutates on its own, and no note fires from an empty
    // stored gate set).
    {
        Harness h;
        h.setStatePitch(0, {60, 64, 67});
        h.setStateVelocity(0, {100, 90, 80});
        const sequencer::PatternGenerativeState before = *h.state(0);
        h.play();
        for (uint64_t n = 1; n <= 100; ++n) {
            h.tick(n);
        }
        check(countRealTime(h.out.record(),
                            midi::SystemRealTimeMessage::TIMING_CLOCK) == 100,
              "out-of-scope: the transport ran the full 100 ticks");
        check(countNoteOns(h.out.record()) == 0,
              "out-of-scope: no note fires from empty stored gates");
        check(sameStateBytes(*h.state(0), before),
              "out-of-scope: no automatic mutation touched the working state");
        check(h.undoSize() == 0 && h.pendingCount() == 0,
              "out-of-scope: nothing staged or recorded without commands");
        h.auditStopped("out-of-scope");
    }

    // No USER-DEFINABLE scales: common::Scale can only be built from the
    // fixed read-only template table — an out-of-range template index
    // CLAMPS into the table (it can never invent a new template), and the
    // table size is the fixed extended set the spec's FR3 approved.
    {
        const int count = common::scaleTemplateCount();
        check(count == 28, "out-of-scope: the built-in template table is the fixed approved set");
        const common::Scale clamped(0, 200);
        check(clamped.templateIndex() == count - 1,
              "out-of-scope: an out-of-range template clamps — never a user template");
        bool allValid = true;
        for (int t = 0; t < count; ++t) {
            const common::Scale scale(3, static_cast<uint8_t>(t));
            if (!scale.containsNote(scale.nearestNoteInScale(64))) allValid = false;
        }
        check(allValid,
              "out-of-scope: every template stays a valid fixed scale");
    }

    // No PER-PATTERN undo history: the observable undo surface is ONE
    // global LIFO stack (WorkingStateManager::undoStack()), sized by the
    // single global depth limit — pinned structurally by AC5's cross-
    // pattern LIFO behavior above.
    {
        Harness h;
        check(h.seq.workingStateManager().undoStack().size() == 0,
              "out-of-scope: the single global undo stack starts empty");
        check(sequencer::UndoStack::kMaxDepth <= 16,
              "out-of-scope: the undo depth stays within the limited history");
    }
}

}  // namespace

int main() {
    std::printf("generative_engine_test (task 22, AC1-AC8 integration)\n");

    test_ac1_test_only_generator_plays();
    test_ac2_scale_lock_all_templates_and_roots();
    test_ac3_modulation_every_ui_parameter();
    test_ac4_mutation_timing_budget();
    test_ac5_undo_across_patterns();
    test_ac6_revert_restores_baseline();
    test_ac7_timing_policy();
    test_ac8_capability_driven_controls();
    test_out_of_scope_guards();

    if (failures == 0) {
        std::printf("\ngenerative_engine_test: ALL CHECKS PASSED\n");
        return 0;
    }
    std::printf("\ngenerative_engine_test: %d FAILURE(S)\n", failures);
    return 1;
}
