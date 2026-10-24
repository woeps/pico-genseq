#pragma once

#include "../common/PatternId.h"
#include "../common/ScaleTemplates.h"

#include <cstdint>

// Parameter addressing for the modulation layer (generative-engine-expansion
// task 16, FR12/FR13, AC3).
//
// A ParameterAddress names ONE UI-settable parameter in one place:
//   - scope: per-Pattern (the factory default, FR13) or Global (the option
//     global BPM needs, FR12);
//   - patternIndex: which of the fixed 16 pattern slots (ignored by the
//     Global scope);
//   - paramId: which parameter, from the fixed table below.
//
// The table covers EVERY existing UI-settable scalar — global BPM, the
// Euclidean gate configuration, the pitch/velocity set sizes and their
// playing orders — plus the new generator parameters and the scale
// parameters, each with its min/max (the same bounds the UI enforces),
// display name and factory default.
//
// Task 17 resolves these addresses through the ParameterBus; task 15's
// ModulationTarget binds one address per target.
//
// SDK-free: <cstdint> + the fixed common tables only. Everything is
// constexpr: the whole addressing layer is resolvable at compile time and
// needs no runtime initialization.

namespace sequencer {

// Where a parameter lives (FR13: per-pattern by default, global the option).
enum class ParameterScope : uint8_t {
    Pattern = 0,  // 16 fixed pattern slots, each with its own value
    Global = 1,   // one process-wide value (the transport timeline)
};

// Every addressable parameter (task-16 FR12/FR13). The ids are STABLE:
// configuration derived from ids (modulation targets, MIDI mappings,
// overlays) survives table edits; append new parameters before Count.
//
// Storage note: the per-pattern parameters are scalars of the generative
// working state (PatternGenerativeState), except BPM which is the global
// transport timeline's. Generator parameters share their per-role storage
// slot with whatever generator currently occupies that role; a ParamId fixes
// the semantic RANGE of the value addressed through it.
enum class ParamId : uint8_t {
    // --- Global scope (the transport timeline) ----------------------------
    GlobalBpm = 0,  // FR12: the global BPM, UI range 40-255 (default 120)

    // --- Per-pattern: gate generator parameters ---------------------------
    GateSteps = 1,       // Euclidean step count, UI range 1-64 (default 16)
    GatePulses = 2,      // Euclidean pulse count, UI range 0-64 (default 4)
    GateRotation = 3,    // Euclidean rotation, UI range 0-63 (default 0)
    GateProbability = 4, // probability fill amount, 0-127 (default 64)

    // --- Per-pattern: pitch generator parameters ---------------------------
    PitchLowNote = 5,  // scale range lower bound, MIDI 0-127 (default 48)
    PitchHighNote = 6, // scale range upper bound, MIDI 0-127 (default 72)

    // --- Per-pattern: duration generator parameters -------------------------
    DurationBaseTicks = 7,  // base note length, ticks 1-255 (default 24)
    DurationMinTicks = 8,   // minimum note length, ticks 1-255 (default 1)
    DurationMaxTicks = 9,  // maximum note length, ticks 1-255 (default 48)
    DurationVariation = 10, // random variation amount, 0-127 (default 0)

    // --- Per-pattern: scale parameters (FR3/FR4) -----------------------------
    ScaleRoot = 11,        // root pitch class 0-11 (C, C#, ... B; default 0)
    ScaleTemplate = 12,    // index into the 29 built-in templates (default 0)

    // --- Per-pattern: material set parameters (FR7-style UI set sizes) ------
    PitchSetCount = 13,       // pitch set size, 0-16 (default 0)
    PitchPlayingOrder = 14,   // FORWARDS/BACKWARDS/PENDULUM/RANDOM, 0-3
    VelocitySetCount = 15,    // velocity set size, 0-16 (default 0)
    VelocityPlayingOrder = 16, // playing order 0-3

