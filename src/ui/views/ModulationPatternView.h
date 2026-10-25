#pragma once

#include <cstdint>

#include "IView.h"
#include "../hardware/LedMatrix.h"
#include "../../sequencer/ModulationRegistry.h"
#include "../../sequencer/ModulationTarget.h"
#include "../../sequencer/ParameterAddress.h"
#include "../../sequencer/ParameterBus.h"

namespace ui {

// Modulation pattern editor + target configuration view
// (generative-engine-expansion task 21, FR11-FR14).
//
// The operator's front end to the modulation layer (tasks 15-17): one view,
// two pages of fields.
//
//   Pattern editing (FR11): which of the registry's fixed pool of GLOBAL
//   ModulationPatterns is edited, its variable length (1-16, clamped) and
//   the step values (0-127, clamped). Only steps within the current length
//   are editable or highlighted; shortening the length NEVER clears the
//   stored steps, so re-lengthening restores them exactly.
//
//   Target configuration (FR12-FR14): the selected ModulationTarget slot's
//   parameter (picked from the FULL ParameterBus table, including the
//   global BPM), scope (FR13: per-pattern by default, global the option),
//   which global pattern drives the target, and the FR14 scaling / balance
//   / step duration / playing order.
//
// Every edit writes straight into the ModulationRegistry's fixed storage
// (the configuration the tick-path ModulationApplier reads). The view
// NEVER writes a ParameterBus value itself — no base value, no overlay:
// overlays are written exclusively by the per-tick applier, so the
// Milestone 2 / AC3 data-integrity guarantees are untouched by the UI.
//
// Scope binding rule (keeps an explicitly configured target VALID): a
// ParameterAddress must carry the scope its parameter is defined under
// (BPM is global-only, everything else per-pattern). Selecting a parameter
// writes the table's scope alongside it; switching the scope re-points a
// scope-mismatched parameter (global keeps only the BPM, per-pattern falls
// back to the first per-pattern parameter) instead of leaving a dead
// address. A freshly claimed target keeps its deliberately INVALID factory
// address until the performer binds one — default behavior stays stable.
//
// Selection state is view-local by design (UIState carries no modulation
// fields), so handleEvent returns the UIState unchanged; it only mirrors
// state.selectedPattern into the per-pattern address slot so a target
// bound while pattern 7 is selected addresses pattern 7.
//
// Key mapping (documented, follows the sibling view conventions):
//   LEFT / RIGHT   - cycle the edited field (wrapping). On the Step field
//                    they move the step cursor WITHIN the active length and
//                    leave the field at the cursor's edge (0 / length-1).
//   UP / DOWN      - adjust the edited field (SHIFT: coarse step 10):
//                      Pattern: previous / next global pattern (wrap)
//                      Length:  +/- step, clamped 1-16
//                      Step:    the cursor step's value, clamped 0-127
//                      Target:  previous / next ACTIVE slot (wrap)
//                      Bind:    previous / next driving pattern (wrap)
//                      Param:   previous / next table entry (wrap)
//                      Scope:   toggle per-pattern <-> global
//                      Scale:   +/- step, clamped 0-127
//                      Balance: +/- step, clamped -127..127
//                      Duration: +/- step length, clamped 1-16
//                      Order:   forward/reverse/ping-pong/random (wrap)
//   ENTER          - on the Target field: claim a new target slot (a full
//                    registry is surfaced as a rejected action, never a
//                    block); on the Duration field: toggle beat/bar;
//                    otherwise a no-op.
//
// Global constraints honored here: no heap allocation, no unbounded loops
// (every operation is a bounded sweep over the fixed pattern/target/
// parameter tables), fixed-capacity storage (scalar members only), never a
// block. The view never touches the tick path.
class ModulationPatternView : public IView {
public:
    // FR11 step bounds: values 0-127, length 1-16 (the pattern's fixed
    // capacity).
    static constexpr int kMaxStepValue = 127;
    static constexpr int kMinLength = 1;
    static constexpr int kMaxLength = 16;
    // FR14 balance bounds (int8_t storage on the target).
    static constexpr int kMaxBalance = 127;
    static constexpr int kMinBalance = -127;
    // The documented UI bound for a step duration's unit count (the target
    // stores uint16_t; the UI keeps it a sane small count).
    static constexpr int kMaxDurationLength = 16;
    // The coarse (SHIFT) adjustment step, matching the sibling views.
    static constexpr int kCoarseStep = 10;
    // Sentinel: no target slot selected.
    static constexpr uint8_t kNoTarget = 0xFF;

