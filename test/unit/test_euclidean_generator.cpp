// generative-engine-expansion task 4 — Euclidean gate generator plugin
// (FR2, AC1).
//
// Golden-output tests. The step-level Euclidean patterns below were captured
// from the PRE-REFACTORING common::GateSet::createEuclidean (the bucket
// algorithm that lived in gate_set.cpp) by running it for each tuple with
// stepLength=1 and gateLength=1 — a configuration under which the tick
// pattern IS the step pattern (each pulse yields exactly one high tick at
// its step position, and pulses == steps fills every tick). The literals
// pin the historical behavior so the plugin is verified against the
// ORIGINAL algorithm, not against itself, after gate_set.cpp delegates to
// the plugin.
//
// Validates:
//   1. the plugin's generate() reproduces the golden step decisions for
//      representative (steps, pulses, rotation) tuples — including the
//      edge cases 0 pulses, pulses == steps, pulses > steps (legacy raw
//      input) and steps == 1 — with the pattern repeating every `steps`
//      decisions (stepIndex wraps);
//   2. the delegated GateSet::createEuclidean still produces the exact
//      golden tick patterns (bit-identical output);
//   3. the shared free function common::euclideanPulseAt (the algorithm's
//      single home since task 4) reproduces the golden step decisions
//      directly, and an enumeration over the FULL supported ranges
//      (steps 1..64, pulses 0..steps, rotation 0..steps-1) compares the
//      legacy gate_set Euclidean output position by position against the
//      plugin output — zero divergence (task 4, step 1);
//   4. rotation >= steps wraps modulo steps, as the historical algorithm
//      did through (i + rotation) % steps;
//   5. steps == 0 (outside the parameter range) is total: no pulse, the
//      analog of the historical empty gate set;
//   6. capabilities are GATE only (AC8);
//   7. paramDefs match the existing UI limits: steps 1..64 (default 16),
//      pulses 0..64 (default 4), rotation 0..63 (default 0);
//   8. the generator is registered by registerGenerators() under
//      "builtin.euclidean" (AC1: the ONLY built-in listing lives in
//      RegisterGenerators.cpp), and the default-id constant
//      PatternGenerativeState::kDefaultGateGeneratorId names exactly that
//      registered plugin, so the Euclidean generator is the default gate
//      generator (while the default-constructed state stays inert,
//      keeping Milestone 2 playback stable).
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_euclidean_generator".

#include "GeneratorCapabilities.h"
#include "GeneratorRegistry.h"
#include "IGenerator.h"
#include "sequencer/PatternGenerativeState.h"
#include "sequencer/generators/EuclideanGateGenerator.h"

#include "common/gate_set.h"

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

// One golden (steps, pulses, rotation) tuple with its pre-refactoring
// step pattern: '1' = pulse, '0' = no pulse, one character per pattern
// position, captured from the historical createEuclidean output.
struct GoldenCase {
    uint8_t steps;
    uint8_t pulses;
    uint8_t rotation;
    const char* pattern;
};

constexpr GoldenCase kGoldenCases[] = {
    // Representative tuples.
    {8, 3, 0, "00100101"},
    {8, 3, 2, "01001001"},
    {16, 4, 0, "0001000100010001"},  // the factory default configuration
    {16, 5, 0, "0001001001001001"},
    {16, 7, 3, "1010010101001010"},
    {13, 5, 1, "1001001010010"},
    {7, 4, 6, "1010110"},
    {5, 2, 4, "01010"},
    // Edge cases required by the task.
    {8, 0, 0, "00000000"},  // 0 pulses: silence
    {8, 0, 5, "00000000"},  // 0 pulses, rotated: still silence
    {8, 8, 3, "11111111"},  // pulses == steps: every position on
    {4, 9, 2, "1111"},      // pulses > steps (legacy raw input): all on
    {1, 1, 0, "1"},         // steps == 1 with its single pulse
    {1, 0, 0, "0"},         // steps == 1 without pulses
    // The maximum UI steps value.
    {64, 33, 17,
     "0101010101010101101010101010101010101010101010101101010101010101"},
};

