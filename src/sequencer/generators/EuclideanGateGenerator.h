#pragma once

#include <cstdint>
#include <type_traits>

#include "../GeneratorCapabilities.h"
#include "../IGenerator.h"

// Euclidean gate generator plugin (generative-engine-expansion task 4,
// FR2, AC1).
//
// The Euclidean pattern logic that lived in common::GateSet::createEuclidean
// moves here, into the plugin layer: which of `steps` pattern positions carry
// a pulse, distributed as evenly as possible (the historical bucket
// algorithm) and rotated by `rotation`. Decisions surface per step through
// IGenerator::generate(); the tick-level expansion (step length, gate
// length) stays shared gate data in GateSet, which delegates its step
// decisions here — so the plugin path and the legacy createEuclidean path
// are bit-identical by construction (the golden tests in
// test_euclidean_generator.cpp pin the pre-refactoring outputs).
//
//   - capability: GATE only (the UI shows just the gate controls, AC8);
//   - parameters: steps (id 0), pulses (id 1), rotation (id 2), with the
//     ranges the existing UI enforces (Reducer.cpp): steps 1..64,
//     pulses 0..steps, rotation 0..steps-1; factory defaults follow the
//     GateSetConfig values (16, 4, 0);
//   - deterministic: generate() is a pure function of (configuration,
//     stepIndex) — the Euclidean algorithm draws no randomness, so the
//     context's RNG is never touched.
//
// Values outside the declared parameter ranges keep the HISTORICAL
// semantics exactly (see pulseAt): steps 0 or pulses 0 -> no pulse anywhere,
// pulses >= steps -> every position, rotation wraps modulo steps. That
// totality is what lets GateSet::createEuclidean delegate with bit-identical
// output for every input it has ever accepted.
//
// Global constraints honored here: no heap allocation, no RTTI, no
// exceptions on the generation path; fixed-size members with a
// static_assert bound below. SDK-free: <cstdint> plus the SDK-free generator
// interface headers only.

namespace sequencer {

// The Euclidean gate generator plugin. Default construction yields the
// factory configuration (16, 4, 0); the registered instance in
// RegisterGenerators.cpp uses exactly that.
class EuclideanGateGenerator final : public IGenerator {
public:
    // Stable parameter ids (PatternGenerativeState stores configured
    // values indexed by these).
    static constexpr uint8_t kParamSteps = 0;
    static constexpr uint8_t kParamPulses = 1;
    static constexpr uint8_t kParamRotation = 2;

    // The existing limits (Reducer.cpp gate-set editing): steps 1..64,
    // pulses 0..steps (so at most 64), rotation 0..steps-1 (so at most 63).
    static constexpr uint8_t kMinSteps = 1;
    static constexpr uint8_t kMaxSteps = 64;
    // Factory defaults: the GateSetConfig values.
    static constexpr uint8_t kDefaultSteps = 16;
    static constexpr uint8_t kDefaultPulses = 4;

    // Factory configuration (16, 4, 0).
    EuclideanGateGenerator() = default;
    EuclideanGateGenerator(uint8_t steps, uint8_t pulses, uint8_t rotation);

    // Set the pattern configuration. Values are stored EXACTLY as given —
    // the algorithm is total with the historical semantics for any uint8
    // input, and GateSet::createEuclidean relies on that for bit-identical
    // delegation; callers wanting the UI limits clamp before configuring.
    void configure(uint8_t steps, uint8_t pulses, uint8_t rotation);

    // The stored configuration.
    uint8_t steps() const { return steps_; }
    uint8_t pulses() const { return pulses_; }
    uint8_t rotation() const { return rotation_; }

    // IGenerator: stable identity for dispatch, persistence and the UI.
    const char* id() const override { return "builtin.euclidean"; }
    const char* name() const override { return "Euclidean Gates"; }
    // GATE only (FR1, AC8).
    GeneratorCapabilities capabilities() const override {
        return GeneratorCapability::GATE;
    }
    const GeneratorParamSet& paramDefs() const override;
    // One step's decision: hasGate is always true (the generator always
    // owns the gate), gate is the Euclidean pulse decision at the wrapped
    // pattern position stepIndex % steps.
    void generate(uint32_t stepIndex, const GeneratorContext& context,
                  GeneratorOutput& output) const override;

