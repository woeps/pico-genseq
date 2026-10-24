// generative-engine-expansion task 6 — probability gate generator plugin
// (FR2, AC1).
//
// The gate generator whose per-step decision is a Bernoulli draw with the
// configured probability (0-127), taken from the SAME deterministic,
// seedable GeneratorRng the scale-aware pitch generator (T5) draws with:
// one uniform draw over the 127 probability levels per step, gate iff the
// draw is strictly below the configured probability — so
//   - probability 0   -> no level passes -> silence everywhere;
//   - probability 127 -> every level passes -> certainty everywhere;
//   - probability p   -> long-run density p/127, so 64 is roughly half.
//
// Validates:
//   1. probability 0 produces no gates, over many steps and seeds;
//   2. probability 127 produces ALL gates, over many steps and seeds;
//   3. around 64 (also 32 and 96, pinning the p/127 law) the long-run
//      density is roughly p/127 for a fixed seed base — roughly half at 64
//      over many steps;
//   4. the same seed produces the same output (determinism: identical
//      (seed, step) inputs replay identical decisions), while a different
//      seed base produces a different stream somewhere (the RNG actually
//      drives the decision);
//   5. capabilities are GATE only (AC8: the UI shows just the probability
//      control) — no PITCH, no VELOCITY, no DURATION;
//   6. generate() writes ONLY the gate decision: hasGate always true,
//      hasPitch / hasVelocity / hasDuration always false (a gate-only
//      generator never disturbs stored pitch material);
//   7. paramDefs expose the single probability parameter (id 0, range
//      0-127, factory default 64, Percent hint);
//   8. configure() is total: raw values above 127 clamp to 127;
//   9. registration: registerGenerators() (RegisterGenerators.cpp, the
//      ONLY built-in listing, AC1) registers the plugin under
//      "builtin.probability" with the factory configuration, alongside the
//      Euclidean gate and scale-aware pitch generators; repeated
//      registration adds nothing; the not-yet-registered T7 built-in
//      ("builtin.duration") stays unreachable; the id fits
//      PatternGenerativeState's fixed generator-id storage.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_probability_gate_generator".

#include "GeneratorCapabilities.h"
#include "GeneratorRegistry.h"
#include "IGenerator.h"
#include "sequencer/PatternGenerativeState.h"
#include "sequencer/generators/ProbabilityGateGenerator.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

using sequencer::GeneratorCapability;
using sequencer::GeneratorCapabilities;
using sequencer::GeneratorContext;
using sequencer::GeneratorOutput;
using sequencer::GeneratorParamHint;
using sequencer::GeneratorRng;

// The gate decision the generator produces at `stepIndex`, with a FRESH
// rng carrying `seed` — mirroring the sequencer's per-(pattern, role, step)
// reseeding so identical inputs replay identical outputs.
bool generatedGateAt(const sequencer::IGenerator& gen, uint8_t patternLength,
                     uint32_t stepIndex, uint32_t seed) {
    GeneratorRng rng(seed);
    GeneratorContext context(patternLength, nullptr, rng);
    GeneratorOutput output;
    gen.generate(stepIndex, context, output);
    check(output.hasGate, "the probability gate generator always produces a gate decision");
    return output.gate;
}

// --- 1: probability 0 produces no gates ------------------------------------

void test_probability_zero_produces_no_gates() {
    const sequencer::ProbabilityGateGenerator plugin;  // factory: p = 64
    sequencer::ProbabilityGateGenerator silent;
    silent.configure(0);
    const sequencer::IGenerator& byBase = silent;

    bool anyGate = false;
    for (uint32_t step = 0; step < 256; ++step) {
        if (generatedGateAt(byBase, 16, step, 1234u + step)) anyGate = true;
    }
    check(!anyGate, "probability 0 produces no gates over many steps and seeds");

    // The zero configuration is reachable through the typed ctor too.
    const sequencer::ProbabilityGateGenerator typed(0);
    check(generatedGateAt(typed, 16, 0, 99u) == false,
          "the typed ctor stores the zero probability");
    check(silent.probability() == 0, "probability() reports 0");
}

// --- 2: probability 127 produces all gates ----------------------------------

void test_probability_max_produces_all_gates() {
    sequencer::ProbabilityGateGenerator certain;
    certain.configure(127);
    const sequencer::IGenerator& byBase = certain;

    bool allGates = true;
    for (uint32_t step = 0; step < 256; ++step) {
        if (!generatedGateAt(byBase, 16, step, 4321u + step)) allGates = false;
    }
    check(allGates, "probability 127 produces ALL gates over many steps and seeds");

    const sequencer::ProbabilityGateGenerator typed(127);
    check(generatedGateAt(typed, 16, 0, 99u),
          "the typed ctor stores the maximum probability");
    check(certain.probability() == 127, "probability() reports 127");
}

// --- 3: around 64 the density is roughly half (the p/127 law) ----------------

