#pragma once

#include "ParameterAddress.h"
#include "TransportTimeline.h"
#include "WorkingStateManager.h"
#include "generators/EuclideanGateGenerator.h"
#include "generators/NoteDurationGenerator.h"
#include "generators/ProbabilityGateGenerator.h"
#include "generators/ScaleAwarePitchGenerator.h"

#include <cstdint>

// Parameter bus (generative-engine-expansion task 16, FR12/FR13, AC3).
//
// One fixed-capacity addressing surface over EVERY UI-settable parameter:
// the STORED base values are the live sequencer state — the transport
// timeline for global BPM, the per-pattern generative working state for
// everything else — while MODULATION overlays live in separate fixed arrays
// owned by the bus. The invariant (AC3, task-16 step 4): a modulation write
// is setOverlay — it changes effectiveValue and NOTHING else. Stored values,
// undo snapshots and the saved baseline stay exactly what the user set, so
// Milestone 2's data-integrity guarantees hold unchanged.
//
//   - baseValue/setBase: the stored value the UI and the mutation engine
//     edit (setBase clamps into the parameter's UI range first);
//   - setOverlay/clearOverlay: the modulation layer's ONLY write path — an
//     ABSOLUTE effective value (the clamped base when a target is at its
//     center), never a delta, so it is independent of the base below it;
//   - effectiveValue: the value generators/the tick path read — the overlay
//     when one is active, otherwise the base; always clamped to range, even
//     when a stale stored byte lies outside it;
//   - paramInfo: the (min, max, name) metadata from the task-16 table.
//
// Fixed footprint (global constraint): plain arrays sized by the fixed
// pattern capacity and parameter-table count — no heap, no unbounded loops,
// no dynamic initialization, no locks; every operation is O(1) pointer
// arithmetic or a bounded table sweep. SDK-free: <cstdint> + headers only.
//
// The bus is BINDABLE rather than owning state (one bus per sequencer, but
// unit tests drive it with local state): bind() attaches the timeline and
// working-state manager whose entries the base reads/writes address.
// Overlays are bus-local and need no backing, so they are configurable
// before (or without) binding; effectiveValue of an overlay is absolute and
// likewise works unbound. An UNBOUND or invalid base read degrades safely
// to 0; base writes are rejected — the tick path never chases a bad
// pointer, and rejected writes leave everything untouched.

namespace sequencer {

class ParameterBus {
public:
    // Fixed capacities, mirrored from the state layer (16 pattern slots).
    static constexpr uint8_t kPatternCount = WorkingStateManager::kPatternCount;

    // --- binding ------------------------------------------------------------
    // Attach the live state the base values live in (the Sequencer binds
    // its OWN timeline and working-state manager). Both pointers must be
    // non-null; idempotent (re-binding the same pair is a no-op). Overlays
    // are unaffected by (re)binding.
    bool bind(TransportTimeline* timeline, WorkingStateManager* working) {
        if (timeline == nullptr || working == nullptr) return false;
        timeline_ = timeline;
        working_ = working;
        return true;
    }
    bool bound() const { return timeline_ != nullptr && working_ != nullptr; }

    // --- base values: the stored sequencer state -----------------------------
    // The base of a valid address, or 0 when the address is invalid or the
    // bus is unbound. This is the value the UI, the mutation engine, undo
    // and the saved baseline all see — never what modulation changed.
    int32_t baseValue(const ParameterAddress& address) const {
        const ParamInfo* info = paramInfo(address);
        if (info == nullptr || !bound()) return 0;
        return readBase(address, *info);
    }

    // Edit the stored base: clamps `value` into the parameter's UI range and
    // writes it through to the backing state (timeline / working state).
    // Returns false on an invalid address or an unbound bus.
    bool setBase(const ParameterAddress& address, int32_t value) {
        const ParamInfo* info = paramInfo(address);
        if (info == nullptr || !bound()) return false;
        return writeBase(address, *info, clamp(value, *info));
    }

    // --- overlays: the modulation layer's write surface ----------------------
    // Arm the modulation overlay for a valid address: an ABSOLUTE effective
    // value, clamped into the parameter's UI range. NEVER touches the
    // stored base, undo or the saved baseline (AC3). Overlays are bus-local
    // and need no backing, so they work before/without a bind.
    bool setOverlay(const ParameterAddress& address, int32_t value) {
        if (!validAddress(address)) return false;
        const ParamInfo& info = *paramInfoOf(static_cast<uint16_t>(address.paramId));
        if (address.scope == ParameterScope::Pattern) {
            patternOverlay_[address.patternIndex][static_cast<uint8_t>(
                address.paramId)] = clamp(value, info);
            patternOverlayActive_[address.patternIndex][static_cast<uint8_t>(
                address.paramId)] = true;
        } else {
            globalOverlay_[static_cast<uint8_t>(address.paramId)] = clamp(value, info);
            globalOverlayActive_[static_cast<uint8_t>(address.paramId)] = true;
        }
        return true;
    }

