// generative-engine-expansion task 7 — note duration generator plugin
// (FR2, AC1).
//
// The duration generator producing per-step note lengths in transport
// ticks: a fixed BASE duration varied around itself by a 0-127 variation
// amount, drawn from the SAME deterministic, seedable GeneratorRng the
// other plugins draw with, and hard-clamped into the configured
// min/max tick window so every decision is a well-formed note length:
//   - variation 0  -> the constant base duration (no RNG draw at all);
//   - variation v  -> one uniform byte draw per step scaled to
//     v/127 of the min..max spread, offset from the base, then clamped
//     into [min, max].
//
// The durations must stay compatible with the Milestone 2 per-pattern
// note ownership: the sequencer arms a bounded countdown at Note On and
// releases the recorded onset exactly once when it crosses zero — so every
// Note On gets exactly one Note Off, no hung notes, no duplicate offs.
//
// Validates:
//   1. every generated duration stays within the configured min/max
//      ticks, over many steps, seeds and configurations;
//   2. variation 0 gives the constant base duration for every step/seed
//      (and consumes no RNG entropy);
//   3. the output is deterministic per seed: identical (seed, step)
//      inputs replay identical durations, a different seed produces a
//      different stream somewhere;
//   4. with variation 127 the durations actually vary — both the
//      configured minimum and maximum tick values are reached;
//   5. capabilities are DURATION only (AC8: the UI shows just the
//      duration controls) — no GATE, no PITCH, no VELOCITY — and
//      generate() writes ONLY the duration decision;
//   6. paramDefs expose the four parameters (base/min/max length in
//      ticks, variation), with their ranges, factory defaults and hints;
//   7. identity and configure() totality: out-of-range values clamp, a
//      reversed min/max range collapses, a base outside [min, max] is
//      pulled inside;
//   8. registration: registerGenerators() (RegisterGenerators.cpp, the
//      ONLY built-in listing, AC1) registers the plugin under
//      "builtin.duration" with the factory configuration, alongside the
//      three earlier built-ins; repeated registration adds nothing; the
//      id fits PatternGenerativeState's fixed generator-id storage;
//   9. a sequencer-level check: driving the REAL Sequencer with the
//      real duration generator assigned to the DURATION role, every
//      Note On still gets exactly one Note Off — the release boundary
//      crossing disarms the countdown so a later falling edge cannot
//      double-release, and a falling-edge release disarms a pending
//      countdown so it cannot fire twice either.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_note_duration_generator".

#include "FakeMonotonicClock.h"
#include "GeneratorRegistry.h"
#include "IGenerator.h"
#include "PatternGenerativeState.h"
#include "sequencer.h"
#include "command.h"
#include "midi_messages.h"
#include "sequencer/generators/NoteDurationGenerator.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

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
using sequencer::GeneratorParamHint;
using sequencer::GeneratorRole;
using sequencer::GeneratorRng;
using sequencer::IGenerator;
using sequencer::NoteDurationGenerator;

// The duration decision the generator produces at `stepIndex`, with a
// FRESH rng carrying `seed` — mirroring the sequencer's per-(pattern,
// role, step) reseeding so identical inputs replay identical outputs.
uint16_t generatedDurationAt(const IGenerator& gen, uint8_t patternLength,
                             uint32_t stepIndex, uint32_t seed) {
    GeneratorRng rng(seed);
    GeneratorContext context(patternLength, nullptr, rng);
    GeneratorOutput output;
    gen.generate(stepIndex, context, output);
    check(output.hasDuration,
          "the note duration generator always produces a duration decision");
    return output.duration;
}

// --- 1: durations stay within the configured min/max ticks --------------------

