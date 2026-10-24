#pragma once

#include <cstdint>

#include "ModulationPattern.h"
#include "TransportTimeline.h"

// Per-target modulation configuration (generative-engine-expansion task 15,
// FR11, FR13, FR14).
//
// A ModulationTarget binds one global ModulationPattern (by index into the
// ModulationRegistry) to one UI-settable parameter (by address) and describes
// HOW the pattern drives it:
//   - scaling factor + balance value (FR14): how deeply and how offset the
//     steps move the parameter;
//   - step duration (FR14): how much transport time — beats or bars — one
//     step occupies (FR11 beat/bar timing);
//   - playing order (FR11): forward / reverse / ping-pong / random, the
//     random order being a pure, DETERMINISTIC function of (seed, counter);
//   - scope (FR13): per-pattern by default, global as the option.
//
// The step position is always DERIVED from the authoritative transport
// position (TransportTimeline -> TransportPosition), never from a separate
// modulation clock (FR11/FR13/FR14).
//
// Both evaluation helpers are PURE functions of (fields, inputs): the tick
// path can call them freely with no hidden state, no heap and no locks.
// SDK-free: <cstdint> only, no Pico SDK types.

namespace sequencer {

// FR13: modulation targets are per-pattern by default; global is the option.
enum class ModulationScope : uint8_t {
    Pattern = 0,
    Global = 1,
};

// Address of the parameter a target modulates. Forward-compatible with the
// ParameterAddress of task 16 ({scope, patternIndex, paramId}): task 16's
// parameter table assigns the ids consumed here, so a target configured
// today resolves against that table unchanged.
struct ModulationParameterAddress {
    ModulationScope scope = ModulationScope::Pattern;  // FR13 default
    uint8_t patternIndex = 0;  // which pattern's parameter (scope == Pattern)
    uint16_t paramId = 0;      // id from the task-16 parameter table
};

// FR11: step timing is beat- or bar-relative, each step lasting `length`
// units. A zero length is guarded to one unit at evaluation time.
enum class StepUnit : uint8_t {
    Beat = 0,
    Bar = 1,
};

struct StepDuration {
    StepUnit unit = StepUnit::Beat;
    uint16_t length = 1;
};

// FR11: per-target playing order.
enum class ModulationOrder : uint8_t {
    Forward = 0,
    Reverse = 1,
    PingPong = 2,  // triangular: 0,1,...,n-1,n-2,...,1,0,...
    Random = 3,    // deterministic: index = hash(seed, counter) % numSteps
};

// Snapshot of the AUTHORITATIVE transport position the step index derives
// from. Built from the one TransportTimeline (see transportPositionOf); a
// modulation target never owns or advances a clock of its own.
struct TransportPosition {
    uint64_t beatIndex = 0;
    uint64_t barIndex = 0;
};

// Derive the modulation position snapshot from the authoritative timeline.
inline TransportPosition transportPositionOf(const TransportTimeline& timeline) {
    TransportPosition position;
    position.beatIndex = timeline.beatIndex();
    position.barIndex = timeline.barIndex();
    return position;
}

// Deterministic 64-bit mixer (splitmix64 finalizer) backing the Random
// playing order. A pure function: the same (seed, counter) always yields the
// same index, so random modulation is stable by default and recoverable
// (global constraint: all randomness uses deterministic seeded PRNGs).
inline uint64_t modulationMix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// One modulation target slot (FR13, FR14). A plain configurable struct: every
// field is bounded by its type, so configuration needs no clamping beyond
// the pattern's own 0-127 step rule.
struct ModulationTarget {
    // Which global ModulationPattern drives this target (index into the
    // ModulationRegistry's pattern pool).
    uint8_t patternIndex = 0;
    // Which parameter the overlay is written to (task-16 ParameterAddress).
    ModulationParameterAddress address{};
    // FR14 "scaling factor", 0-127 (uint8_t, naturally bounded):
    //   0   -> modulation disabled;
    //   64  -> a centered step maps 1:1 onto parameter units on a 0-127 range;
    //   127 -> the full step swing maps onto the full parameter range.
    uint8_t scaleFactor = 64;
    // FR14 "balance value": step-space offset applied BEFORE scaling, moving
    // the pattern's center (e.g. -16 biases the output downward).
    int8_t balance = 0;
    // FR14 "step duration": transport time one step occupies (FR11).
    StepDuration stepDuration{};
    // FR11 "playing order" (per target).
    ModulationOrder order = ModulationOrder::Forward;
    // Deterministic seed for ModulationOrder::Random.
    uint16_t randomSeed = 0;