void test_density_follows_the_probability() {
    // Many steps with a fixed seed base (seed = base + step, the T5 test
    // convention): the long-run gate density approaches p/127.
    constexpr int kSteps = 4000;
    constexpr uint8_t kPatternLength = 16;

    const struct {
        uint8_t probability;
        double expected;
        const char* what;
    } cases[] = {
        {32, 32.0 / 127.0, "density near 32/127"},
        {64, 64.0 / 127.0, "density near 64/127 (roughly half)"},
        {96, 96.0 / 127.0, "density near 96/127"},
    };

    for (const auto& c : cases) {
        sequencer::ProbabilityGateGenerator plugin;
        plugin.configure(c.probability);
        const sequencer::IGenerator& byBase = plugin;

        const uint32_t seedBase = 1000u;  // one fixed seed base for the sweep
        int gates = 0;
        for (int s = 0; s < kSteps; ++s) {
            if (generatedGateAt(byBase, kPatternLength, static_cast<uint32_t>(s),
                                seedBase + static_cast<uint32_t>(s))) {
                ++gates;
            }
        }
        const double density = static_cast<double>(gates) / kSteps;
        // Generous tolerance: the law is p/127, the check is "roughly" —
        // observed densities sit within ~1% of the expectation (stddev is
        // ~0.8% at 4000 steps), so +-10 percentage points is far outside
        // statistical noise while still pinning the behaviour.
        check(density > c.expected - 0.10 && density < c.expected + 0.10, c.what);

        if (c.probability == 64) {
            check(density > 0.40 && density < 0.60,
                  "at probability 64 the density is roughly half");
            // Monotonicity anchor: 64 sits between the 32 and 96 densities.
        }
    }

    // Monotone in the probability: a higher probability never gates less
    // over the same seed sweep.
    sequencer::ProbabilityGateGenerator low, mid, high;
    low.configure(32);
    mid.configure(64);
    high.configure(96);
    int lowOn = 0, midOn = 0, highOn = 0;
    for (int s = 0; s < kSteps; ++s) {
        const uint32_t seed = 77u + static_cast<uint32_t>(s);
        if (generatedGateAt(low, 16, static_cast<uint32_t>(s), seed)) ++lowOn;
        if (generatedGateAt(mid, 16, static_cast<uint32_t>(s), seed)) ++midOn;
        if (generatedGateAt(high, 16, static_cast<uint32_t>(s), seed)) ++highOn;
    }
    check(lowOn < midOn && midOn < highOn,
          "the gate density grows with the probability over the same seeds");
}

// --- 4: the same seed produces the same output (determinism) -----------------

void test_same_seed_same_output() {
    sequencer::ProbabilityGateGenerator plugin;
    plugin.configure(64);
    const sequencer::IGenerator& byBase = plugin;

    // Two independent sweeps with identical (seed, step) inputs replay
    // identical decisions.
    bool replayIdentical = true;
    for (uint32_t step = 0; step < 256; ++step) {
        const uint32_t seed = 555u + step;
        if (generatedGateAt(byBase, 16, step, seed) !=
            generatedGateAt(byBase, 16, step, seed)) {
            replayIdentical = false;
        }
    }
    check(replayIdentical, "the same (seed, step) produces the same gate decision");

    // A different seed base produces a different stream somewhere (the
    // RNG genuinely drives the decision; a fixed seed is what makes it
    // deterministic, not a constant answer).
    bool differsSomewhere = false;
    for (uint32_t step = 0; step < 64; ++step) {
        if (generatedGateAt(byBase, 16, step, 555u + step) !=
            generatedGateAt(byBase, 16, step, 777u + step)) {
            differsSomewhere = true;
        }
    }
    check(differsSomewhere, "a different seed produces a different gate stream");
}

// --- 5: capabilities are GATE only (AC8) -------------------------------------

void test_capabilities() {
    const sequencer::ProbabilityGateGenerator plugin;
    const GeneratorCapabilities caps = plugin.capabilities();
    check(caps.has(GeneratorCapability::GATE), "the generator declares GATE");
    check(!caps.has(GeneratorCapability::PITCH), "the generator declares no PITCH");
    check(!caps.has(GeneratorCapability::VELOCITY),
          "the generator declares no VELOCITY");
    check(!caps.has(GeneratorCapability::DURATION),
          "the generator declares no DURATION");
    check(caps.mask() == static_cast<uint8_t>(GeneratorCapability::GATE),
          "the capability set is exactly GATE");
}

// --- 6: generate() writes only the gate decision ------------------------------

void test_output_is_gate_only() {
    const sequencer::ProbabilityGateGenerator plugin;
    GeneratorRng rng(42u);
    GeneratorContext context(16, nullptr, rng);
    GeneratorOutput output;
    // Poison the optional fields so a stale value would be caught.
    output.hasPitch = true;
    output.pitch = 60;
    output.hasVelocity = true;
    output.velocity = 100;
    output.hasDuration = true;
    output.duration = 24;

    plugin.generate(3, context, output);

    check(output.hasGate, "generate() sets hasGate");
    check(output.hasPitch == false, "generate() never claims a pitch");
    check(output.hasVelocity == false, "generate() never claims a velocity");
    check(output.hasDuration == false, "generate() never claims a duration");
}