void test_durations_stay_within_bounds() {
    const struct {
        uint16_t minTicks;
        uint16_t maxTicks;
        uint16_t baseTicks;
        uint8_t variation;
        const char* what;
    } configs[] = {
        {1, 255, 128, 127, "full tick range, full variation"},
        {2, 40, 21, 127, "a mid window, full variation"},
        {1, 12, 6, 127, "a short window, full variation"},
        {1, 255, 1, 64, "base at the minimum, mid variation"},
        {24, 24, 24, 0, "a degenerate single-tick window"},
        {3, 9, 5, 200, "a raw variation above 127 (clamped)"},
    };

    for (const auto& c : configs) {
        NoteDurationGenerator plugin;
        plugin.configure(c.minTicks, c.maxTicks, c.baseTicks, c.variation);
        const IGenerator& byBase = plugin;

        bool inBounds = true;
        for (uint32_t step = 0; step < 1000; ++step) {
            const uint16_t d = generatedDurationAt(byBase, 16, step, 7000u + step);
            if (d < plugin.minTicks() || d > plugin.maxTicks()) inBounds = false;
        }
        check(inBounds, c.what);
    }

    // The invariant holds across seed bases too (not just one stream).
    NoteDurationGenerator plugin;
    plugin.configure(2, 40, 21, 127);
    const IGenerator& byBase = plugin;
    bool inBounds = true;
    for (uint32_t base = 0; base < 8; ++base) {
        for (uint32_t step = 0; step < 200; ++step) {
            const uint16_t d = generatedDurationAt(byBase, 16, step,
                                                   31u + base * 977u + step);
            if (d < 2 || d > 40) inBounds = false;
        }
    }
    check(inBounds, "every duration stays inside the configured window across seeds");
}

// --- 2: variation 0 gives the constant base duration ---------------------------

void test_variation_zero_is_constant_base() {
    // (a) The factory configuration (variation 0): every step/seed gets the
    // factory base duration, and no RNG entropy is consumed at all.
    const NoteDurationGenerator factory;
    const IGenerator& byBase = factory;

    bool constant = true;
    bool entropyUntouched = true;
    for (uint32_t step = 0; step < 512; ++step) {
        GeneratorRng rng(999u + step);
        const uint64_t stateBefore = rng.state;
        GeneratorContext context(16, nullptr, rng);
        GeneratorOutput output;
        byBase.generate(step, context, output);
        if (!output.hasDuration || output.duration != factory.baseTicks()) constant = false;
        if (rng.state != stateBefore) entropyUntouched = false;
    }
    check(constant, "variation 0 gives the constant base duration at every step");
    check(entropyUntouched, "variation 0 consumes no RNG entropy (a constant needs no draw)");

    // (b) A configured non-factory base with variation 0: constant too.
    NoteDurationGenerator configured;
    configured.configure(4, 96, 33, 0);
    bool constantConfigured = true;
    for (uint32_t step = 0; step < 256; ++step) {
        if (generatedDurationAt(configured, 16, step, 12345u + step) != 33) {
            constantConfigured = false;
        }
    }
    check(constantConfigured, "variation 0 keeps a configured base constant");

    // (c) A degenerate single-value window (min == max) is constant even
    // with variation requested — there is no room to vary.
    NoteDurationGenerator degenerate;
    degenerate.configure(7, 7, 7, 127);
    bool constantDegenerate = true;
    bool degenerateEntropyUntouched = true;
    for (uint32_t step = 0; step < 256; ++step) {
        GeneratorRng rng(4242u + step);
        const uint64_t stateBefore = rng.state;
        GeneratorContext context(16, nullptr, rng);
        GeneratorOutput output;
        degenerate.generate(step, context, output);
        if (output.duration != 7) constantDegenerate = false;
        if (rng.state != stateBefore) degenerateEntropyUntouched = false;
    }
    check(constantDegenerate, "min == max yields the constant single value");
    check(degenerateEntropyUntouched,
          "a degenerate window consumes no RNG entropy");
}

// --- 3: deterministic per seed -----------------------------------------------

