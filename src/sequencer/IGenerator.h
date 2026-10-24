#pragma once

#include <cstdint>
#include <type_traits>

#include "../common/Scale.h"
#include "GeneratorCapabilities.h"

// Generator plugin interface (generative-engine-expansion task 2, FR1, AC1).
//
// IGenerator is the plugin contract the sequencer dispatches to and the UI
// introspects. Concrete generators live in src/sequencer/generators/, are
// listed ONLY in generators/RegisterGenerators.cpp (AC1: adding a plugin
// never touches sequencer.h / sequencer.cpp) and are looked up by id through
// GeneratorRegistry.
//
// Contract:
//   - id() / name()     — stable identity for dispatch, persistence, UI;
//   - capabilities()    — the FR1 feature bits the UI adapts to (AC8);
//   - paramDefs()       — the generator's fixed parameter table;
//   - generate(...)     — one step's decision, a PURE function of
//     (stepIndex, context, configuration): no generator keeps hidden
//     mutable state, so generation is deterministic and replayable.
//
// Global constraints honored here: no heap allocation, no RTTI and no
// exceptions on the generation path; all randomness flows through the
// deterministic, seedable GeneratorRng carried by GeneratorContext. SDK-free:
// <cstdint> plus the SDK-free Scale header only.

namespace sequencer {

// Deterministic, seedable RNG state for generator randomness (global
// constraint: all randomness uses a deterministic, seedable RNG so tests are
// reproducible). xorshift64*: 8 bytes, no heap, no shared hidden state. The
// CALLER owns and seeds the state per invocation, so identical (seed, step)
// inputs always produce identical outputs.
struct GeneratorRng {
    uint64_t state = 1;  // never 0 (the xorshift fixed point)

    constexpr GeneratorRng() = default;
    constexpr explicit GeneratorRng(uint64_t seed) : state(seed != 0 ? seed : 1) {}

    void reseed(uint64_t seed) { state = (seed != 0) ? seed : 1; }

    // Next raw value (full 64-bit multiplicative output).
    uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }

    // Uniform 0-255 byte (top bits of the multiplicative output).
    uint8_t nextByte() { return static_cast<uint8_t>(next() >> 56); }

    // Uniform value in [0, bound) with rejection sampling (no modulo bias);
    // total for bound 0 (returns 0 without consuming entropy).
    uint8_t nextBelow(uint8_t bound) {
        if (bound == 0) return 0;
        const uint8_t limit = static_cast<uint8_t>(256 % bound);
        uint8_t value;
        do { value = nextByte(); } while (value < limit);
        return static_cast<uint8_t>(value % bound);
    }
};

// Per-invocation generation inputs (FR1/FR4): the pattern length in steps,
// the pattern's scale constraint for pitch generators (may be null when the
// pattern carries none) and the deterministic RNG state.
struct GeneratorContext {
    uint8_t patternLength = 0;
    const common::Scale* scale = nullptr;
    GeneratorRng& rng;

    GeneratorContext(uint8_t length, const common::Scale* scaleIn, GeneratorRng& rngIn)
        : patternLength(length), scale(scaleIn), rng(rngIn) {}
};

// One step's generator decision (FR1). Each field is optional: a generator
// writes only what its capabilities declare and leaves the rest at the
// "absent" defaults, so a gate-only generator never disturbs stored pitch
// material. `duration` is measured in transport ticks (PPQN per beat).
struct GeneratorOutput {
    bool hasGate = false;
    bool gate = false;
    bool hasPitch = false;
    uint8_t pitch = 0;
    bool hasVelocity = false;
    uint8_t velocity = 0;
    bool hasDuration = false;
    uint16_t duration = 0;
};

// FR1: the generator plugin interface. AC1: a new generator implements this
// interface and is registered in generators/RegisterGenerators.cpp only —
// no core sequencer file changes.
class IGenerator {
public:
    virtual ~IGenerator() = default;

    // Stable, unique generator id (e.g. "builtin.euclidean").
    virtual const char* id() const = 0;
    // Human-readable display name for the UI.
    virtual const char* name() const = 0;
    // The feature bits this generator produces (FR1, AC8).
    virtual GeneratorCapabilities capabilities() const = 0;
    // The generator's fixed parameter table (FR1).
    virtual const GeneratorParamSet& paramDefs() const = 0;

    // Produce one step's decision. Pure: the output depends only on
    // (stepIndex, context, configuration); it never allocates and never
    // touches the transport — the sequencer owns when the material plays.
    virtual void generate(uint32_t stepIndex, const GeneratorContext& context,
                           GeneratorOutput& output) const = 0;
};

// Size bounds on the new POD state (global constraint): the RNG state and
// the output stay small fixed types with no dynamic parts.
static_assert(std::is_trivially_copyable<GeneratorRng>::value,
              "GeneratorRng must be trivially copyable");
static_assert(sizeof(GeneratorRng) <= 8, "GeneratorRng stays one machine word");
static_assert(std::is_trivially_copyable<GeneratorOutput>::value,
              "GeneratorOutput must be trivially copyable");
static_assert(sizeof(GeneratorOutput) <= 16, "GeneratorOutput stays a small fixed POD");

}  // namespace sequencer
