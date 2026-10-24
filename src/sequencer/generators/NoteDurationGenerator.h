#pragma once

#include <cstdint>
#include <type_traits>

#include "../GeneratorCapabilities.h"
#include "../IGenerator.h"

// Note duration generator plugin (generative-engine-expansion task 7,
// FR2, AC1).
//
// The duration generator producing per-step note lengths in TRANSPORT
// TICKS (PPQN per beat; one tick = one pattern step in this sequencer).
// Each decision is a fixed BASE duration varied around itself by a
// 0-127 variation amount and hard-clamped into the configured min/max
// tick window:
//
//   - variation 0   -> the constant base duration (NO RNG draw at all:
//     a constant needs no entropy, so the context's stream is left
//     untouched — the same convention as the Euclidean plugin);
//   - variation v   -> ONE uniform byte draw from the deterministic,
//     seedable GeneratorRng the GeneratorContext carries (the SAME
//     seeded PRNG the pitch and probability generators draw with, never
//     a duplicate), scaled to v/127 of the min..max spread as a signed
//     offset around the base, then clamped into [min, max] — so every
//     decision is a well-formed note length whatever the configuration.
//
// Milestone 2 note-ownership compatibility: the durations are plain tick
// counts the sequencer arms as a bounded countdown at Note On; the
// recorded onset is released exactly once when the countdown crosses
// zero (and a falling gate edge disarms a still-pending countdown), so
// every Note On still gets exactly one Note Off.
//
//   - capability: DURATION only (the UI shows just the length controls,
//     AC8); generate() writes ONLY the duration decision, so a
//     duration-only generator never disturbs stored gate/pitch material;
//   - parameters: base length (id 0), min length (id 1), max length
//     (id 2) — all in ticks, 1..255 — and variation (id 3), 0..127 with
//     the Percent hint. Factory defaults: base 24 (one beat at PPQN 24),
//     window 1..48 (up to two beats), variation 0 — stable by default;
//   - deterministic: generate() is a pure function of (stepIndex,
//     context, configuration) — the only randomness is the context's
//     caller-owned, caller-seeded RNG (the sequencer reseeds per
//     pattern/role/step), so a fixed seed replays identical output and a
//     different seed produces a different stream.
//
// Global constraints honored here: no heap allocation, no RTTI, no
// exceptions on the generation path; bounded arithmetic only (the
// jitter offset is a fixed closed-form computation — no loops); fixed-
// size members with a static_assert bound below. SDK-free: <cstdint>
// plus the SDK-free generator interface headers only.

namespace sequencer {

// The note duration generator plugin. Default construction yields the
// factory configuration (base 24, window 1..48, variation 0); the
// registered instance in RegisterGenerators.cpp uses exactly that.
class NoteDurationGenerator final : public IGenerator {
public:
    // Stable parameter ids (PatternGenerativeState stores configured
    // values indexed by these).
    static constexpr uint8_t kParamBaseLength = 0;
    static constexpr uint8_t kParamMinLength = 1;
    static constexpr uint8_t kParamMaxLength = 2;
    static constexpr uint8_t kParamVariation = 3;

    // The tick parameters' declared range: every configured duration
    // stays within 1..255 transport ticks (just over ten beats at
    // PPQN 24, comfortably spanning the 64-step pattern limit).
    static constexpr uint16_t kMinDurationTicks = 1;
    static constexpr uint16_t kMaxDurationTicks = 255;
    // The variation parameter's declared range: 0 (constant base) .. 127
    // (the full min..max spread as the jitter amplitude).
    static constexpr uint8_t kMaxVariation = 127;
    // Factory defaults: a constant one-beat note (24 ticks at PPQN 24)
    // inside a 1..48 window, with no variation (stable by default).
    static constexpr uint16_t kDefaultBaseTicks = 24;
    static constexpr uint16_t kDefaultMinTicks = 1;
    static constexpr uint16_t kDefaultMaxTicks = 48;
    static constexpr uint8_t kDefaultVariation = 0;