void test_deterministic_per_seed() {
    NoteDurationGenerator plugin;
    plugin.configure(2, 40, 21, 64);
    const IGenerator& byBase = plugin;

    // Two independent sweeps with identical (seed, step) inputs replay
    // identical durations.
    bool replayIdentical = true;
    for (uint32_t step = 0; step < 256; ++step) {
        const uint32_t seed = 555u + step;
        if (generatedDurationAt(byBase, 16, step, seed) !=
            generatedDurationAt(byBase, 16, step, seed)) {
            replayIdentical = false;
        }
    }
    check(replayIdentical, "the same (seed, step) produces the same duration");

    // A different seed base produces a different stream somewhere (the
    // RNG genuinely drives the decision; a fixed seed is what makes it
    // deterministic, not a constant answer).
    bool differsSomewhere = false;
    for (uint32_t step = 0; step < 256; ++step) {
        if (generatedDurationAt(byBase, 16, step, 555u + step) !=
            generatedDurationAt(byBase, 16, step, 777u + step)) {
            differsSomewhere = true;
        }
    }
    check(differsSomewhere, "a different seed produces a different duration stream");
}

// --- 4: variation actually varies ---------------------------------------------

void test_variation_actually_varies() {
    NoteDurationGenerator plugin;
    plugin.configure(2, 40, 21, 127);
    const IGenerator& byBase = plugin;

    constexpr int kSteps = 4000;
    uint16_t observedMin = 40;
    uint16_t observedMax = 2;
    int distinct = 0;
    bool seen[256] = {};
    for (uint32_t step = 0; step < kSteps; ++step) {
        const uint16_t d = generatedDurationAt(byBase, 16, step, 1000u + step);
        if (d < observedMin) observedMin = d;
        if (d > observedMax) observedMax = d;
        if (!seen[d]) { seen[d] = true; ++distinct; }
    }
    check(distinct > 1, "variation 127 produces more than one distinct duration");
    check(observedMin == 2, "the configured minimum tick value is reached");
    check(observedMax == 40, "the configured maximum tick value is reached");
}

// --- 5: capabilities are DURATION only (AC8) -----------------------------------

void test_capabilities_duration_only() {
    const NoteDurationGenerator plugin;
    const GeneratorCapabilities caps = plugin.capabilities();
    check(caps.has(GeneratorCapability::DURATION),
          "the generator declares DURATION");
    check(!caps.has(GeneratorCapability::GATE), "the generator declares no GATE");
    check(!caps.has(GeneratorCapability::PITCH), "the generator declares no PITCH");
    check(!caps.has(GeneratorCapability::VELOCITY),
          "the generator declares no VELOCITY");
    check(caps.mask() == static_cast<uint8_t>(GeneratorCapability::DURATION),
          "the capability set is exactly DURATION");

    // generate() writes ONLY the duration decision.
    GeneratorRng rng(42u);
    GeneratorContext context(16, nullptr, rng);
    GeneratorOutput output;
    // Poison the optional fields so a stale value would be caught.
    output.hasGate = true;
    output.gate = true;
    output.hasPitch = true;
    output.pitch = 60;
    output.hasVelocity = true;
    output.velocity = 100;

    plugin.generate(3, context, output);

    check(output.hasDuration, "generate() sets hasDuration");
    check(output.hasGate == false, "generate() never claims a gate");
    check(output.hasPitch == false, "generate() never claims a pitch");
    check(output.hasVelocity == false, "generate() never claims a velocity");
    check(output.duration >= plugin.minTicks() && output.duration <= plugin.maxTicks(),
          "generate() emits a duration inside the configured window");
}

// --- 6: the fixed parameter table ----------------------------------------------