    // The moved Euclidean core: does pattern position `position` carry a
    // pulse in the E(pulses, steps) pattern rotated by `rotation`?
    //
    // Exactly the decision of the historical createEuclidean bucket
    // algorithm (which marked pattern[(i + rotation) % steps] when the
    // pulse bucket crossed a step boundary), total for every uint8 input:
    //   - steps == 0 or pulses == 0  -> false (no pulse anywhere);
    //   - pulses >= steps            -> true (every position carries one);
    //   - otherwise position and rotation wrap modulo steps.
    static bool pulseAt(uint8_t steps, uint8_t pulses, uint8_t rotation,
                        uint32_t position);

private:
    // The generator's configuration. Values may lie outside the declared
    // parameter ranges (total legacy semantics); see configure().
    uint8_t steps_ = kDefaultSteps;
    uint8_t pulses_ = kDefaultPulses;
    uint8_t rotation_ = 0;
};

inline EuclideanGateGenerator::EuclideanGateGenerator(uint8_t steps, uint8_t pulses,
                                                     uint8_t rotation)
    : steps_(steps), pulses_(pulses), rotation_(rotation) {}

inline void EuclideanGateGenerator::configure(uint8_t steps, uint8_t pulses,
                                             uint8_t rotation) {
    steps_ = steps;
    pulses_ = pulses;
    rotation_ = rotation;
}

inline bool EuclideanGateGenerator::pulseAt(uint8_t steps, uint8_t pulses,
                                            uint8_t rotation, uint32_t position) {
    // Historical edge cases first: an empty pattern (steps 0) and a silent
    // one (pulses 0) carry no pulse at any position; pulses >= steps fills
    // every position.
    if (steps == 0 || pulses == 0) return false;
    if (pulses >= steps) return true;
    // The historical loop marked position (i + rotation) % steps when the
    // pulse bucket for i crossed a step boundary, i.e. when
    // ((i + 1) * pulses) mod steps < pulses. Invert the rotation to find
    // the bucket index for this position, then apply the same condition.
    const uint32_t r = static_cast<uint32_t>(rotation) % steps;
    const uint32_t i = (position % steps + steps - r) % steps;
    // (i + 1) * pulses <= 255 * 255: fits in 32 bits, no overflow.
    const uint32_t bucket = (i + 1u) * static_cast<uint32_t>(pulses);
    return (bucket % steps) < static_cast<uint32_t>(pulses);
}

inline void EuclideanGateGenerator::generate(uint32_t stepIndex,
                                            const GeneratorContext& /*context*/,
                                            GeneratorOutput& output) const {
    // Reset the output so stale caller fields never leak (the same
    // convention as the other plugins), then emit only the gate decision.
    output = GeneratorOutput{};
    output.hasGate = true;
    // The pattern repeats every `steps` decisions; pulseAt wraps the
    // position and is total for every configuration, including the
    // legacy steps == 0 (no pulse anywhere — the empty-pattern analog).
    output.gate = pulseAt(steps_, pulses_, rotation_, stepIndex);
}

// The fixed parameter table, declared at namespace scope so the constexpr
// evaluation happens after the class type is complete (the same pattern as
// the registry tests' fake generators).
constexpr GeneratorParamSet makeEuclideanParamDefs() {
    GeneratorParamSet set;
    set.defs[0] = {EuclideanGateGenerator::kParamSteps, "Steps",
                   EuclideanGateGenerator::kMinSteps, EuclideanGateGenerator::kMaxSteps,
                   EuclideanGateGenerator::kDefaultSteps, GeneratorParamHint::Steps};
    set.defs[1] = {EuclideanGateGenerator::kParamPulses, "Pulses",
                   0, EuclideanGateGenerator::kMaxSteps,
                   EuclideanGateGenerator::kDefaultPulses, GeneratorParamHint::Steps};
    set.defs[2] = {EuclideanGateGenerator::kParamRotation, "Rotation",
                   0, static_cast<int16_t>(EuclideanGateGenerator::kMaxSteps - 1),
                   0, GeneratorParamHint::None};
    set.count = 3;
    return set;
}

constexpr GeneratorParamSet kEuclideanParamDefs = makeEuclideanParamDefs();

inline const GeneratorParamSet& EuclideanGateGenerator::paramDefs() const {
    return kEuclideanParamDefs;
}

// Size bounds on the new plugin state (global constraint): one vtable
// pointer plus the three configuration bytes, a small fixed footprint.
static_assert(sizeof(EuclideanGateGenerator) <= 16,
              "EuclideanGateGenerator stays a small fixed object");

}  // namespace sequencer