constexpr size_t goldenLength(const GoldenCase& c) {
    size_t len = 0;
    while (c.pattern[len] != '\0') ++len;
    return len;
}

// The golden gate decision at pattern position p.
bool goldenAt(const GoldenCase& c, size_t p) {
    return c.pattern[p % goldenLength(c)] == '1';
}

// Ask the generator (through the IGenerator base, as the sequencer does)
// for the decision at stepIndex with a fresh seeded context.
bool generatedGateAt(const sequencer::IGenerator& gen, uint8_t patternLength,
                      uint32_t stepIndex, uint32_t seed = 1u) {
    GeneratorRng rng(seed);
    GeneratorContext context(patternLength, nullptr, rng);
    GeneratorOutput output;
    gen.generate(stepIndex, context, output);
    return output.gate;
}

// --- 1: golden step decisions through generate() ------------------------------

void test_golden_step_decisions() {
    for (const GoldenCase& c : kGoldenCases) {
        const size_t len = goldenLength(c);
        check(len == c.steps, "the golden pattern length equals steps");

        const sequencer::EuclideanGateGenerator plugin(c.steps, c.pulses, c.rotation);
        const sequencer::IGenerator& byBase = plugin;  // dispatch as the sequencer does

        bool matches = true;
        // Two full pattern revolutions: the pattern repeats every `steps`
        // decisions (stepIndex wraps modulo steps).
        for (uint32_t step = 0; step < 2u * len; ++step) {
            GeneratorRng rng(1u);
            GeneratorContext context(static_cast<uint8_t>(len), nullptr, rng);
            GeneratorOutput output;
            byBase.generate(step, context, output);
            if (!output.hasGate || output.gate != goldenAt(c, step) ||
                output.hasPitch || output.hasVelocity || output.hasDuration) {
                matches = false;
            }
        }
        check(matches, "generate() reproduces the golden Euclidean pattern "
                       "(including the wrapped second revolution)");

        // generate() resets the output: a caller's stale fields never leak.
        GeneratorRng rng(7u);
        GeneratorContext context(static_cast<uint8_t>(len), nullptr, rng);
        GeneratorOutput stale;
        stale.hasPitch = true;
        stale.pitch = 60;
        stale.hasVelocity = true;
        stale.hasDuration = true;
        byBase.generate(0, context, stale);
        check(stale.hasGate && !stale.hasPitch && !stale.hasVelocity &&
                  !stale.hasDuration,
              "generate() clears every field it does not produce");

        // The shared free function — the algorithm's single home, which
        // the plugin delegates to — reproduces the same golden decisions.
        bool sharedMatches = true;
        for (uint32_t step = 0; step < 2u * len; ++step) {
            if (common::euclideanPulseAt(c.steps, c.pulses, c.rotation, step) !=
                goldenAt(c, step)) {
                sharedMatches = false;
            }
        }
        check(sharedMatches,
              "the shared common::euclideanPulseAt reproduces the golden "
              "Euclidean pattern");
    }
}

// --- 2b: enumerate the supported ranges: plugin == legacy gate_set output --