void test_param_defs() {
    const NoteDurationGenerator plugin;
    const sequencer::GeneratorParamSet& defs = plugin.paramDefs();

    check(defs.count == 4, "the generator exposes four parameters");
    check(defs.defs[0].id == NoteDurationGenerator::kParamBaseLength,
          "the base length parameter carries id 0");
    check(std::strcmp(defs.defs[0].name, "Base Length") == 0,
          "the base length parameter is named");
    check(defs.defs[0].minValue == NoteDurationGenerator::kMinDurationTicks &&
              defs.defs[0].maxValue == NoteDurationGenerator::kMaxDurationTicks,
          "the base length range is the full tick window");
    check(defs.defs[0].defaultValue == NoteDurationGenerator::kDefaultBaseTicks,
          "the factory base length is one beat at PPQN 24 (24 ticks)");
    check(defs.defs[0].hint == GeneratorParamHint::Steps,
          "the base length parameter carries the Steps hint");

    check(defs.defs[1].id == NoteDurationGenerator::kParamMinLength,
          "the min length parameter carries id 1");
    check(std::strcmp(defs.defs[1].name, "Min Length") == 0,
          "the min length parameter is named");
    check(defs.defs[1].minValue == NoteDurationGenerator::kMinDurationTicks &&
              defs.defs[1].maxValue == NoteDurationGenerator::kMaxDurationTicks,
          "the min length range is the full tick window");
    check(defs.defs[1].defaultValue == NoteDurationGenerator::kDefaultMinTicks,
          "the factory min length is 1 tick");
    check(defs.defs[1].hint == GeneratorParamHint::Steps,
          "the min length parameter carries the Steps hint");

    check(defs.defs[2].id == NoteDurationGenerator::kParamMaxLength,
          "the max length parameter carries id 2");
    check(std::strcmp(defs.defs[2].name, "Max Length") == 0,
          "the max length parameter is named");
    check(defs.defs[2].minValue == NoteDurationGenerator::kMinDurationTicks &&
              defs.defs[2].maxValue == NoteDurationGenerator::kMaxDurationTicks,
          "the max length range is the full tick window");
    check(defs.defs[2].defaultValue == NoteDurationGenerator::kDefaultMaxTicks,
          "the factory max length is two beats at PPQN 24 (48 ticks)");
    check(defs.defs[2].hint == GeneratorParamHint::Steps,
          "the max length parameter carries the Steps hint");

    check(defs.defs[3].id == NoteDurationGenerator::kParamVariation,
          "the variation parameter carries id 3");
    check(std::strcmp(defs.defs[3].name, "Variation") == 0,
          "the variation parameter is named");
    check(defs.defs[3].minValue == 0 && defs.defs[3].maxValue == 127,
          "the variation range is 0..127");
    check(defs.defs[3].defaultValue == 0,
          "the factory variation is 0 (stable by default)");
    check(defs.defs[3].hint == GeneratorParamHint::Percent,
          "the variation parameter carries the Percent hint");

    for (int i = 4; i < sequencer::kMaxGeneratorParams; ++i) {
        check(defs.defs[i].id == 0 && defs.defs[i].name[0] == '\0',
              "unused parameter slots stay zeroed");
    }
}

// --- 7: identity, configuration and totality -----------------------------------

