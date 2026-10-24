#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "GeneratorCapabilities.h"  // kMaxGeneratorParams (fixed param capacity)
#include "../common/Scale.h"        // FR4 scale constraint value type
#include "../common/pitch_set.h"    // common::PlayingOrder

// Per-pattern generative state (generative-engine-expansion task 3, FR1,
// FR4, AC1).
//
// PatternGenerativeState is the fixed-size POD every pattern OWNS: the
// configuration layer of the generative engine. It carries
//
//   - the generator id assigned to each of the four generation roles (gate,
//     pitch, velocity, duration) — the sequencer resolves these by id through
//     GeneratorRegistry only, so it never names a concrete generator type
//     (AC1). A role with an empty id (or an id nothing registered) falls
//     back to the pattern's stored material;
//   - the configured parameter values for each role's generator, indexed by
//     the generator's GeneratorParamDef::id;
//   - the scale constraint (FR4): root pitch class + built-in template index,
//     constraining pitch generation and pitch mutation (AC2);
//   - the generative material sets (pitch / velocity / duration): contents
//     plus playing order, the substrate mutations permute (T9, FR7).
//
// Global constraints honored here: a plain fixed-capacity value type — no
// heap, no pointers (generator ids are embedded char arrays so the whole
// state snapshots byte-identically for undo / baseline / Keep, T12/T13), no
// RTTI, no exceptions; static size bounds below. Runtime positions (step
// counters, gate edge memory) live in the Sequencer, NEVER here, so
// snapshots of this state stay replayable. SDK-free: standard headers plus
// the SDK-free Scale/pitch_set headers only, so it compiles unchanged in the
// firmware and the host test build.

namespace sequencer {

// The four generation roles a pattern can delegate to generator plugins.
// The ordinal is the per-role array index used across this header.
enum class GeneratorRole : uint8_t {
    GATE = 0,
    PITCH = 1,
    VELOCITY = 2,
    DURATION = 3,
};

// Number of generation roles (bounds every per-role array below).
constexpr uint8_t kGeneratorRoleCount = 4;

// Fixed capacity for one stored generator id string (bytes including the
// terminating NUL). Ids are short dotted names ("builtin.euclidean");
// setGeneratorId REJECTS an overlong id instead of truncating it — a
// truncated prefix could alias a different generator.
constexpr uint8_t kGeneratorIdCapacity = 24;

// Fixed capacity for the generative value sets, matching the wire-protocol
// set limit (commands::MAX_PITCH_SET_LENGTH = 16).
constexpr uint8_t kGenerativeSetCapacity = 16;

// One fixed-capacity ordered value set: `count` entries of `values` are
// live, the rest is padding; `order` is the playing order. A plain POD the
// mutation operators (T9) permute in place.
struct GenerativeValueSet {
    uint8_t values[kGenerativeSetCapacity] = {};
    uint8_t count = 0;
    common::PlayingOrder order = common::PlayingOrder::FORWARDS;

    bool empty() const { return count == 0; }
};

// FR4: the per-pattern generative state (see file comment). Default
// construction yields a VALID, inert configuration: no generators assigned
// (pure stored-data playback), C-Major scale constraint, empty material
// sets traversed forwards.
struct PatternGenerativeState {
    // Generator id per role ("" = unassigned: the role falls back to the
    // stored pattern data). Embedded char storage, not pointers.
    char generatorIds[kGeneratorRoleCount][kGeneratorIdCapacity] = {};

    // Configured parameter values for each role's generator, indexed by the
    // generator's GeneratorParamDef::id. int16 matches the param-def range.
    int16_t generatorParams[kGeneratorRoleCount][kMaxGeneratorParams] = {};

    // Scale constraint (FR4): root pitch class 0-11 and index into the
    // built-in template table (common::SCALE_TEMPLATES).
    uint8_t scaleRoot = 0;
    uint8_t scaleTemplateIndex = 0;

    // Generative material per role (contents + playing order).
    GenerativeValueSet pitchSet;
    GenerativeValueSet velocitySet;
    GenerativeValueSet durationSet;

    // --- role / id helpers ---------------------------------------------------