    // The fields LEFT/RIGHT cycle (see the class comment).
    enum class Field : uint8_t {
        Pattern = 0,     // which global modulation pattern the editor edits
        Length = 1,      // the edited pattern's active length (1-16)
        Step = 2,        // the step cursor + its value (0-127)
        Target = 3,      // the target slot selection (ENTER claims)
        PatternBind = 4, // which global pattern drives the selected target
        Param = 5,       // the target parameter picker (full bus table)
        Scope = 6,       // FR13: per-pattern (default) / global
        Scale = 7,       // FR14 scaling factor (0-127)
        Balance = 8,     // FR14 balance value
        Duration = 9,    // FR14 step duration (ENTER toggles beat/bar)
        Order = 10,      // FR11 playing order
    };
    static constexpr uint8_t kFieldCount = 11;

    // One pickable parameter, mirroring the task-16 table entry the
    // ParameterBus resolves for that ParamId.
    struct ParamOption {
        const char* name = "";
        int16_t minValue = 0;
        int16_t maxValue = 0;
        sequencer::ParameterScope scope = sequencer::ParameterScope::Pattern;
    };

    ModulationPatternView(sequencer::ModulationRegistry& registry,
                          sequencer::ParameterBus& bus,
                          hardware::LedMatrix& ledMatrix);

    // --- pattern editing (FR11) ---------------------------------------------
    // Which global ModulationPattern the editor edits (0..MAX_PATTERNS-1).
    bool selectEditedPattern(uint8_t index);  // refused out of the pool
    void cycleEditedPattern(int delta);       // wraps
    uint8_t editedPattern() const;

    // The edited pattern's active length, clamped into 1-16 (0 becomes 1,
    // 17 becomes 16) and written through to the pattern.
    void setPatternLength(int length);
    void adjustPatternLength(int delta);
    uint8_t patternLength() const;

    // Only steps within the current length are editable or highlighted.
    bool stepEditable(uint8_t index) const;
    bool stepHighlighted(uint8_t index) const;
    // The step cursor: selectable / movable only within the length. The
    // cursor is clamped back into the length whenever the pattern or its
    // length changes, so it can never name an out-of-length step.
    bool selectStep(uint8_t index);
    bool cycleStep(int delta);  // clamped; false when already at the edge
    uint8_t selectedStep() const;
    // A total read (wraps modulo the length, like ModulationPattern).
    uint8_t stepValue(uint8_t index) const;
    // Write one step: refused when the step is not editable (index past the
    // length); the value is clamped to 0-127.
    bool setStepValue(uint8_t index, int value);
    // Relative edit of the cursor step's value (clamped 0-127).
    void adjustStepValue(int delta);

    // --- target slots (FR13) --------------------------------------------------
    // The fixed slot pool (the registry's MAX_TARGETS).
    uint8_t targetSlotCount() const;
    uint8_t activeTargetCount() const;
    // Claim a free slot (registry.addTarget factory configuration), select
    // it and return its index; kNoTarget + lastActionRejected when the
    // registry is full — never a block, never a growth.
    uint8_t addTarget();
    // Release the selected slot (refused when none is selected).
    bool removeSelectedTarget();
    // Select an ACTIVE slot (a released / never-claimed one is refused).
    bool selectTargetSlot(uint8_t slot);
    // Move to the previous / next ACTIVE slot (wrapping; a no-op when no
    // slot is active).
    void cycleTargetSlot(int delta);
    uint8_t selectedTargetSlot() const;  // kNoTarget when none
    bool hasSelectedTarget() const;
    // True when the most recent action was rejected (a full registry); the
    // render path surfaces it.
    bool lastActionRejected() const;

    // --- the target's driving pattern -----------------------------------------
    // Which global ModulationPattern drives the selected target (written to
    // the target's patternIndex).
    bool setTargetPattern(uint8_t patternIndex);  // refused out of the pool
    void cycleTargetPattern(int delta);           // wraps
    uint8_t targetPattern() const;