void test_identity_and_config() {
    const NoteDurationGenerator plugin;
    check(std::strcmp(plugin.id(), "builtin.duration") == 0,
          "the id is builtin.duration");
    check(plugin.name()[0] != '\0', "the display name is non-empty");

    // configure() stores in-range values.
    NoteDurationGenerator reconfigured;
    reconfigured.configure(4, 9, 6, 50);
    check(reconfigured.minTicks() == 4 && reconfigured.maxTicks() == 9 &&
              reconfigured.baseTicks() == 6 && reconfigured.variation() == 50,
          "configure() stores the configuration");

    // configure() is total: raw tick values outside 1..255 clamp.
    NoteDurationGenerator clamped;
    clamped.configure(0, 300, 0, 200);
    check(clamped.minTicks() == 1 && clamped.maxTicks() == 255 &&
              clamped.baseTicks() == 1 && clamped.variation() == 127,
          "configure() clamps tick values into 1..255 and variation into 0..127");

    // A reversed min/max range collapses to the minimum.
    NoteDurationGenerator reversed;
    reversed.configure(9, 4, 6, 50);
    check(reversed.minTicks() == 9 && reversed.maxTicks() == 9,
          "a reversed min/max range collapses to the minimum");
    check(reversed.baseTicks() == 9,
          "the base is pulled inside the collapsed window");

    // A base outside [min, max] is pulled inside, from either side.
    NoteDurationGenerator above;
    above.configure(4, 9, 40, 10);
    check(above.baseTicks() == 9, "a base above the maximum clamps to the maximum");
    NoteDurationGenerator below;
    below.configure(4, 9, 2, 10);
    check(below.baseTicks() == 4, "a base below the minimum clamps to the minimum");

    // The typed ctor matches configure().
    const NoteDurationGenerator typed(4, 9, 6, 50);
    check(typed.minTicks() == 4 && typed.maxTicks() == 9 &&
              typed.baseTicks() == 6 && typed.variation() == 50,
          "the typed ctor matches configure()");

    // The factory defaults are musical and stable: a constant one-beat
    // note (24 ticks at PPQN 24), window 1..48, no variation.
    check(NoteDurationGenerator::kDefaultBaseTicks == 24 &&
              NoteDurationGenerator::kDefaultMinTicks == 1 &&
              NoteDurationGenerator::kDefaultMaxTicks == 48 &&
              NoteDurationGenerator::kDefaultVariation == 0,
          "the factory configuration is the constant one-beat base");
}

// --- 8: registration (AC1: RegisterGenerators.cpp is the only listing) ----------

void test_registration() {
    sequencer::GeneratorRegistry registry;
    sequencer::registerGenerators(registry);

    check(registry.count() == 4,
          "registerGenerators registers the Euclidean gate, the scale-aware "
          "pitch, the probability gate and the note duration generator");
    const IGenerator* found = registry.find("builtin.duration");
    check(found != nullptr, "builtin.duration is reachable by id");
    check(found != nullptr &&
              found->capabilities().has(GeneratorCapability::DURATION),
          "the registered duration generator declares DURATION");
    check(found != nullptr && found->paramDefs().count == 4,
          "the registered duration generator exposes its parameters");
    check(registry.find("builtin.euclidean") != nullptr,
          "the Euclidean gate generator stays registered");
    check(registry.find("builtin.pitch") != nullptr,
          "the scale-aware pitch generator stays registered");
    check(registry.find("builtin.probability") != nullptr,
          "the probability gate generator stays registered");

    // The registered instance carries the FACTORY configuration: its
    // decisions match a locally factory-configured instance bit for bit.
    const NoteDurationGenerator factory;
    bool matches = true;
    for (uint32_t step = 0; step < 128; ++step) {
        if (generatedDurationAt(*found, 16, step, 31337u + step) !=
            generatedDurationAt(factory, 16, step, 31337u + step)) {
            matches = false;
        }
    }
    check(matches, "the registered instance uses the factory configuration");

    // Repeated registration is safe: the duplicates are rejected.
    sequencer::registerGenerators(registry);
    check(registry.count() == 4, "a repeated registerGenerators adds nothing");

    // The id fits PatternGenerativeState's fixed generator-id storage
    // (assignable to the DURATION role).
    sequencer::PatternGenerativeState assigned;
    check(assigned.setGeneratorId(GeneratorRole::DURATION, "builtin.duration"),
          "the plugin id fits the fixed generator id storage");
    check(assigned.hasGenerator(GeneratorRole::DURATION),
          "the duration role accepts the plugin id");
}

// --- 9: sequencer-level — exactly one Note Off per Note On ---------------------

// Test-only fixed-capacity MIDI sink (the same recording double as the
// sibling generator tests), so the sequencer-level stream can be counted.
class FixedMidiOutput final : public sequencer::IMidiOutput {
public:
    static constexpr size_t kCapacity = 4096;

