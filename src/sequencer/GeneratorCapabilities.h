#pragma once

#include <cstdint>
#include <initializer_list>
#include <type_traits>

// Generator capability system (generative-engine-expansion task 2, FR1, AC8).
//
// The introspection half of the generator plugin contract a generator
// exposes to the registry and the UI:
//   - GeneratorCapability bitflags — WHAT the generator can produce
//     (gate / pitch / velocity / duration);
//   - GeneratorCapabilities — a bitflag SET wrapper with has() queries, so
//     the UI can show only the controls a generator actually uses (AC8);
//   - GeneratorParamDef — one parameter's identity, display name, range,
//     factory default and UI hint;
//   - GeneratorParamSet — the fixed-capacity parameter table every
//     generator returns through IGenerator::paramDefs() (no heap: a
//     generator's parameter list is a compile-time table);
//   - kMaxGeneratorParams / kMaxGenerators — the fixed limits backing the
//     parameter tables and the static registry storage.
//
// Global constraints honored here: plain value types with static size, no
// heap allocation, no RTTI, no exceptions. SDK-free: standard headers only,
// no Pico SDK types.

namespace sequencer {

// FR1: capability bitflags. One bit per supported feature so a generator
// declares exactly what it produces and the UI shows only those controls
// (AC8).
enum class GeneratorCapability : uint8_t {
    GATE = 1u << 0,      // emits on/off gate decisions per step
    PITCH = 1u << 1,     // emits MIDI note numbers (scale-constrained)
    VELOCITY = 1u << 2,  // emits note velocities (1-127)
    DURATION = 1u << 3,  // emits note lengths (in transport ticks)
};

// A bitflag SET over GeneratorCapability (FR1). A tiny value type wrapping
// one mask byte: pass around by value, query with has(), combine with
// operator| from either side.
class GeneratorCapabilities {
public:
    constexpr GeneratorCapabilities() = default;

    // Single-flag constructor (implicit: one flag IS a one-element set, so
    // call sites can pass a bare GeneratorCapability).
    constexpr GeneratorCapabilities(GeneratorCapability c)
        : mask_(static_cast<uint8_t>(c)) {}

    constexpr GeneratorCapabilities(std::initializer_list<GeneratorCapability> caps)
        : mask_(maskOf(caps)) {}

    // Set-style combination: this set plus one more flag.
    constexpr GeneratorCapabilities operator|(GeneratorCapability c) const {
        return GeneratorCapabilities(static_cast<uint8_t>(mask_ | static_cast<uint8_t>(c)));
    }

    constexpr bool has(GeneratorCapability c) const {
        return (mask_ & static_cast<uint8_t>(c)) != 0;
    }
    constexpr bool none() const { return mask_ == 0; }
    constexpr uint8_t mask() const { return mask_; }

    // From a raw mask byte (used by operator| below and by code that loads
    // a persisted mask).
    explicit constexpr GeneratorCapabilities(uint8_t mask) : mask_(mask) {}

private:
    static constexpr uint8_t maskOf(std::initializer_list<GeneratorCapability> caps) {
        uint8_t mask = 0;
        for (GeneratorCapability c : caps) mask = static_cast<uint8_t>(mask | static_cast<uint8_t>(c));
        return mask;
    }

    uint8_t mask_ = 0;
};

// Combine two bare flags into one set.
constexpr GeneratorCapabilities operator|(GeneratorCapability a, GeneratorCapability b) {
    return GeneratorCapabilities(static_cast<uint8_t>(static_cast<uint8_t>(a) |
                                                       static_cast<uint8_t>(b)));
}

// AC8: how the UI best presents one parameter. A bounded scalar hint keeps
// the UI generic — it never needs to know the concrete generator type.
enum class GeneratorParamHint : uint8_t {
    None = 0,  // plain bounded value
    Percent,   // 0-100 style percentage
    Toggle,    // two-state value (min / max)
    Steps,     // pattern-relative step count
    Note,      // MIDI note number (0-127)
};

// FR1: one generator parameter definition. `id` is unique within its
// generator (the sequencer and the UI address parameters by it);
// `minValue`/`maxValue` bound every configured value; `defaultValue`
// restores the factory configuration; `hint` drives generic UI presentation
// (AC8).
struct GeneratorParamDef {
    uint8_t id = 0;
    const char* name = "";
    int16_t minValue = 0;
    int16_t maxValue = 0;
    int16_t defaultValue = 0;
    GeneratorParamHint hint = GeneratorParamHint::None;
};

// Fixed limits backing the static storages (global constraint: fixed
// capacity everywhere). kMaxGenerators sizes GeneratorRegistry; it holds the
// four built-in plugins (T4-T7) plus headroom.
constexpr uint8_t kMaxGeneratorParams = 8;
constexpr uint8_t kMaxGenerators = 8;

// FR1: the fixed-capacity parameter table every generator exposes through
// IGenerator::paramDefs(). Slots at or beyond `count` are unused (zeroed
// defs); a generator's list is a compile-time table — no heap, no growth.
struct GeneratorParamSet {
    GeneratorParamDef defs[kMaxGeneratorParams] = {};
    uint8_t count = 0;
};

// Size bounds on the new POD state (global constraint).
static_assert(std::is_trivially_copyable<GeneratorParamDef>::value,
              "GeneratorParamDef must be trivially copyable");
static_assert(sizeof(GeneratorParamDef) <= 24, "GeneratorParamDef stays a small fixed POD");
static_assert(std::is_trivially_copyable<GeneratorParamSet>::value,
              "GeneratorParamSet must be trivially copyable");
static_assert(sizeof(GeneratorParamSet) <= 256,
              "GeneratorParamSet stays a small fixed POD");
static_assert(kMaxGeneratorParams >= 3,
              "a generator must be able to declare at least three parameters");
static_assert(kMaxGenerators >= 4,
              "the registry must hold at least the four built-in generators");

}  // namespace sequencer
