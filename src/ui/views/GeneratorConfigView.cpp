// Capability-driven generator configuration view (generative-engine-expansion
// task 18, FR1, AC8). See GeneratorConfigView.h for the design; this
// translation unit holds the control-list construction, the GEN_PARAM_SET
// command write-through (ui-integration T5) and the key-event front end.

#include "GeneratorConfigView.h"

#include "../../commands/command.h"
#include "../../sequencer/generators/EuclideanGateGenerator.h"
#include "../../sequencer/generators/NoteDurationGenerator.h"
#include "../../sequencer/generators/ProbabilityGateGenerator.h"
#include "../../sequencer/generators/ScaleAwarePitchGenerator.h"
#include "../Types.h"

#include <cstdio>
#include <cstring>

namespace ui {

namespace {

using sequencer::GeneratorRole;
using sequencer::ParamId;

// The capability bit a generation role is filtered on (FR1): a generator
// qualifies for a role exactly when its capability set contains the role's
// feature bit, so the per-role generator lists are capability-driven, never
// name-driven (AC8).
constexpr sequencer::GeneratorCapability capabilityForRole(GeneratorRole role) {
    switch (role) {
        case GeneratorRole::GATE: return sequencer::GeneratorCapability::GATE;
        case GeneratorRole::PITCH: return sequencer::GeneratorCapability::PITCH;
        case GeneratorRole::VELOCITY: return sequencer::GeneratorCapability::VELOCITY;
        case GeneratorRole::DURATION: return sequencer::GeneratorCapability::DURATION;
        default: return sequencer::GeneratorCapability::GATE;  // total: 4 roles
    }
}

// UI-side inverse of the task-16 parameter table: which ParameterBus ParamId
// addresses one generator parameter. The task-16 table fixes WHICH parameters
// are bus-addressable (its read/write paths hardcode exactly these (role,
// generator-param) pairs for the four built-ins), so this fixed table is the
// view's counterpart — a plugin parameter outside it still RENDERS (AC8) but
// cannot be edited through the bus. Adding a generator needs no core
// sequencer file change (AC1): its parameters appear automatically once they
// map onto the bus table; the mapping stays in the UI layer.
struct GeneratorParamBusMap {
    const char* generatorId;
    GeneratorRole role;
    uint8_t generatorParamId;
    ParamId busParamId;
};

// The fixed mapping for the four built-in plugins (ids mirror
// RegisterGenerators.cpp; parameter ids mirror the plugins' tables, which
// the task-16 bus table itself names through the same constants).
constexpr GeneratorParamBusMap kGeneratorParamBusMap[] = {
    {"builtin.euclidean", GeneratorRole::GATE,
     sequencer::EuclideanGateGenerator::kParamSteps, ParamId::GateSteps},
    {"builtin.euclidean", GeneratorRole::GATE,
     sequencer::EuclideanGateGenerator::kParamPulses, ParamId::GatePulses},
    {"builtin.euclidean", GeneratorRole::GATE,
     sequencer::EuclideanGateGenerator::kParamRotation, ParamId::GateRotation},
    {"builtin.probability", GeneratorRole::GATE,
     sequencer::ProbabilityGateGenerator::kParamProbability,
     ParamId::GateProbability},
    {"builtin.pitch", GeneratorRole::PITCH,
     sequencer::ScaleAwarePitchGenerator::kParamLowNote, ParamId::PitchLowNote},
    {"builtin.pitch", GeneratorRole::PITCH,
     sequencer::ScaleAwarePitchGenerator::kParamHighNote, ParamId::PitchHighNote},
    {"builtin.duration", GeneratorRole::DURATION,
     sequencer::NoteDurationGenerator::kParamBaseLength,
     ParamId::DurationBaseTicks},
    {"builtin.duration", GeneratorRole::DURATION,
     sequencer::NoteDurationGenerator::kParamMinLength,
     ParamId::DurationMinTicks},
    {"builtin.duration", GeneratorRole::DURATION,
     sequencer::NoteDurationGenerator::kParamMaxLength,
     ParamId::DurationMaxTicks},
    {"builtin.duration", GeneratorRole::DURATION,
     sequencer::NoteDurationGenerator::kParamVariation,
     ParamId::DurationVariation},
};

// The 3-character matrix labels for the four roles (drawLabel takes exactly
// a 4-byte array: three characters plus the NUL).
const char (&roleLabel(GeneratorRole role))[4] {
    switch (role) {
        case GeneratorRole::GATE: return "GAt";
        case GeneratorRole::PITCH: return "PtC";
        case GeneratorRole::VELOCITY: return "UEL";
        case GeneratorRole::DURATION: return "dur";
        default: return "GAt";  // total: 4 roles
    }
}

}  // namespace

GeneratorConfigView::GeneratorConfigView(sequencer::GeneratorRegistry& registry,
                                        hardware::LedMatrix& ledMatrix)
    : registry_(registry), ledMatrix_(ledMatrix) {
    // Seed Core0's UI cache with the task-16 factory defaults (the fixed
    // parameter table IS shared metadata — constexpr ROM data — while the
    // VALUES are the view's own Core0 state; the sequencer core keeps its
    // separate working state). Bounded: pattern slots x table entries.
    for (uint8_t p = 0; p < common::PATTERN_CAPACITY; ++p) {
        for (uint8_t id = 0; id < sequencer::kParamIdCount; ++id) {
            cache_[p][id] = sequencer::kParamInfos[id].defaultValue;
        }
    }
}

// --- role selection -----------------------------------------------------------

void GeneratorConfigView::selectRole(sequencer::GeneratorRole role) {
    if (!sequencer::PatternGenerativeState::validRole(role)) return;
    role_ = role;
    // Entering a role without a selection picks the first qualifying
    // generator, so the view is immediately usable.
    if (selection_[static_cast<uint8_t>(role)][0] == '\0' &&
        generatorCountForRole(role) > 0) {
        selectGeneratorForRole(role, 0);
        return;
    }
    selectedControl_ = 0;
}

sequencer::GeneratorRole GeneratorConfigView::selectedRole() const { return role_; }

void GeneratorConfigView::cycleRole() {
    const uint8_t next =
        static_cast<uint8_t>(static_cast<uint8_t>(role_) + 1u) %
        sequencer::kGeneratorRoleCount;
    selectRole(static_cast<GeneratorRole>(next));
}

// --- generator selection per role ---------------------------------------------

uint8_t GeneratorConfigView::generatorCountForRole(GeneratorRole role) const {
    if (!sequencer::PatternGenerativeState::validRole(role)) return 0;
    const sequencer::GeneratorCapability capability = capabilityForRole(role);
    uint8_t count = 0;
    const uint8_t registered = registry_.count();
    for (uint8_t i = 0; i < registered; ++i) {  // bounded: fixed registry
        const sequencer::IGenerator* generator = registry_.at(i);
        if (generator != nullptr && generator->capabilities().has(capability)) {
            ++count;
        }
    }
    return count;
}

const sequencer::IGenerator* GeneratorConfigView::generatorForRole(
    GeneratorRole role, uint8_t index) const {
    if (!sequencer::PatternGenerativeState::validRole(role)) return nullptr;
    const sequencer::GeneratorCapability capability = capabilityForRole(role);
    uint8_t matched = 0;
    const uint8_t registered = registry_.count();
    for (uint8_t i = 0; i < registered; ++i) {  // bounded: fixed registry
        const sequencer::IGenerator* generator = registry_.at(i);
        if (generator == nullptr || !generator->capabilities().has(capability)) {
            continue;
        }
        if (matched == index) return generator;
        ++matched;
    }
    return nullptr;
}

bool GeneratorConfigView::selectGeneratorForRole(GeneratorRole role, uint8_t index) {
    const sequencer::IGenerator* generator = generatorForRole(role, index);
    if (generator == nullptr) return false;
    const uint8_t r = static_cast<uint8_t>(role);
    const char* id = generator->id();
    if (id == nullptr) return false;
    // The id fits the fixed storage by construction (the registry rejects
    // nothing on length, so guard the copy the same way the pattern state
    // does: an overlong id is refused, never truncated).
    uint8_t len = 0;
    while (id[len] != '\0' && len < sequencer::kGeneratorIdCapacity) ++len;
    if (len >= sequencer::kGeneratorIdCapacity) return false;
    std::memcpy(selection_[r], id, static_cast<size_t>(len) + 1u);
    if (role == role_) selectedControl_ = 0;
    return true;
}

void GeneratorConfigView::cycleGeneratorForSelectedRole() {
    const uint8_t count = generatorCountForRole(role_);
    if (count == 0) return;
    const char* current = selectedGeneratorId(role_);
    uint8_t currentIdx = 0;
    for (uint8_t i = 0; i < count; ++i) {
        const sequencer::IGenerator* generator = generatorForRole(role_, i);
        if (generator != nullptr && current[0] != '\0' &&
            std::strcmp(generator->id(), current) == 0) {
            currentIdx = i;
            break;
        }
    }
    // Wrapping advance; a role whose selection is empty (or unregistered)
    // restarts at the first qualifying generator.
    const uint8_t next = static_cast<uint8_t>((currentIdx + 1u) % count);
    selectGeneratorForRole(role_, next);
}

const char* GeneratorConfigView::selectedGeneratorId(GeneratorRole role) const {
    if (!sequencer::PatternGenerativeState::validRole(role)) return "";
    return selection_[static_cast<uint8_t>(role)];
}

// --- controls ------------------------------------------------------------------

uint8_t GeneratorConfigView::controlCount() const {
    const sequencer::IGenerator* generator = selectedGenerator();
    return (generator != nullptr) ? generator->paramDefs().count : 0;
}

bool GeneratorConfigView::controlAt(uint8_t index, Control& out) const {
    const sequencer::GeneratorParamDef* def = selectedDef(index);
    if (def == nullptr) return false;
    out.name = def->name;
    out.minValue = def->minValue;
    out.maxValue = def->maxValue;
    out.defaultValue = def->defaultValue;
    out.hint = def->hint;
    sequencer::ParameterAddress address;
    out.editable = resolveAddress(*def, address);
    return true;
}

bool GeneratorConfigView::selectControl(uint8_t index) {
    if (index >= controlCount()) return false;
    selectedControl_ = index;
    return true;
}

uint8_t GeneratorConfigView::selectedControl() const { return selectedControl_; }

int32_t GeneratorConfigView::controlValue(uint8_t patternIndex, uint8_t index) const {
    const sequencer::GeneratorParamDef* def = selectedDef(index);
    if (def == nullptr) return 0;
    sequencer::ParameterAddress address;
    if (!resolveAddress(*def, address)) return def->defaultValue;
    if (patternIndex >= common::PATTERN_CAPACITY) return def->defaultValue;
    // Core0's own mirror of the edited values — never a live sequencer
    // read (no shared data structures between the cores).
    return cache_[patternIndex][static_cast<uint8_t>(address.paramId)];
}

bool GeneratorConfigView::setControlValue(uint8_t patternIndex, uint8_t index,
                                          int32_t value) {
    const sequencer::GeneratorParamDef* def = selectedDef(index);
    if (def == nullptr) return false;
    sequencer::ParameterAddress address;
    if (!resolveAddress(*def, address)) return false;  // no parameter address
    if (patternIndex >= common::PATTERN_CAPACITY) return false;
    // Clamp into the ParameterDef range first (the sequencer's bus clamps
    // again into the table range — identical bounds for every mapped
    // parameter).
    const int16_t clamped = clampToDef(value, *def);
    // Update Core0's cache first, then post the cross-core write as a
    // GEN_PARAM_SET command (the UI's only write path — never the bus).
    cache_[patternIndex][static_cast<uint8_t>(address.paramId)] = clamped;
    commands::sendGenParamCommand(
        patternIndex, static_cast<uint8_t>(address.paramId), clamped);
    return true;
}

bool GeneratorConfigView::adjustControl(uint8_t patternIndex, uint8_t index,
                                        int delta) {
    const int32_t current = controlValue(patternIndex, index);
    const sequencer::GeneratorParamDef* def = selectedDef(index);
    if (def == nullptr) return false;
    return setControlValue(patternIndex, index, current + delta);
}

// --- IView -----------------------------------------------------------------------

void GeneratorConfigView::onEnter() {
    std::printf("Entering Generator Config View\n");
    ledMatrix_.clear();
}

state::UIState GeneratorConfigView::handleEvent(const state::UIState& state,
                                               const events::Event& event) {
    // The edited values live in the view's local UI cache and cross the
    // cores only as GEN_PARAM_SET commands — not in the UIState (which
    // carries no generator fields) — so every path here returns the state
    // unchanged except the ESCAPE back-out to the parent view.
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return state;
    }