    // FR11/FR13/FR14: the pattern-relative step index for a transport
    // position. The step counter advances every `stepDuration.length`
    // beats/bars of the AUTHORITATIVE position, then the per-target playing
    // order maps it onto [0, numSteps). `numSteps` is the bound pattern's
    // active length (single source of truth: the pattern), passed by the
    // caller — keeping this a pure function with no cached pattern state.
    // Total function: numSteps 0 reads as index 0.
    uint32_t stepIndexForTransport(const TransportPosition& position, uint8_t numSteps) const {
        if (numSteps == 0) return 0;
        const uint64_t unitLength =
            (stepDuration.length == 0) ? 1u : static_cast<uint64_t>(stepDuration.length);
        const uint64_t counter = (stepDuration.unit == StepUnit::Bar)
                                     ? (position.barIndex / unitLength)
                                     : (position.beatIndex / unitLength);
        return indexForCounter(counter, numSteps);
    }

    // The playing-order mapping of a monotonic step counter onto
    // [0, numSteps). Exposed for tests and for the registry processor (task
    // 17). Total function: numSteps 0 reads as index 0.
    uint32_t indexForCounter(uint64_t counter, uint8_t numSteps) const {
        if (numSteps == 0) return 0;
        switch (order) {
            case ModulationOrder::Forward:
                return static_cast<uint32_t>(counter % numSteps);
            case ModulationOrder::Reverse:
                return static_cast<uint32_t>(numSteps - 1u - (counter % numSteps));
            case ModulationOrder::PingPong: {
                if (numSteps == 1) return 0;
                const uint64_t period = 2u * static_cast<uint64_t>(numSteps - 1u);
                const uint64_t phase = counter % period;
                return static_cast<uint32_t>(
                    (phase < numSteps) ? phase : (period - phase));
            }
            case ModulationOrder::Random: {
                const uint64_t mixed = modulationMix64(
                    (static_cast<uint64_t>(randomSeed) << 32) ^ modulationMix64(counter));
                return static_cast<uint32_t>(mixed % numSteps);
            }
        }
        return 0;
    }

    // FR14 scaling/balance mapping of one pattern step value onto a target
    // parameter range — a PURE function of (fields, base, range, step):
    //
    //   centered = step + balance - 64                 (step-space deviation)
    //   off      = centered * scaleFactor * span / (64 * 127)
    //   output   = clamp(base + off, rangeMin, rangeMax)
    //
    // with span = rangeMax - rangeMin (clamped at 0), so one target config
    // scales sensibly across differently-ranged parameters. The step value is
    // passed explicitly (read via ModulationPattern::stepAt at the derived
    // index) to keep this function pure. 64-bit intermediates keep any
    // realistic parameter span overflow-free. Always clamped to the target
    // range, even when the modulation is idle (scale 0) or the range is
    // degenerate.
    int32_t applyToBase(int32_t base, int32_t rangeMin, int32_t rangeMax,
                        uint8_t stepValue) const {
        const int64_t span =
            (rangeMax > rangeMin) ? (static_cast<int64_t>(rangeMax) - rangeMin) : 0;
        const int64_t centered = static_cast<int64_t>(stepValue) + balance - 64;
        const int64_t offset = (centered * scaleFactor * span) / (64LL * 127LL);
        int64_t output = static_cast<int64_t>(base) + offset;
        if (output < rangeMin) output = rangeMin;
        if (output > rangeMax) output = rangeMax;
        return static_cast<int32_t>(output);
    }
};

}  // namespace sequencer
