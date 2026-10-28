#pragma once

#include <cstdint>

#include "IView.h"
#include "../hardware/LedMatrix.h"
#include "../../sequencer/GeneratorCapabilities.h"
#include "../../sequencer/GeneratorRegistry.h"
#include "../../sequencer/IGenerator.h"
#include "../../sequencer/ParameterAddress.h"
#include "../../sequencer/PatternGenerativeState.h"

namespace ui {

// Capability-driven generator configuration view
// (generative-engine-expansion task 18, FR1, AC8).
//
// The view shows the generator selection for each of the four generation
// roles (gate / pitch / velocity / duration) and, for the selected role's
// generator, ONLY the controls that generator actually uses: the control
// list is built at runtime from the GeneratorRegistry's capability bits
// (which generators even qualify for a role) and the selected generator's
// GeneratorParamDefs (which controls appear) — the view never names a
// concrete generator type, so a newly registered plugin renders its own
// controls with no view code change (AC8's plugin half of AC1).
//
// Edits ride the COMMAND QUEUE (ui-integration T5): a committed edit posts a
// GEN_PARAM_SET command (pattern, paramId, value) to the sequencer core —
// the view NEVER touches the ParameterBus. Core0 keeps its OWN local UI
// cache of the edited values (seeded from the task-16 factory defaults,
// updated by every write), so reads and relative adjustments stay pure
// Core0 state — no data structure is shared between the cores. The
// sequencer applies the command as a plain bus BASE write (the stored live
// per-pattern generative working state changes; no modulation overlay is
// ever armed — overlays belong to the modulation layer alone).
// Every edit is clamped to the control's ParameterDef min/max before it is
// cached and sent.
//
// Selection state is view-local by design: UIState carries no generator
// configuration fields (adding them is outside task 18's declared scope),
// so the role / generator / control selection lives in this view while the
// edited VALUES live in the single authoritative sequencer state through
// the bus. handleEvent therefore returns the UIState unchanged.
//
// Key mapping (documented, follows the GateSetView conventions):
//   LEFT / RIGHT   - select the previous / next control (wrapping)
//   UP / DOWN      - adjust the selected control by 1 (SHIFT: coarse 5)
//   ENTER          - cycle to the next generator for the selected role
//   TAB            - cycle the selected role (gate/pitch/velocity/duration)
//   ESCAPE         - return to the GATE_SET view this view was drilled into
//                    from (the ENTER drill-in path is GATE_SET -> TAB ->
//                    GENERATOR_CONFIG; ESCAPE walks straight back)
//
// Global constraints honored here: no heap allocation, no unbounded loops
// (every operation is a bounded sweep over the fixed registry / parameter
// tables), fixed-capacity storage (the per-role id selections are embedded
// char arrays sized by kGeneratorIdCapacity; the UI cache is a fixed
// pattern x parameter table). The view never touches the tick path and
// never shares state with the sequencer core — commands only.
class GeneratorConfigView : public IView {
public:
    // One visible control, mirroring one GeneratorParamDef of the selected
    // generator. `editable` reports whether the parameter resolves to an
    // address in the fixed task-16 parameter table (the built-in generator
    // parameters do; a plugin's custom parameters render but cannot be
    // edited through the bus).
    struct Control {
        const char* name = "";
        int16_t minValue = 0;
        int16_t maxValue = 0;
        int16_t defaultValue = 0;
        sequencer::GeneratorParamHint hint = sequencer::GeneratorParamHint::None;
        bool editable = false;
    };

    GeneratorConfigView(sequencer::GeneratorRegistry& registry,
                        hardware::LedMatrix& ledMatrix);

    // --- role selection --------------------------------------------------
    // Select which generation role the view edits. A role without a stored
    // selection auto-selects the first registered generator whose
    // capabilities cover the role (registration order), so entering a role
    // always shows something usable. An out-of-range role is ignored.
    void selectRole(sequencer::GeneratorRole role);
    sequencer::GeneratorRole selectedRole() const;
    // Cycle GATE -> PITCH -> VELOCITY -> DURATION -> GATE.
    void cycleRole();