    void write(uint8_t byte) override {
        if (count_ < kCapacity) bytes_[count_++] = byte;
    }

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

private:
    uint8_t bytes_[kCapacity] = {};
    size_t count_ = 0;
};

// Test-only gate fakes (the sequencer resolves them by id through the LOCAL
// registry; the core never names a concrete generator type, AC1).
class FakeHeldGateGenerator final : public IGenerator {
public:
    const char* id() const override { return "fake.gate.held"; }
    const char* name() const override { return "Fake Held Gate"; }
    GeneratorCapabilities capabilities() const override { return GeneratorCapability::GATE; }
    const sequencer::GeneratorParamSet& paramDefs() const override {
        static constexpr sequencer::GeneratorParamSet kNoParams{};
        return kNoParams;
    }
    void generate(uint32_t, const GeneratorContext&,
                  GeneratorOutput& output) const override {
        output = GeneratorOutput{};
        output.hasGate = true;
        output.gate = true;  // held high: only a duration can end the note
    }
};

// High for 6 of every 8 steps, low for 2: repeated note pairs whose releases
// sometimes come from the generated duration (drawn below 6) and sometimes
// from the falling edge (which disarms a still-pending countdown).
class FakePulsedGateGenerator final : public IGenerator {
public:
    const char* id() const override { return "fake.gate.pulsed"; }
    const char* name() const override { return "Fake Pulsed Gate"; }
    GeneratorCapabilities capabilities() const override { return GeneratorCapability::GATE; }
    const sequencer::GeneratorParamSet& paramDefs() const override {
        static constexpr sequencer::GeneratorParamSet kNoParams{};
        return kNoParams;
    }
    void generate(uint32_t stepIndex, const GeneratorContext&,
                  GeneratorOutput& output) const override {
        output = GeneratorOutput{};
        output.hasGate = true;
        output.gate = (stepIndex % 8u) < 6u;
    }
};

// Drives the real Sequencer with a fake clock, a recording sink and a
// LOCAL registry the test populates (the harness the sibling generator
// tests use).
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
    void setPitch(const std::vector<uint8_t>& pitches) {
        commands::CommandMessage m;
        m.cmd = commands::Command::PATTERN_PITCH_SET;
        m.param1 = 0;
        m.pitchCount = static_cast<uint8_t>(pitches.size());
        m.pitches = pitches;
        seq.processCommand(m);
    }
    void setVelocity(const std::vector<uint8_t>& velocities) {
        commands::CommandMessage m;
        m.cmd = commands::Command::PATTERN_VELOCITY_SET;
        m.param1 = 0;
        m.velocityCount = static_cast<uint8_t>(velocities.size());
        m.velocities = velocities;
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
    void tick(uint64_t n) { clock.setMicros(deadline(n)); seq.update(); }
};

// (a) A held gate + the real duration generator: the generated duration is
// the ONLY thing that can end the note, and it ends it exactly once —
// no re-fire while the gate stays high, no second Note Off.
void test_sequencer_held_gate_single_pair() {
    Harness h;
    FakeHeldGateGenerator gateGen;
    NoteDurationGenerator durGen;  // configured to a constant 3-tick note
    durGen.configure(3, 3, 3, 0);
    // The Sequencer constructor auto-registered the built-ins into the
    // harness registry; replace them with the test's fake gate and the
    // REAL duration plugin (configured for this scenario), so the
    // DURATION role resolves through the registry to the real generator.
    h.registry.clear();
    check(h.registry.add(&gateGen), "held-gate fake registers");
    check(h.registry.add(&durGen), "the real duration generator registers");

    h.setGate({false, false, false, false});  // 4-tick stored scaffold (length only)
    h.setPitch({72});
    h.setVelocity({100});
    sequencer::PatternGenerativeState* st = h.seq.generativeState(0);
    check(st != nullptr && st->setGeneratorId(GeneratorRole::GATE, "fake.gate.held"),
          "GATE role assignment accepted");
    check(st != nullptr && st->setGeneratorId(GeneratorRole::DURATION, "builtin.duration"),
          "DURATION role accepts the real plugin id");

    h.play();
    h.tick(1);
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_ON) == 1,
          "the held gate fires one Note On");
    h.tick(2);
    h.tick(3);
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 0,
          "the note still sounds before the generated duration elapses");
    h.tick(4);
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 1,
          "the generated duration (3 ticks) ends the note on the boundary");
    h.tick(5);
    h.tick(6);
    h.tick(7);
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_ON) == 1,
          "a held gate produces no re-fire after the duration release");
    check(h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF) == 1,
          "exactly one Note Off per Note On — no duplicate release");
}