    const uint8_t pattern = state.selectedPattern;
    const bool coarse = (event.data.key.mods & mod::SHIFT) != 0;
    const int step = coarse ? 5 : 1;
    switch (combo(event.data.key.id, event.data.key.mods & ~mod::SHIFT)) {
        case combo(KeyId::ESCAPE):
            // Back out to the gate configuration view this view is drilled
            // into from (FR11: ESCAPE returns to the appropriate parent).
            // Held repeats are ignored, like the sibling views.
            if (event.type == events::EventType::KEY_PRESSED) {
                state::UIState next = state;
                next.currentView = state::ViewId::GATE_SET;
                return next;
            }
            break;
        case combo(KeyId::LEFT):
            // Previous control, wrapping within the selected generator's list.
            if (controlCount() > 0) {
                selectControl(selectedControl_ == 0
                                  ? static_cast<uint8_t>(controlCount() - 1)
                                  : static_cast<uint8_t>(selectedControl_ - 1));
            }
            break;
        case combo(KeyId::RIGHT):
            // Next control, wrapping.
            if (controlCount() > 0) {
                selectControl(static_cast<uint8_t>((selectedControl_ + 1u) %
                                                  controlCount()));
            }
            break;
        case combo(KeyId::UP):
            adjustControl(pattern, selectedControl_, +step);
            break;
        case combo(KeyId::DOWN):
            adjustControl(pattern, selectedControl_, -step);
            break;
        case combo(KeyId::ENTER):
            if (event.type == events::EventType::KEY_PRESSED) {
                cycleGeneratorForSelectedRole();
            }
            break;
        case combo(KeyId::TAB):
            if (event.type == events::EventType::KEY_PRESSED) {
                cycleRole();
            }
            break;
        default:
            break;
    }
    return state;
}