    Count = 17,  // sentinel: number of table entries
};

// Number of table entries, as a plain integer for loop bounds and bit-level
// parameter fields (addresses are plain values, not enum class instances,
// everywhere outside this header's type-safe helpers).
constexpr uint8_t kParamIdCount = static_cast<uint8_t>(ParamId::Count);

// Static metadata for one parameter (task-16 FR12): display name, the same
// min/max bounds the UI enforces, the factory default, and the scope the
// parameter is addressable under.
struct ParamInfo {
    const char* name;
    int16_t minValue;
    int16_t maxValue;
    int16_t defaultValue;
    ParameterScope scope;
};

// The fixed parameter table, indexed by ParamId (SDK-free, read-only,
// shared across translation units, no dynamic initialization).
inline constexpr ParamInfo kParamInfos[kParamIdCount] = {
    // Global scope (the transport timeline).
    {"BPM", 40, 255, 120, ParameterScope::Global},

    // Per-pattern: gate generator parameters (role storage, GATE slots 0-2).
    {"Gate Steps", 1, 64, 16, ParameterScope::Pattern},
    {"Gate Pulses", 0, 64, 4, ParameterScope::Pattern},
    {"Gate Rotation", 0, 63, 0, ParameterScope::Pattern},
    {"Gate Probability", 0, 127, 64, ParameterScope::Pattern},

    // Per-pattern: pitch generator parameters (PITCH slots 0-1).
    {"Pitch Low Note", 0, 127, 48, ParameterScope::Pattern},
    {"Pitch High Note", 0, 127, 72, ParameterScope::Pattern},

    // Per-pattern: duration generator parameters (DURATION slots 0-3).
    {"Duration Base", 1, 255, 24, ParameterScope::Pattern},
    {"Duration Min", 1, 255, 1, ParameterScope::Pattern},
    {"Duration Max", 1, 255, 48, ParameterScope::Pattern},
    {"Duration Variation", 0, 127, 0, ParameterScope::Pattern},

    // Per-pattern: scale parameters (FR3/FR4).
    {"Scale Root", 0, 11, 0, ParameterScope::Pattern},
    {"Scale Template", 0,
     static_cast<int16_t>(common::scaleTemplateCount() - 1), 0,
     ParameterScope::Pattern},

    // Per-pattern: material set parameters (the fixed 16-value set capacity).
    {"Pitch Set Size", 0, 16, 0, ParameterScope::Pattern},
    {"Pitch Order", 0, 3, 0, ParameterScope::Pattern},
    {"Velocity Set Size", 0, 16, 0, ParameterScope::Pattern},
    {"Velocity Order", 0, 3, 0, ParameterScope::Pattern},
};

// One parameter in one place (task-16 FR12/FR13): scope + pattern slot +
// parameter id. The factory value is deliberately INVALID (per-pattern scope
// holding the global-only BPM id): an unconfigured address modulates
// nothing until a performer or the configuration layer binds it, so default
// behavior stays stable (AC1 of the global constraints).
struct ParameterAddress {
    ParameterScope scope = ParameterScope::Pattern;  // FR13: per-pattern default
    uint8_t patternIndex = 0;                        // slot within the fixed 16
    ParamId paramId = ParamId::GlobalBpm;            // deliberately unconfigured
};

constexpr bool operator==(const ParameterAddress& a, const ParameterAddress& b) {
    return a.scope == b.scope && a.patternIndex == b.patternIndex &&
           a.paramId == b.paramId;
}
constexpr bool operator!=(const ParameterAddress& a, const ParameterAddress& b) {
    return !(a == b);
}

// Metadata lookup by raw id (constexpr: resolves at compile time). An
// out-of-table id carries NO metadata (nullptr).
constexpr const ParamInfo* paramInfoOf(uint16_t paramId) {
    return paramId < kParamIdCount ? &kParamInfos[paramId] : nullptr;
}

// Validity (AC3): a valid address names a real table parameter, under the
// scope that parameter is defined in, with a pattern index inside the fixed
// pattern capacity. Anything else is invalid and every consumer of the
// addressing layer rejects it — the tick path never chases a bad pointer.
constexpr bool validAddress(const ParameterAddress& a) {
    if (a.scope != ParameterScope::Pattern && a.scope != ParameterScope::Global) {
        return false;  // unknown scope byte (e.g. a corrupt message field)
    }
    const ParamInfo* info = paramInfoOf(static_cast<uint16_t>(a.paramId));
    if (info == nullptr) {
        return false;  // unknown parameter id
    }
    if (info->scope != a.scope) {
        return false;  // scope mismatch: BPM is global-only, the rest per-pattern
    }
    if (a.scope == ParameterScope::Pattern &&
        a.patternIndex >= common::PATTERN_CAPACITY) {
        return false;  // pattern slot outside the fixed 16-slot capacity
    }
    return true;
}

}  // namespace sequencer