    // Drop one overlay (returns to the base). Clearing an inactive overlay
    // is a safe no-op that still reports success; invalid addresses are
    // rejected and change nothing.
    bool clearOverlay(const ParameterAddress& address) {
        if (!validAddress(address)) return false;
        if (address.scope == ParameterScope::Pattern) {
            patternOverlayActive_[address.patternIndex][static_cast<uint8_t>(
                address.paramId)] = false;
        } else {
            globalOverlayActive_[static_cast<uint8_t>(address.paramId)] = false;
        }
        return true;
    }

    // Drop EVERY overlay at once (the bounded fixed arrays sweep — the
    // transport-stop / Play-restart reset, task 17).
    void clearAllOverlays() {
        for (uint8_t p = 0; p < kPatternCount; ++p) {
            for (uint8_t id = 0; id < kParamIdCount; ++id) {
                patternOverlayActive_[p][id] = false;
            }
        }
        for (uint8_t id = 0; id < kParamIdCount; ++id) {
            globalOverlayActive_[id] = false;
        }
    }

    // Whether an overlay is armed at this address (false on invalid).
    bool hasOverlay(const ParameterAddress& address) const {
        if (!validAddress(address)) return false;
        if (address.scope == ParameterScope::Pattern) {
            return patternOverlayActive_[address.patternIndex][static_cast<uint8_t>(
                address.paramId)];
        }
        return globalOverlayActive_[static_cast<uint8_t>(address.paramId)];
    }

    // --- effective values: what the tick path reads ----------------------------
    // The overlay when one is armed (absolute, so it needs no backing);
    // otherwise the clamped base. Always inside the parameter's UI range.
    // Invalid addresses degrade to 0.
    int32_t effectiveValue(const ParameterAddress& address) const {
        const ParamInfo* info = paramInfo(address);
        if (info == nullptr) return 0;
        if (hasOverlay(address)) {
            return address.scope == ParameterScope::Pattern
                       ? patternOverlay_[address.patternIndex][static_cast<uint8_t>(
                             address.paramId)]
                       : globalOverlay_[static_cast<uint8_t>(address.paramId)];
        }
        if (!bound()) return 0;
        return clamp(readBase(address, *info), *info);
    }

    // --- metadata ---------------------------------------------------------------
    // (min, max, name, default) of the parameter an address names — nullptr
    // when the address is invalid. By raw ParamId: nullptr for unknown ids.
    const ParamInfo* paramInfo(const ParameterAddress& address) const {
        if (!validAddress(address)) return nullptr;
        return paramInfoOf(static_cast<uint16_t>(address.paramId));
    }
    const ParamInfo* paramInfo(ParamId id) const {
        return paramInfoOf(static_cast<uint16_t>(id));
    }

private:
    // Clamp into the parameter's UI range (the same bounds the UI enforces).
    static int16_t clamp(int32_t value, const ParamInfo& info) {
        if (value < info.minValue) return info.minValue;
        if (value > info.maxValue) return info.maxValue;
        return static_cast<int16_t>(value);
    }

    // Read the stored base: the live backing state, unclamped (the stored
    // byte is reported as-is so mis-stored values stay diagnosable;
    // effectiveValue clamps on read).
    int32_t readBase(const ParameterAddress& a, const ParamInfo& info) const {
        (void)info;
        const uint8_t slot =
            a.scope == ParameterScope::Pattern ? a.patternIndex : uint8_t{0};
        const PatternGenerativeState* s = working_->working(slot);
        if (s == nullptr) return 0;  // total: validAddress bounds the slot, guard anyway
        switch (a.paramId) {
            case ParamId::GlobalBpm: return timeline_->bpm();
            case ParamId::GateSteps:
                return s->generatorParam(GeneratorRole::GATE,
                                         EuclideanGateGenerator::kParamSteps);
            case ParamId::GatePulses:
                return s->generatorParam(GeneratorRole::GATE,
                                         EuclideanGateGenerator::kParamPulses);
            case ParamId::GateRotation:
                return s->generatorParam(GeneratorRole::GATE,
                                         EuclideanGateGenerator::kParamRotation);
            case ParamId::GateProbability:
                return s->generatorParam(GeneratorRole::GATE,
                                         ProbabilityGateGenerator::kParamProbability);
            case ParamId::PitchLowNote:
                return s->generatorParam(GeneratorRole::PITCH,
                                         ScaleAwarePitchGenerator::kParamLowNote);
            case ParamId::PitchHighNote:
                return s->generatorParam(GeneratorRole::PITCH,
                                         ScaleAwarePitchGenerator::kParamHighNote);
            case ParamId::DurationBaseTicks:
                return s->generatorParam(GeneratorRole::DURATION,
                                         NoteDurationGenerator::kParamBaseLength);
            case ParamId::DurationMinTicks:
                return s->generatorParam(GeneratorRole::DURATION,
                                         NoteDurationGenerator::kParamMinLength);
            case ParamId::DurationMaxTicks:
                return s->generatorParam(GeneratorRole::DURATION,
                                         NoteDurationGenerator::kParamMaxLength);
            case ParamId::DurationVariation:
                return s->generatorParam(GeneratorRole::DURATION,
                                         NoteDurationGenerator::kParamVariation);
            case ParamId::ScaleRoot: return s->scaleRoot;
            case ParamId::ScaleTemplate: return s->scaleTemplateIndex;
            case ParamId::PitchSetCount: return s->pitchSet.count;
            case ParamId::PitchPlayingOrder:
                return static_cast<int32_t>(s->pitchSet.order);
            case ParamId::VelocitySetCount: return s->velocitySet.count;
            case ParamId::VelocityPlayingOrder:
                return static_cast<int32_t>(s->velocitySet.order);
            default: return 0;
        }
    }