void test_enumerate_supported_ranges() {
    // Task 4, step 1: enumerate the legacy gate_set Euclidean output over
    // the supported ranges — steps 1..64, pulses 0..steps, rotation
    // 0..steps-1 — and compare it against the plugin output, position by
    // position (with stepLength=1 and gateLength=1 the tick pattern IS
    // the step pattern, so createEuclidean exposes the legacy step
    // decisions exactly). Any divergence is a bit-identity break between
    // the two consumers of the shared algorithm.
    bool matches = true;
    for (uint8_t steps = 1; steps <= 64; ++steps) {
        for (uint8_t pulses = 0; pulses <= steps; ++pulses) {
            for (uint8_t rotation = 0; rotation < steps; ++rotation) {
                const common::GateSet legacy = common::GateSet::createEuclidean(
                    steps, pulses, rotation, 1, 1);
                const std::vector<bool>& gates = legacy.getGates();
                if (gates.size() != steps) {
                    matches = false;
                    continue;
                }
                const sequencer::EuclideanGateGenerator plugin(steps, pulses,
                                                                rotation);
                const sequencer::IGenerator& byBase = plugin;
                for (uint32_t p = 0; p < steps; ++p) {
                    GeneratorRng rng(1u);
                    GeneratorContext context(steps, nullptr, rng);
                    GeneratorOutput output;
                    byBase.generate(p, context, output);
                    if (!output.hasGate || output.gate != gates[p] ||
                        common::euclideanPulseAt(steps, pulses, rotation, p) !=
                            gates[p]) {
                        matches = false;
                    }
                }
            }
        }
    }
    check(matches, "plugin output == legacy createEuclidean output over "
                    "the full supported ranges (steps 1..64, pulses "
                    "0..steps, rotation 0..steps-1)");
}

// --- 2: the delegated GateSet still produces the golden tick patterns ----------

void test_golden_delegation() {
    for (const GoldenCase& c : kGoldenCases) {
        const size_t len = goldenLength(c);
        // stepLength=1, gateLength=1: the tick pattern IS the step pattern
        // (the configuration the goldens were captured under).
        const common::GateSet gateSet = common::GateSet::createEuclidean(
            c.steps, c.pulses, c.rotation, 1, 1);
        const std::vector<bool>& gates = gateSet.getGates();
        check(gates.size() == len, "createEuclidean keeps the tick length");
        bool matches = gates.size() == len;
        for (size_t i = 0; i < gates.size(); ++i) {
            if (gates[i] != goldenAt(c, i)) matches = false;
        }
        check(matches, "createEuclidean (delegating to the plugin) is "
                       "bit-identical to the golden patterns");
    }
}

// --- 3: rotation wraps modulo steps, as the historical algorithm did ------------

void test_rotation_wrap() {
    // (i + rotation) % steps: rotation 10 over 8 steps == rotation 2.
    const sequencer::EuclideanGateGenerator wrapped(8, 3, 10);
    const sequencer::EuclideanGateGenerator canonical(8, 3, 2);
    bool matches = true;
    for (uint32_t step = 0; step < 16; ++step) {
        if (generatedGateAt(wrapped, 8, step) != generatedGateAt(canonical, 8, step)) {
            matches = false;
        }
    }
    check(matches, "rotation >= steps wraps modulo steps (historical "
                   "behavior)");

    // The decision depends only on (configuration, stepIndex): a different
    // RNG seed — which the Euclidean algorithm never draws — changes
    // nothing.
    const sequencer::EuclideanGateGenerator plugin(16, 7, 3);
    bool seedIndependent = true;
    for (uint32_t step = 0; step < 32; ++step) {
        if (generatedGateAt(plugin, 16, step, 1u) !=
            generatedGateAt(plugin, 16, step, 0xDEADBEEFu)) {
            seedIndependent = false;
        }
    }
    check(seedIndependent,
          "the gate decision is deterministic and seed-independent");
}

// --- 4: steps == 0 is total (the historical empty-pattern analog) ----------------

void test_zero_steps_total() {
    sequencer::EuclideanGateGenerator plugin;
    plugin.configure(0, 4, 0);
    bool silent = true;
    for (uint32_t step = 0; step < 64; ++step) {
        GeneratorRng rng(1u);
        GeneratorContext context(0, nullptr, rng);
        GeneratorOutput output;
        plugin.generate(step, context, output);
        if (!output.hasGate || output.gate) silent = false;
    }
    check(silent, "steps == 0 produces no pulse on any position (total)");
}

// --- 5: capabilities are GATE only (AC8) -----------------------------------------