// --- 7: the fixed parameter table ---------------------------------------------

void test_param_defs() {
    const sequencer::ProbabilityGateGenerator plugin;
    const sequencer::GeneratorParamSet& defs = plugin.paramDefs();

    check(defs.count == 1, "the generator exposes one parameter");
    check(defs.defs[0].id == sequencer::ProbabilityGateGenerator::kParamProbability,
          "the probability parameter carries id 0");
    check(std::strcmp(defs.defs[0].name, "Probability") == 0,
          "the probability parameter is named");
    check(defs.defs[0].minValue == 0 &&
              defs.defs[0].maxValue == 127,
          "the probability range is 0..127");
    check(defs.defs[0].defaultValue == 64,
          "the factory default probability is 64 (roughly half)");
    check(defs.defs[0].hint == GeneratorParamHint::Percent,
          "the probability parameter carries the Percent hint");
    for (int i = 1; i < sequencer::kMaxGeneratorParams; ++i) {
        check(defs.defs[i].id == 0 && defs.defs[i].name[0] == '\0',
              "unused parameter slots stay zeroed");
    }
}

// --- 8: identity, configuration and totality -----------------------------------

void test_identity_and_config() {
    const sequencer::ProbabilityGateGenerator plugin;
    check(std::strcmp(plugin.id(), "builtin.probability") == 0,
          "the id is builtin.probability");
    check(plugin.name()[0] != '\0', "the display name is non-empty");

    check(sequencer::ProbabilityGateGenerator::kDefaultProbability == 64,
          "the default probability constant is 64");

    // configure() stores in-range values.
    sequencer::ProbabilityGateGenerator reconfigured;
    reconfigured.configure(90);
    check(reconfigured.probability() == 90, "configure() stores the probability");

    // configure() is total: raw values above 127 clamp to 127.
    sequencer::ProbabilityGateGenerator clamped;
    clamped.configure(200);
    check(clamped.probability() == 127, "configure() clamps above 127 to 127");

    const sequencer::ProbabilityGateGenerator typed(90);
    check(typed.probability() == 90, "the typed ctor matches configure(90)");
}

// --- 9: registration (AC1: RegisterGenerators.cpp is the only listing) ----------

void test_registration() {
    sequencer::GeneratorRegistry registry;
    sequencer::registerGenerators(registry);

    check(registry.count() == 3,
          "registerGenerators registers the Euclidean gate, the scale-aware "
          "pitch and the probability gate generator");
    const sequencer::IGenerator* found = registry.find("builtin.probability");
    check(found != nullptr, "builtin.probability is reachable by id");
    check(found != nullptr &&
              found->capabilities().has(GeneratorCapability::GATE),
          "the registered probability generator declares GATE");
    check(found != nullptr && found->paramDefs().count == 1,
          "the registered probability generator exposes its parameter");
    check(registry.find("builtin.euclidean") != nullptr,
          "the Euclidean gate generator stays registered");
    check(registry.find("builtin.pitch") != nullptr,
          "the scale-aware pitch generator stays registered");
    check(registry.find("builtin.duration") == nullptr,
          "not-yet-registered built-ins stay unreachable");

    // The registered instance carries the FACTORY configuration: its
    // decisions match a locally factory-configured instance bit for bit.
    const sequencer::ProbabilityGateGenerator factory;
    bool matches = true;
    for (uint32_t step = 0; step < 64; ++step) {
        if (generatedGateAt(*found, 16, step, 31337u + step) !=
            generatedGateAt(factory, 16, step, 31337u + step)) {
            matches = false;
        }
    }
    check(matches, "the registered instance uses the factory configuration");

    // Repeated registration is safe: the duplicates are rejected.
    sequencer::registerGenerators(registry);
    check(registry.count() == 3, "a repeated registerGenerators adds nothing");

    // The id fits PatternGenerativeState's fixed generator-id storage
    // (assignable to the GATE role).
    sequencer::PatternGenerativeState assigned;
    check(assigned.setGeneratorId(sequencer::GeneratorRole::GATE, "builtin.probability"),
          "the plugin id fits the fixed generator id storage");
    check(assigned.hasGenerator(sequencer::GeneratorRole::GATE),
          "the gate role accepts the plugin id");
}

}  // namespace

int main() {
    test_probability_zero_produces_no_gates();
    test_probability_max_produces_all_gates();
    test_density_follows_the_probability();
    test_same_seed_same_output();
    test_capabilities();
    test_output_is_gate_only();
    test_param_defs();
    test_identity_and_config();
    test_registration();

    if (failures == 0) {
        std::printf("test_probability_gate_generator: all checks passed\n");
        return 0;
    }
    std::printf("test_probability_gate_generator: %d check(s) failed\n", failures);
    return 1;
}