// (b) Repeated note pairs: a pulsed gate whose 6-step high phase is
// sometimes outlived by the generated duration (min 1 .. max 12 with full
// variation). Every onset gets exactly one release — whichever of the two
// release paths fires first wins, and the loser is a no-op.
void test_sequencer_pulsed_pairs_exactly_one_off_per_on() {
    Harness h;
    FakePulsedGateGenerator gateGen;
    NoteDurationGenerator durGen;
    durGen.configure(1, 12, 6, 127);  // full variation inside a window spanning the gate phase
    h.registry.clear();  // replace the auto-registered built-ins (see above)
    h.registry.add(&gateGen);
    h.registry.add(&durGen);

    h.setGate(std::vector<bool>(8, false));  // 8-tick stored scaffold (length only)
    h.setPitch({72});
    h.setVelocity({100});
    sequencer::PatternGenerativeState* st = h.seq.generativeState(0);
    st->setGeneratorId(GeneratorRole::GATE, "fake.gate.pulsed");
    st->setGeneratorId(GeneratorRole::DURATION, "builtin.duration");

    h.play();
    // 96 ticks = 12 full 8-step periods. tick(n) services step n-1; a
    // settle point is a step ≡ 7 (mod 8): the gate has been low for two
    // steps, so every onset of the period is released (by its duration
    // countdown or by the falling edge) and the counts must balance.
    bool prefixNeverOver = true;
    bool balancedAtEverySettle = true;
    for (uint64_t n = 1; n <= 96; ++n) {
        h.tick(n);
        const int on = h.out.countVoice(midi::ChannelVoiceMessage::NOTE_ON);
        const int off = h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF);
        if (off > on) prefixNeverOver = false;  // never more offs than ons
        if (n % 8 == 0 && off != on) balancedAtEverySettle = false;
    }
    check(prefixNeverOver, "Note Offs never outnumber Note Ons at any tick");
    check(balancedAtEverySettle,
          "at every settle point exactly one Note Off has arrived per Note On");

    const int totalOn = h.out.countVoice(midi::ChannelVoiceMessage::NOTE_ON);
    const int totalOff = h.out.countVoice(midi::ChannelVoiceMessage::NOTE_OFF);
    check(totalOn == 12, "the pulsed gate fired one Note On per period (12)");
    check(totalOff == 12,
          "12 Note Ons received exactly 12 Note Offs — no hung notes, no duplicates");
}

}  // namespace

int main() {
    test_durations_stay_within_bounds();
    test_variation_zero_is_constant_base();
    test_deterministic_per_seed();
    test_variation_actually_varies();
    test_capabilities_duration_only();
    test_param_defs();
    test_identity_and_config();
    test_registration();
    test_sequencer_held_gate_single_pair();
    test_sequencer_pulsed_pairs_exactly_one_off_per_on();

    if (failures == 0) {
        std::printf("test_note_duration_generator: all checks passed\n");
        return 0;
    }
    std::printf("test_note_duration_generator: %d check(s) failed\n", failures);
    return 1;
}