void test_capabilities() {
    const sequencer::EuclideanGateGenerator plugin;
    const GeneratorCapabilities caps = plugin.capabilities();
    check(caps.has(GeneratorCapability::GATE), "the plugin reports GATE");
    check(!caps.has(GeneratorCapability::PITCH), "the plugin reports no PITCH");
    check(!caps.has(GeneratorCapability::VELOCITY),
          "the plugin reports no VELOCITY");
    check(!caps.has(GeneratorCapability::DURATION),
          "the plugin reports no DURATION");
    check(caps.mask() == static_cast<uint8_t>(GeneratorCapability::GATE),
          "the capability mask is exactly the GATE bit");
    check(!caps.none(), "the capability set is not empty");
}

// --- 6: paramDefs match the existing limits ---------------------------------------

void test_param_defs() {
    const sequencer::EuclideanGateGenerator plugin;
    const sequencer::GeneratorParamSet& defs = plugin.paramDefs();

    check(defs.count == 3, "the plugin declares exactly three parameters");
    check(defs.count <= sequencer::kMaxGeneratorParams,
          "the parameter count stays within the fixed capacity");

    check(defs.defs[0].id == sequencer::EuclideanGateGenerator::kParamSteps &&
              defs.defs[1].id == sequencer::EuclideanGateGenerator::kParamPulses &&
              defs.defs[2].id == sequencer::EuclideanGateGenerator::kParamRotation,
          "parameter ids are steps(0), pulses(1), rotation(2)");
    for (uint8_t i = 0; i < defs.count; ++i) {
        check(defs.defs[i].name != nullptr && defs.defs[i].name[0] != '\0',
              "parameter names are non-empty");
        check(defs.defs[i].minValue <= defs.defs[i].maxValue,
              "parameter ranges are ordered");
        check(defs.defs[i].defaultValue >= defs.defs[i].minValue &&
                  defs.defs[i].defaultValue <= defs.defs[i].maxValue,
              "parameter defaults lie inside their ranges");
    }
    for (uint8_t i = 0; i < defs.count; ++i)
        for (uint8_t j = i + 1; j < defs.count; ++j)
            check(defs.defs[i].id != defs.defs[j].id, "parameter ids are unique");

    // The existing limits (Reducer.cpp gate-set editing): steps 1..64,
    // pulses 0..steps (max 64), rotation 0..steps-1 (max 63); factory
    // defaults follow GateSetConfig (16 steps, 4 pulses, no rotation).
    check(std::strcmp(defs.defs[0].name, "Steps") == 0, "Steps name");
    check(defs.defs[0].minValue == 1 && defs.defs[0].maxValue == 64 &&
              defs.defs[0].defaultValue == 16,
          "Steps range 1..64, default 16 (the existing limit)");
    check(defs.defs[1].minValue == 0 && defs.defs[1].maxValue == 64 &&
              defs.defs[1].defaultValue == 4,
          "Pulses range 0..64, default 4 (the existing limit)");
    check(defs.defs[2].minValue == 0 && defs.defs[2].maxValue == 63 &&
              defs.defs[2].defaultValue == 0,
          "Rotation range 0..63, default 0 (the existing limit)");
    check(defs.defs[0].hint == GeneratorParamHint::Steps &&
              defs.defs[1].hint == GeneratorParamHint::Steps,
          "Steps/Pulses present as pattern-relative step counts");
}

// --- 7: identity + configuration accessors ----------------------------------------

void test_identity_and_config() {
    const sequencer::EuclideanGateGenerator plugin;
    check(std::strcmp(plugin.id(), "builtin.euclidean") == 0,
          "the plugin id is builtin.euclidean");
    check(plugin.name() != nullptr && plugin.name()[0] != '\0',
          "the display name is non-empty");

    // Factory defaults: the GateSetConfig values (16, 4, 0).
    check(plugin.steps() == 16 && plugin.pulses() == 4 && plugin.rotation() == 0,
          "the default configuration is (16, 4, 0)");

    const sequencer::EuclideanGateGenerator configured(13, 5, 1);
    check(configured.steps() == 13 && configured.pulses() == 5 &&
              configured.rotation() == 1,
          "the constructor stores the configuration");

    sequencer::EuclideanGateGenerator reconfigured;
    reconfigured.configure(7, 4, 6);
    check(reconfigured.steps() == 7 && reconfigured.pulses() == 4 &&
              reconfigured.rotation() == 6,
          "configure() stores the configuration");
}