void GeneratorConfigView::render(const state::UIState& state) {
    ledMatrix_.clear();
    ledMatrix_.drawLabel(roleLabel(role_), 0xFFFFEE00);
    Control control;
    if (controlAt(selectedControl_, control)) {
        ledMatrix_.drawNumber(static_cast<int>(controlValue(state.selectedPattern,
                                                            selectedControl_)),
                              0xFFFF0011);
    }
}

// --- private helpers ---------------------------------------------------------------

const sequencer::IGenerator* GeneratorConfigView::selectedGenerator() const {
    return registry_.find(selectedGeneratorId(role_));
}

const sequencer::GeneratorParamDef* GeneratorConfigView::selectedDef(
    uint8_t index) const {
    const sequencer::IGenerator* generator = selectedGenerator();
    if (generator == nullptr) return nullptr;
    const sequencer::GeneratorParamSet& defs = generator->paramDefs();
    if (index >= defs.count) return nullptr;
    return &defs.defs[index];
}

bool GeneratorConfigView::resolveAddress(
    const sequencer::GeneratorParamDef& def,
    sequencer::ParameterAddress& address) const {
    const char* generatorId = selectedGeneratorId(role_);
    if (generatorId[0] == '\0') return false;
    // Bounded sweep over the fixed mapping table (10 entries).
    for (const GeneratorParamBusMap& mapping : kGeneratorParamBusMap) {
        if (mapping.role == role_ && mapping.generatorParamId == def.id &&
            std::strcmp(mapping.generatorId, generatorId) == 0) {
            address.scope = sequencer::ParameterScope::Pattern;
            address.patternIndex = 0;  // the caller fills the pattern slot
            address.paramId = mapping.busParamId;
            return true;
        }
    }
    return false;
}

int16_t GeneratorConfigView::clampToDef(int32_t value,
                                        const sequencer::GeneratorParamDef& def) {
    if (value < def.minValue) return def.minValue;
    if (value > def.maxValue) return def.maxValue;
    return static_cast<int16_t>(value);
}

}  // namespace ui
