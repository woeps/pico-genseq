#pragma once

#include <cstdint>
#include <type_traits>

#include "../GeneratorCapabilities.h"
#include "../IGenerator.h"

// Probability gate generator plugin (generative-engine-expansion task 6,
// FR2, AC1).
//
// The gate generator whose per-step decision is a Bernoulli draw with the
// configured probability (0-127): ONE uniform draw over the kProbabilityLevels
// (127) levels of the deterministic, seedable GeneratorRng the
// GeneratorContext carries — the SAME seeded PRNG the scale-aware pitch
// generator (T5) draws with, never a duplicate — and the gate is ON iff the
// draw lands strictly below the configured probability:
//
//   - probability 0    -> no level passes -> silence at every step;
//   - probability 127  -> every level passes -> certainty at every step;
//   - probability p    -> long-run gate density p/127, so 64 is roughly
//     half and 32/96 bracket it on either side.
//
// The two endpoints are therefore exact (0 = never, 127 = always): with a
// 127-level draw, the strict-below comparison covers the full 0..127
// threshold span with no missing level at the top and no passing level at
// the bottom — the reason the draw is over 127 levels, not 128 (a 128-level
// draw with a 0..127 threshold would leave probability 127 at 127/128, not
// certainty).
//
//   - capability: GATE only (the UI shows just the probability control,
//     AC8); generate() writes ONLY the gate decision, so a gate-only
//     generator never disturbs stored pitch material;
//   - parameters: probability (id 0), 0..127 with the Percent hint; the
//     factory default is 64 (roughly half density);
//   - deterministic: generate() is a pure function of (stepIndex, context,
//     configuration) — the only randomness is the context's caller-owned,
//     caller-seeded RNG (the sequencer reseeds per pattern/role/step), so a
//     fixed seed replays identical output and a different seed produces a
//     different stream.
//
// Global constraints honored here: no heap allocation, no RTTI, no
// exceptions on the generation path; fixed-size members with a
// static_assert bound below. SDK-free: <cstdint> plus the SDK-free
// generator interface headers only.

namespace sequencer {

// The probability gate generator plugin. Default construction yields the
// factory configuration (probability 64); the registered instance in
// RegisterGenerators.cpp uses exactly that.
class ProbabilityGateGenerator final : public IGenerator {
public:
    // Stable parameter id (PatternGenerativeState stores configured values
    // indexed by it).
    static constexpr uint8_t kParamProbability = 0;

    // The probability parameter's declared range: 0 (never) .. 127
    // (always).
    static constexpr uint8_t kMinProbability = 0;
    static constexpr uint8_t kMaxProbability = 127;
    // The number of equiprobable draw levels: one uniform draw over
    // [0, 127) — 127 levels — so the strict-below threshold spans the full
    // 0..127 probability range with BOTH endpoints exact.
    static constexpr uint8_t kProbabilityLevels = 127;
    // Factory default: 64, roughly half density.
    static constexpr uint8_t kDefaultProbability = 64;

    // Factory configuration (probability 64).
    ProbabilityGateGenerator() = default;
    explicit ProbabilityGateGenerator(uint8_t probability);

    // Set the gate probability. Total: a raw value above 127 clamps to 127
    // (a uint8 can still carry a raw 128-255 value), so the stored
    // probability is always a valid 0-127.
    void configure(uint8_t probability);

    // The stored (clamped) probability.
    uint8_t probability() const { return probability_; }

    // IGenerator: stable identity for dispatch, persistence and the UI.
    const char* id() const override { return "builtin.probability"; }
    const char* name() const override { return "Probability Gates"; }
    // GATE only (FR1, AC8).
    GeneratorCapabilities capabilities() const override {
        return GeneratorCapability::GATE;
    }
    const GeneratorParamSet& paramDefs() const override;
    // One step's decision: hasGate is always true (the generator always
    // owns the gate), gate is the Bernoulli draw — one uniform level from
    // the context's seeded PRNG, ON iff below the configured probability.
    // The RNG state is the CALLER's (the sequencer reseeds per
    // pattern/role/step), so identical inputs replay identical decisions.
    void generate(uint32_t stepIndex, const GeneratorContext& context,
                  GeneratorOutput& output) const override;

private:
    // The generator's configuration: the clamped gate probability.
    uint8_t probability_ = kDefaultProbability;
};

inline ProbabilityGateGenerator::ProbabilityGateGenerator(uint8_t probability) {
    configure(probability);
}

inline void ProbabilityGateGenerator::configure(uint8_t probability) {
    // Clamp into the declared 0-127 range (total for any uint8 input).
    probability_ = (probability > kMaxProbability) ? kMaxProbability : probability;
}

inline void ProbabilityGateGenerator::generate(uint32_t /*stepIndex*/,
                                               const GeneratorContext& context,
                                               GeneratorOutput& output) const {
    // Reset the output so stale caller fields never leak (the same
    // convention as the other plugins), then emit only the gate decision.
    output = GeneratorOutput{};
    output.hasGate = true;
    // One uniform draw over the 127 probability levels (the T5 GeneratorRng
    // API; no modulo bias — nextBelow rejection-samples the byte). Gate ON
    // iff the level lands strictly below the probability: 0 passes nothing,
    // 127 passes every level, the long-run density is p/127.
    output.gate =
        context.rng.nextBelow(kProbabilityLevels) < probability_;
}

// The fixed parameter table, declared at namespace scope so the constexpr
// evaluation happens after the class type is complete (the same pattern as
// the sibling plugins).
constexpr GeneratorParamSet makeProbabilityParamDefs() {
    GeneratorParamSet set;
    set.defs[0] = {ProbabilityGateGenerator::kParamProbability, "Probability",
                   ProbabilityGateGenerator::kMinProbability,
                   ProbabilityGateGenerator::kMaxProbability,
                   ProbabilityGateGenerator::kDefaultProbability,
                   GeneratorParamHint::Percent};
    set.count = 1;
    return set;
}

constexpr GeneratorParamSet kProbabilityParamDefs = makeProbabilityParamDefs();

inline const GeneratorParamSet& ProbabilityGateGenerator::paramDefs() const {
    return kProbabilityParamDefs;
}

// Size bounds on the new plugin state (global constraint): one vtable
// pointer plus the one configuration byte, a small fixed footprint.
static_assert(sizeof(ProbabilityGateGenerator) <= 16,
              "ProbabilityGateGenerator stays a small fixed object");

}  // namespace sequencer