// --- 8: registration (AC1: RegisterGenerators.cpp is the only listing) ------------

void test_registration() {
    sequencer::GeneratorRegistry registry;
    sequencer::registerGenerators(registry);

    check(registry.count() == 3,
          "registerGenerators registers the Euclidean gate, the scale-aware "
          "pitch and the probability gate generator");
    const sequencer::IGenerator* found = registry.find("builtin.euclidean");
    check(found != nullptr, "builtin.euclidean is reachable by id");
    check(registry.at(0) == found,
          "iteration reaches the Euclidean generator first");
    check(found != nullptr &&
              found->capabilities().has(GeneratorCapability::GATE),
          "the registered generator declares GATE");
    check(found != nullptr && found->paramDefs().count == 3,
          "the registered generator exposes its three parameters");
    check(registry.find("builtin.duration") == nullptr,
          "not-yet-registered built-ins stay unreachable");

    // Repeated registration is safe: the duplicate is rejected.
    sequencer::registerGenerators(registry);
    check(registry.count() == 3, "a repeated registerGenerators adds nothing");
}

// --- 9: the default-id constant names the registered plugin -------------

void test_default_gate_generator_id() {
    // Task 4, step 5: the default gate generator id is set ONLY via the
    // default-id constant, and the constant names the registered
    // Euclidean plugin — so the plugin IS the default gate generator.
    check(std::strcmp(sequencer::kDefaultGateGeneratorId, "builtin.euclidean") == 0,
          "the default-id constant is builtin.euclidean");

    sequencer::GeneratorRegistry registry;
    sequencer::registerGenerators(registry);
    const sequencer::IGenerator* def =
        registry.find(sequencer::kDefaultGateGeneratorId);
    check(def != nullptr,
          "the default gate generator id resolves through the registry");
    check(def != nullptr && def->capabilities().has(GeneratorCapability::GATE),
          "the default gate generator is gate-capable");
    check(def != nullptr &&
              std::strcmp(def->id(), sequencer::kDefaultGateGeneratorId) == 0,
          "the resolved default gate generator carries the constant's id");

    // The constant fits the fixed id storage (assignable to the GATE
    // role).
    sequencer::PatternGenerativeState assigned;
    check(assigned.setGeneratorId(sequencer::GeneratorRole::GATE,
                                  sequencer::kDefaultGateGeneratorId),
          "the default-id constant fits the fixed generator id storage");
    check(assigned.hasGenerator(sequencer::GeneratorRole::GATE),
          "the gate role accepts the default-id constant");

    // The default-CONSTRUCTED state stays inert: with no generator
    // configured, playback is identical to Milestone 2 (the global
    // default-behavior-stability constraint, pinned by
    // test_pattern_generative_state). The constant is what a consumer
    // assigns when the default gate generator is requested.
    const sequencer::PatternGenerativeState fresh;
    check(!fresh.hasGenerator(sequencer::GeneratorRole::GATE),
          "the default-constructed state keeps the gate role unassigned "
          "(Milestone 2 playback stays stable)");
    check(!fresh.anyGeneratorAssigned(),
          "the default-constructed state assigns no generator");
}

}  // namespace

int main() {
    test_golden_step_decisions();
    test_golden_delegation();
    test_enumerate_supported_ranges();
    test_rotation_wrap();
    test_zero_steps_total();
    test_capabilities();
    test_param_defs();
    test_identity_and_config();
    test_registration();
    test_default_gate_generator_id();

    if (failures == 0) {
        std::printf("test_euclidean_generator: all checks passed\n");
        return 0;
    }
    std::printf("test_euclidean_generator: %d check(s) failed\n", failures);
    return 1;
}