    // Factory configuration (base 24, window 1..48, variation 0).
    NoteDurationGenerator() = default;
    NoteDurationGenerator(uint16_t minTicks, uint16_t maxTicks, uint16_t baseTicks,
                          uint8_t variation);

    // Set the configuration. Total for any input:
    //   - each tick value clamps into the declared 1..255 range;
    //   - a reversed min/max window collapses to the minimum;
    //   - the base clamps into the (normalized) window, so the constant
    //     variation-0 output is always a well-formed in-window length;
    //   - variation clamps into 0..127.
    void configure(uint16_t minTicks, uint16_t maxTicks, uint16_t baseTicks,
                   uint8_t variation);

    // The stored (normalized) configuration.
    uint16_t minTicks() const { return minTicks_; }
    uint16_t maxTicks() const { return maxTicks_; }
    uint16_t baseTicks() const { return baseTicks_; }
    uint8_t variation() const { return variation_; }

    // IGenerator: stable identity for dispatch, persistence and the UI.
    const char* id() const override { return "builtin.duration"; }
    const char* name() const override { return "Note Durations"; }
    // DURATION only (FR1, AC8).
    GeneratorCapabilities capabilities() const override {
        return GeneratorCapability::DURATION;
    }
    const GeneratorParamSet& paramDefs() const override;
    // One step's decision: hasDuration is always true (the generator
    // always owns the note length), duration is the varied, clamped tick
    // count described in the file comment. The RNG state is the
    // CALLER's (the sequencer reseeds per pattern/role/step), so
    // identical inputs replay identical durations; no draw is taken at
    // all when the output cannot vary (variation 0 or a degenerate
    // window).
    void generate(uint32_t stepIndex, const GeneratorContext& context,
                  GeneratorOutput& output) const override;

private:
    // The generator's configuration, normalized by configure(): a
    // non-empty window min_ <= base_ <= max_ inside 1..255, and a
    // variation inside 0..127.
    uint16_t minTicks_ = kDefaultMinTicks;
    uint16_t maxTicks_ = kDefaultMaxTicks;
    uint16_t baseTicks_ = kDefaultBaseTicks;
    uint8_t variation_ = kDefaultVariation;
};

inline NoteDurationGenerator::NoteDurationGenerator(uint16_t minTicks,
                                                    uint16_t maxTicks,
                                                    uint16_t baseTicks,
                                                    uint8_t variation) {
    configure(minTicks, maxTicks, baseTicks, variation);
}

inline void NoteDurationGenerator::configure(uint16_t minTicks, uint16_t maxTicks,
                                             uint16_t baseTicks, uint8_t variation) {
    // Clamp each tick value into the declared range first.
    if (minTicks < kMinDurationTicks) minTicks = kMinDurationTicks;
    if (minTicks > kMaxDurationTicks) minTicks = kMaxDurationTicks;
    if (maxTicks < kMinDurationTicks) maxTicks = kMinDurationTicks;
    if (maxTicks > kMaxDurationTicks) maxTicks = kMaxDurationTicks;
    // Normalize the window: never reversed (a reversed window collapses
    // to its minimum).
    if (maxTicks < minTicks) maxTicks = minTicks;
    // Clamp the base into the declared range, then into the window.
    if (baseTicks < kMinDurationTicks) baseTicks = kMinDurationTicks;
    if (baseTicks > kMaxDurationTicks) baseTicks = kMaxDurationTicks;
    if (baseTicks < minTicks) baseTicks = minTicks;
    if (baseTicks > maxTicks) baseTicks = maxTicks;
    // Clamp the variation into 0..127.
    variation_ = (variation > kMaxVariation) ? kMaxVariation : variation;
    minTicks_ = minTicks;
    maxTicks_ = maxTicks;
    baseTicks_ = baseTicks;
}

inline void NoteDurationGenerator::generate(uint32_t /*stepIndex*/,
                                            const GeneratorContext& context,
                                            GeneratorOutput& output) const {
    // Reset the output so stale caller fields never leak (the same
    // convention as the other plugins), then emit only the duration
    // decision. The generator always owns the note length.
    output = GeneratorOutput{};
    output.hasDuration = true;

    // Constant path: no room (or no request) to vary -> the base
    // duration, with NO RNG draw (a constant needs no entropy, so the
    // caller's stream is left untouched — same as the Euclidean plugin).
    if (variation_ == 0 || maxTicks_ <= minTicks_) {
        output.duration = baseTicks_;
        return;
    }

    // Varied path: one uniform byte draw scaled to variation/127 of the
    // min..max spread, as a signed offset around the base.
    const uint16_t spread = static_cast<uint16_t>(maxTicks_ - minTicks_);
    // amplitude = round(spread * variation / 127); bounded: spread <= 254
    // and variation <= 127, so the product fits 32 bits comfortably.
    const uint32_t amplitude =
        (static_cast<uint32_t>(spread) * variation_ + 63u) / 127u;
    if (amplitude == 0) {  // spread too small for this variation: constant
        output.duration = baseTicks_;
        return;
    }

    // draw in [0, 255] -> offset in [-amplitude, +amplitude] (draw 255
    // maps to +amplitude, draw 0 to -amplitude); bounded arithmetic.
    const uint8_t draw = context.rng.nextByte();
    const int32_t offset =
        static_cast<int32_t>(amplitude * 2u * draw) / 255 - static_cast<int32_t>(amplitude);
    int32_t varied = static_cast<int32_t>(baseTicks_) + offset;
    // Hard clamp into the configured window: the invariant every caller
    // (and the countdown the sequencer arms) relies on.
    if (varied < static_cast<int32_t>(minTicks_)) varied = minTicks_;
    if (varied > static_cast<int32_t>(maxTicks_)) varied = maxTicks_;
    output.duration = static_cast<uint16_t>(varied);
}

// The fixed parameter table, declared at namespace scope so the constexpr
// evaluation happens after the class type is complete (the same pattern
// as the sibling plugins).
constexpr GeneratorParamSet makeNoteDurationParamDefs() {
    GeneratorParamSet set;
    set.defs[0] = {NoteDurationGenerator::kParamBaseLength, "Base Length",
                   NoteDurationGenerator::kMinDurationTicks,
                   NoteDurationGenerator::kMaxDurationTicks,
                   NoteDurationGenerator::kDefaultBaseTicks,
                   GeneratorParamHint::Steps};
    set.defs[1] = {NoteDurationGenerator::kParamMinLength, "Min Length",
                   NoteDurationGenerator::kMinDurationTicks,
                   NoteDurationGenerator::kMaxDurationTicks,
                   NoteDurationGenerator::kDefaultMinTicks,
                   GeneratorParamHint::Steps};
    set.defs[2] = {NoteDurationGenerator::kParamMaxLength, "Max Length",
                   NoteDurationGenerator::kMinDurationTicks,
                   NoteDurationGenerator::kMaxDurationTicks,
                   NoteDurationGenerator::kDefaultMaxTicks,
                   GeneratorParamHint::Steps};
    set.defs[3] = {NoteDurationGenerator::kParamVariation, "Variation", 0,
                   NoteDurationGenerator::kMaxVariation,
                   NoteDurationGenerator::kDefaultVariation,
                   GeneratorParamHint::Percent};
    set.count = 4;
    return set;
}

constexpr GeneratorParamSet kNoteDurationParamDefs = makeNoteDurationParamDefs();

inline const GeneratorParamSet& NoteDurationGenerator::paramDefs() const {
    return kNoteDurationParamDefs;
}

// Size bounds on the new plugin state (global constraint): one vtable
// pointer plus the two-byte window/base fields and the variation byte —
// a small fixed footprint.
static_assert(sizeof(NoteDurationGenerator) <= 16,
              "NoteDurationGenerator stays a small fixed object");

}  // namespace sequencer