    // Write the (already clamped) base through to the backing state. The
    // working-state manager records undo history AROUND mutations; a direct
    // base write is the UI/configuration path and does not snapshot.
    bool writeBase(const ParameterAddress& a, const ParamInfo& info, int16_t v) {
        const uint8_t slot =
            a.scope == ParameterScope::Pattern ? a.patternIndex : uint8_t{0};
        PatternGenerativeState* s = working_->working(slot);
        if (s == nullptr) return false;  // total: validAddress bounds the slot, guard anyway
        switch (a.paramId) {
            case ParamId::GlobalBpm: return timeline_->setBpm(v);
            case ParamId::GateSteps:
                s->setGeneratorParam(GeneratorRole::GATE,
                                     EuclideanGateGenerator::kParamSteps, v);
                return true;
            case ParamId::GatePulses:
                s->setGeneratorParam(GeneratorRole::GATE,
                                     EuclideanGateGenerator::kParamPulses, v);
                return true;
            case ParamId::GateRotation:
                s->setGeneratorParam(GeneratorRole::GATE,
                                     EuclideanGateGenerator::kParamRotation, v);
                return true;
            case ParamId::GateProbability:
                s->setGeneratorParam(GeneratorRole::GATE,
                                     ProbabilityGateGenerator::kParamProbability, v);
                return true;
            case ParamId::PitchLowNote:
                s->setGeneratorParam(GeneratorRole::PITCH,
                                     ScaleAwarePitchGenerator::kParamLowNote, v);
                return true;
            case ParamId::PitchHighNote:
                s->setGeneratorParam(GeneratorRole::PITCH,
                                     ScaleAwarePitchGenerator::kParamHighNote, v);
                return true;
            case ParamId::DurationBaseTicks:
                s->setGeneratorParam(GeneratorRole::DURATION,
                                     NoteDurationGenerator::kParamBaseLength, v);
                return true;
            case ParamId::DurationMinTicks:
                s->setGeneratorParam(GeneratorRole::DURATION,
                                     NoteDurationGenerator::kParamMinLength, v);
                return true;
            case ParamId::DurationMaxTicks:
                s->setGeneratorParam(GeneratorRole::DURATION,
                                     NoteDurationGenerator::kParamMaxLength, v);
                return true;
            case ParamId::DurationVariation:
                s->setGeneratorParam(GeneratorRole::DURATION,
                                     NoteDurationGenerator::kParamVariation, v);
                return true;
            case ParamId::ScaleRoot:
                s->scaleRoot = static_cast<uint8_t>(v);
                return true;
            case ParamId::ScaleTemplate:
                s->scaleTemplateIndex = static_cast<uint8_t>(v);
                return true;
            case ParamId::PitchSetCount:
                s->pitchSet.count = static_cast<uint8_t>(v);
                return true;
            case ParamId::PitchPlayingOrder:
                s->pitchSet.order = static_cast<common::PlayingOrder>(v);
                return true;
            case ParamId::VelocitySetCount:
                s->velocitySet.count = static_cast<uint8_t>(v);
                return true;
            case ParamId::VelocityPlayingOrder:
                s->velocitySet.order = static_cast<common::PlayingOrder>(v);
                return true;
            default:
                (void)info;
                return false;
        }
    }

    // Fixed overlay storage (AC3: separate from EVERY stored value):
    // one absolute value per (scope, pattern, parameter). Row-indexed by
    // the fixed pattern capacity / parameter-table count — no heap, no
    // growth. A pattern row's BPM cell and the global row's per-pattern
    // cells are dead weight (that pairing is invalid by validAddress);
    // the uniform indexing keeps every operation a single array hit.
    int16_t patternOverlay_[kPatternCount][kParamIdCount] = {};
    bool patternOverlayActive_[kPatternCount][kParamIdCount] = {};
    int16_t globalOverlay_[kParamIdCount] = {};
    bool globalOverlayActive_[kParamIdCount] = {};

    // The backing state (never owned; the Sequencer binds its own).
    TransportTimeline* timeline_ = nullptr;
    WorkingStateManager* working_ = nullptr;
};

// Fixed RAM budget (task-16 step 3): the bus is a plain member, statically
// sized by the fixed capacities.
static_assert(sizeof(ParameterBus) <= 1024,
              "ParameterBus stays within its fixed RAM budget");

}  // namespace sequencer