    // True when `role` names one of the four generation roles.
    static bool validRole(GeneratorRole role) {
        return static_cast<uint8_t>(role) < kGeneratorRoleCount;
    }

    // True when the role has a (non-empty) generator id assigned.
    bool hasGenerator(GeneratorRole role) const {
        const uint8_t r = static_cast<uint8_t>(role);
        return r < kGeneratorRoleCount && generatorIds[r][0] != '\0';
    }

    // The role's assigned id, or "" when unassigned (or the role is out of
    // range — defensive total behavior, never out-of-bounds).
    const char* generatorId(GeneratorRole role) const {
        const uint8_t r = static_cast<uint8_t>(role);
        return (r < kGeneratorRoleCount) ? generatorIds[r] : "";
    }

    // Assign `id` to `role`. Fails (returning false, state unchanged) for an
    // out-of-range role, a null id, an empty id (use clearGeneratorId) or an
    // id longer than the fixed storage.
    bool setGeneratorId(GeneratorRole role, const char* id) {
        const uint8_t r = static_cast<uint8_t>(role);
        if (r >= kGeneratorRoleCount || id == nullptr || id[0] == '\0') return false;
        uint8_t len = 0;
        while (id[len] != '\0' && len < kGeneratorIdCapacity) ++len;
        if (len >= kGeneratorIdCapacity) return false;  // too long: no truncation
        std::memcpy(generatorIds[r], id, static_cast<size_t>(len) + 1u);
        return true;
    }

    // Unassign the role (fall back to the stored pattern data).
    void clearGeneratorId(GeneratorRole role) {
        const uint8_t r = static_cast<uint8_t>(role);
        if (r < kGeneratorRoleCount) generatorIds[r][0] = '\0';
    }

    // True when at least one role has a generator assigned. The Sequencer's
    // hot-path guard: pure stored-data playback skips dispatch entirely.
    bool anyGeneratorAssigned() const {
        for (uint8_t r = 0; r < kGeneratorRoleCount; ++r) {
            if (generatorIds[r][0] != '\0') return true;
        }
        return false;
    }

    // --- parameter helpers -----------------------------------------------------

    // The configured value for (role, paramId), or 0 when out of range.
    int16_t generatorParam(GeneratorRole role, uint8_t paramId) const {
        const uint8_t r = static_cast<uint8_t>(role);
        if (r >= kGeneratorRoleCount || paramId >= kMaxGeneratorParams) return 0;
        return generatorParams[r][paramId];
    }

    // Configure one parameter value; out-of-range indices are ignored.
    void setGeneratorParam(GeneratorRole role, uint8_t paramId, int16_t value) {
        const uint8_t r = static_cast<uint8_t>(role);
        if (r >= kGeneratorRoleCount || paramId >= kMaxGeneratorParams) return;
        generatorParams[r][paramId] = value;
    }

    // FR4: the scale constraint as the value type the generator context
    // carries (root and template index, validated/clamped by Scale).
    common::Scale scale() const { return common::Scale(scaleRoot, scaleTemplateIndex); }
};

// Size bounds on the new POD state (global constraint): trivially copyable,
// standard layout, a small fixed footprint.
static_assert(std::is_trivially_copyable<GenerativeValueSet>::value,
              "GenerativeValueSet must be trivially copyable");
static_assert(std::is_standard_layout<GenerativeValueSet>::value,
              "GenerativeValueSet must be standard layout");
static_assert(sizeof(GenerativeValueSet) <= 32,
              "GenerativeValueSet stays a small fixed POD");
static_assert(std::is_trivially_copyable<PatternGenerativeState>::value,
              "PatternGenerativeState must be trivially copyable");
static_assert(std::is_standard_layout<PatternGenerativeState>::value,
              "PatternGenerativeState must be standard layout");
static_assert(sizeof(PatternGenerativeState) <= 256,
              "PatternGenerativeState stays a small fixed POD");
static_assert(kGeneratorRoleCount == 4,
              "exactly the four generation roles: gate, pitch, velocity, duration");
static_assert(kGeneratorIdCapacity >= 20,
              "generator ids fit the builtin.* naming scheme");
static_assert(kGenerativeSetCapacity >= 16,
              "the sets hold at least the wire-protocol set limit");

}  // namespace sequencer