    // --- generator selection per role (capability-driven) -----------------
    // How many REGISTERED generators qualify for `role` (their capability
    // set contains the role's feature bit). Bounded by the registry's
    // fixed capacity.
    uint8_t generatorCountForRole(sequencer::GeneratorRole role) const;
    // The index-th qualifying generator for `role`, or nullptr when the
    // index is out of range (callers need no separate bounds check).
    const sequencer::IGenerator* generatorForRole(sequencer::GeneratorRole role,
                                                  uint8_t index) const;
    // Select the index-th qualifying generator for `role` (records the id
    // per role; switching to the currently shown role resets the control
    // selection). False when the index is out of range.
    bool selectGeneratorForRole(sequencer::GeneratorRole role, uint8_t index);
    // Advance the selected role's generator to the next qualifying one
    // (wrapping; a role with no generator keeps its empty selection).
    void cycleGeneratorForSelectedRole();
    // The role's selected generator id, or "" when none is selected.
    const char* selectedGeneratorId(sequencer::GeneratorRole role) const;

    // --- controls (built from the selected generator's ParameterDefs) -----
    // How many controls the selected role's generator shows (0 when no
    // generator is selected). Bounded by kMaxGeneratorParams.
    uint8_t controlCount() const;
    // Copy the index-th control's metadata into `out`. False when the
    // index is out of range.
    bool controlAt(uint8_t index, Control& out) const;
    // Select which control UP/DOWN adjust (clamped into range; out-of-range
    // requests are refused and leave the selection unchanged).
    bool selectControl(uint8_t index);
    uint8_t selectedControl() const;

    // The control's current value for one pattern slot, read from the
    // view's LOCAL UI CACHE (Core0's own mirror of the edited values,
    // seeded with the factory defaults): the cache entry when the control
    // has a parameter-table address, otherwise the ParameterDef's factory
    // default (an unaddressed control displays but cannot be edited).
    int32_t controlValue(uint8_t patternIndex, uint8_t index) const;
    // Set the control's value for one pattern slot: clamps `value` into
    // the ParameterDef min/max, updates the local UI cache and posts a
    // GEN_PARAM_SET command for the sequencer core. False when the control
    // has no parameter-table address (nothing is cached or sent).
    bool setControlValue(uint8_t patternIndex, uint8_t index, int32_t value);
    // Relative edit of the control's current cached value (clamped into the
    // ParameterDef min/max, cached and sent as a GEN_PARAM_SET command).
    bool adjustControl(uint8_t patternIndex, uint8_t index, int delta);

    // --- IView -------------------------------------------------------------
    void onEnter() override;
    state::UIState handleEvent(const state::UIState& state,
                               const events::Event& event) override;
    void render(const state::UIState& state) override;

private:
    // The generator currently shown (the selected role's selection, looked
    // up by id through the registry), or nullptr when none is selected or
    // the id is no longer registered.
    const sequencer::IGenerator* selectedGenerator() const;
    // The index-th ParameterDef of the selected generator, or nullptr.
    const sequencer::GeneratorParamDef* selectedDef(uint8_t index) const;
    // Resolve one generator parameter to its ParameterBus address (scope
    // and paramId; the caller fills the pattern index). False when the
    // parameter has no entry in the fixed parameter table (see the mapping
    // table in GeneratorConfigView.cpp — the UI-side inverse of the
    // task-16 table, which fixes which parameters exist per role).
    bool resolveAddress(const sequencer::GeneratorParamDef& def,
                        sequencer::ParameterAddress& address) const;
    // Clamp into the ParameterDef range (the same bounds the UI enforces).
    static int16_t clampToDef(int32_t value, const sequencer::GeneratorParamDef& def);

    sequencer::GeneratorRegistry& registry_;
    hardware::LedMatrix& ledMatrix_;

    // Core0's LOCAL UI cache of the edited values (ui-integration T5):
    // one entry per (pattern slot, task-16 ParamId), seeded with the
    // parameter table's factory defaults and updated by every write. This
    // is the view's OWN state — the sequencer core keeps its separate
    // working state and the two communicate only through the command
    // queue. Fixed capacity: 16 pattern slots x the parameter-table count.
    int16_t cache_[common::PATTERN_CAPACITY][sequencer::kParamIdCount] = {};

    // The role currently shown.
    sequencer::GeneratorRole role_ = sequencer::GeneratorRole::GATE;
    // The selected generator id per role ("" = none selected yet). Embedded
    // fixed storage, mirroring PatternGenerativeState's id slots.
    char selection_[sequencer::kGeneratorRoleCount][sequencer::kGeneratorIdCapacity] = {};
    // The control UP/DOWN adjust (index into the selected generator's
    // ParameterDefs; meaningless when controlCount() == 0).
    uint8_t selectedControl_ = 0;
};

}  // namespace ui