    // --- the parameter picker (FR12) ------------------------------------------
    // The picker lists EVERY ParameterBus parameter (the full task-16
    // table, including the global BPM), metadata verbatim.
    uint8_t paramCount() const;
    bool paramAt(uint8_t index, ParamOption& out) const;
    // The picker cursor (an index into the table).
    uint8_t selectedParam() const;
    // Bind the index-th table parameter to the selected target: writes the
    // address (paramId + the table's scope + the pattern slot for
    // per-pattern scope). Refused out of range / without a selected target.
    bool selectParam(uint8_t index);
    void cycleParam(int delta);  // wraps over the whole table

    // --- scope (FR13) -----------------------------------------------------------
    // The selected target's address scope (the FR13 Pattern default when
    // nothing is selected). Switching re-points a scope-mismatched
    // parameter (see the class comment) so the address stays valid.
    sequencer::ParameterScope targetScope() const;
    bool setTargetScope(sequencer::ParameterScope scope);
    void toggleTargetScope();
    // The sequencer pattern slot written into per-pattern addresses
    // (mirrors the UI's selected pattern; 0 by default).
    bool selectAddressPatternSlot(uint8_t slot);  // refused past the capacity
    uint8_t addressPatternSlot() const;

    // --- FR14 per-target config -------------------------------------------------
    uint8_t targetScaling() const;
    bool setTargetScaling(int value);  // clamped 0-127
    void adjustTargetScaling(int delta);
    int8_t targetBalance() const;
    bool setTargetBalance(int value);  // clamped -127..127
    void adjustTargetBalance(int delta);
    sequencer::StepDuration targetStepDuration() const;
    // Refused for a foreign unit / without a selected target; the length is
    // clamped into 1..kMaxDurationLength.
    bool setStepDuration(sequencer::StepUnit unit, int length);
    bool setStepDurationLength(int length);  // clamped, keeps the unit
    bool setStepDurationUnit(sequencer::StepUnit unit);
    void toggleStepDurationUnit();
    sequencer::ModulationOrder targetOrder() const;
    // Refused for a foreign ordinal / without a selected target.
    bool setTargetOrder(sequencer::ModulationOrder order);
    void cycleTargetOrder(int delta);  // wraps over the four orders

    // --- field selection ---------------------------------------------------------
    bool selectField(Field field);  // a foreign enum byte is refused
    Field selectedField() const;
    void cycleField(int delta);     // wraps

    // --- IView -------------------------------------------------------------------
    void onEnter() override;
    state::UIState handleEvent(const state::UIState& state,
                               const events::Event& event) override;
    void render(const state::UIState& state) override;

private:
    // The selected target (bounds-checked, so nullptr never exists): reads
    // are total through the registry's null-target safe default; WRITERS
    // must check hasSelectedTarget() first so an edit can never land in the
    // shared null target.
    sequencer::ModulationTarget& selectedTarget();
    const sequencer::ModulationTarget& selectedTarget() const;
    // The ModulationPattern the editor edits (bounds-checked safe default).
    sequencer::ModulationPattern& editedPatternRef();
    const sequencer::ModulationPattern& editedPatternRef() const;
    // Clamp the step cursor back into the edited pattern's length (called
    // whenever the pattern or its length changes).
    void clampStepCursor();
    // Clamp into [lo, hi] (lo <= hi).
    static int clampInt(int value, int lo, int hi);
    // Adjust the currently selected field by `step` (the key front end).
    void adjustSelectedField(int step);

    sequencer::ModulationRegistry& registry_;
    sequencer::ParameterBus& bus_;
    hardware::LedMatrix& ledMatrix_;

    // View-local selection state (fixed-size scalars only).
    Field field_ = Field::Pattern;
    uint8_t editedPattern_ = 0;       // which global pattern is edited
    uint8_t stepCursor_ = 0;          // stays within the edited length
    uint8_t selectedTargetSlot_ = kNoTarget;
    uint8_t paramCursor_ = 0;         // picker cursor (table index)
    uint8_t addressPatternSlot_ = 0;  // mirrors the UI's selected pattern
    bool lastActionRejected_ = false;
};

}  // namespace ui
