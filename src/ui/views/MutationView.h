#pragma once

#include <cstdint>

#include "IView.h"
#include "../hardware/LedMatrix.h"
#include "../../sequencer/MutationConstraints.h"
#include "../../sequencer/OperationTimingConfig.h"

namespace ui {

// Mutation trigger view (generative-engine-expansion task 20, FR5-FR10,
// FR15; refactored onto the command queue by ui-integration T6).
//
// The operator's front end to the generative mutation layer: it assembles
// ONE mutation request from the built-in mutation operators (FR5), the FR7
// target mask (restricted to the selected operator's supportedTargets —
// FR8 operator control), the FR6 intensity (0-127) and an optional FR10
// level-1 per-invocation timing override; plus the three recovery gestures
// Undo / Revert / Keep (FR9, FR15).
//
// Core discipline (ui-integration global constraint): the view holds NO
// reference to the sequencer core's MutationOperatorRegistry and NO
// CommandInbox — no operator object, no registry and no other data
// structure crosses the cores. The view carries its OWN fixed metadata
// copy of the built-in operators (id / display name / supported-target
// mask, in the SAME registration order registerBuiltInMutations() uses —
// test_mutation_view pins the mirror against the real registry), and
// every action is POSTED over the inter-core command queue as one of the
// four generative commands (MUTATE / UNDO / REVERT / KEEP): MUTATE names
// the operator by its INDEX into that shared order, the pattern by its
// slot index (or the all-patterns sentinel), and carries the FR7 target
// mask, the FR6 intensity and the packed FR10 override; the recovery
// commands carry the override alone. The sequencer core resolves the
// index through its OWN registry, so both sides only ever agree on the
// ORDER — the only thing the FIFO words encode.
//
// The timing override is ABSENT by default: with no override supplied,
// the sequencer's three-level policy applies the operation's configured
// mode (level 2) or the global default (level 3) — the posted command's
// gen.timing.active stays false. setTimingOverride() arms level 1;
// OperationTimingMode::Inherit is refused (an override that delegates is
// not an override).
//
// Selection state is view-local by design (the request is assembled here
// and handed over as a command; UIState carries no mutation fields). The
// mutated pattern comes from the UI state's selected pattern (or the
// explicit all-patterns sentinel through triggerAllPatterns()).
//
// Key mapping (documented, follows the sibling view conventions):
//   LEFT / RIGHT   - cycle the edited field (Operator / Target / Intensity
//                    / Timing, wrapping)
//   UP / DOWN      - adjust the edited field (SHIFT: coarse step)
//                      Operator: previous / next built-in operator (wrap)
//                      Target:   walk the FR7 target cursor; ENTER toggles
//                      Intensity: +/- 1 (SHIFT: +/- coarse step)
//                      Timing:   cycle absent -> Immediate -> Beat -> Bar
//   ENTER          - on the Target field: toggle the cursor's target
//                    (refused for unsupported targets); otherwise: trigger
//                    the mutation on the selected pattern
//   U / R / K      - post Undo / Revert / Keep
//   ESCAPE         - return to the PATTERNS parent view (pressed once)
//
// Global constraints honored here: no heap allocation, no unbounded loops
// (every operation is a bounded sweep over the fixed metadata table / the
// seven FR7 targets), fixed-capacity storage (scalar members only), never
// any direct working-state mutation (commands only). The view never
// touches the tick path.
class MutationView : public IView {
public:
    // The FR6 intensity range: 0 (deliberate no-change) .. 127 (drastic).
    static constexpr int kMaxIntensity = 127;
    // The documented slider default: a moderate change out of the box.
    static constexpr int kDefaultIntensity = 64;
    // The coarse (SHIFT) adjustment step, matching the velocity sibling.
    static constexpr int kCoarseStep = 10;

    // The fields UP/DOWN adjust.
    enum class Field : uint8_t {
        Operator = 0,  // which built-in mutation operator runs
        Target = 1,    // which FR7 targets the request may touch
        Intensity = 2, // how drastic the change may be (FR6)
        Timing = 3,    // the optional FR10 level-1 override
    };
    static constexpr uint8_t kFieldCount = 4;

    explicit MutationView(hardware::LedMatrix& ledMatrix);

    // --- operator selection (FR5) -------------------------------------------
    // The view's OWN fixed metadata copy of the built-in operators — the
    // SAME registration order the sequencer core's registry lists (both
    // sides only agree on this order; nothing crosses the cores).
    static uint8_t operatorCount();
    // The index-th operator's stable id / display name / supported-target
    // mask ("" / 0 / 0 past the count — total accessors).
    static const char* operatorIdAt(uint8_t index);
    static const char* operatorNameAt(uint8_t index);
    static sequencer::MutationTargetMask operatorTargetsAt(uint8_t index);
    // Select the index-th operator; a fresh operator starts with ALL of its
    // supported targets selected. False (no change) when out of range.
    bool selectOperator(uint8_t index);
    // Relative operator selection with wrap.
    void cycleOperator(int delta);
    // The selected operator's id / display name.
    const char* selectedOperatorId() const;
    const char* selectedOperatorName() const;
    // The selected operator's index into the shared registration order.
    uint8_t selectedOperatorIndex() const;

    // --- target selection (FR7, restricted by FR8 operator control) ---------
    // The MutationTargetMask the SELECTED operator supports.
    sequencer::MutationTargetMask supportedTargets() const;
    // The currently selected request target mask (always a subset of
    // supportedTargets()).
    sequencer::MutationTargetMask targetMask() const;
    bool hasTarget(sequencer::MutationTarget target) const;
    // Toggle one FR7 target in the request. REFUSED (false, no change)
    // when the selected operator does not support the target — the mask can
    // only ever include supportedTargets().
    bool toggleTarget(sequencer::MutationTarget target);
    // Select every target the operator supports / none at all.
    void selectAllSupportedTargets();
    void clearTargets();

    // --- the FR7 target cursor (which target ENTER toggles) -----------------
    // The seven FR7 targets in a fixed documented order, by index 0-6
    // (MutationTarget::NONE past the end — a total accessor).
    static sequencer::MutationTarget targetAt(uint8_t index);
    bool selectTargetIndex(uint8_t index);
    uint8_t selectedTargetIndex() const;

    // --- intensity (FR6) ------------------------------------------------------
    uint8_t intensity() const;
    // Set the intensity, CLAMPED to 0-127.
    void setIntensity(int value);
    // Relative adjustment, clamped to 0-127 at both ends.
    void adjustIntensity(int delta);

    // --- the FR10 level-1 per-invocation timing override ---------------------
    // ABSENT by default: the operation's configured mode (level 2) and the
    // global default (level 3) decide.
    bool timingOverrideActive() const;
    sequencer::OperationTimingMode timingOverride() const;
    // Arm the override. Inherit is refused (it delegates, it does not
    // override); false is returned and nothing changes.
    bool setTimingOverride(sequencer::OperationTimingMode mode);
    // Back to the absent default.
    void clearTimingOverride();
    // Cycle absent -> Immediate -> QuantizeBeat -> QuantizeBar -> absent
    // (the sign of `delta` picks the direction).
    void cycleTimingOverride(int delta);

    // --- actions: posts over the command queue (FR5/FR9/FR15) ---------------
    // Each action posts its generative command to the sequencer core via
    // the inter-core command queue — the ONLY cross-core path (a full
    // queue blocks the UI core exactly like every other UI command; the
    // bounded pending list on the sequencer side refuses overflow).
    // Trigger the mutation of `patternIndex` (or the all-patterns
    // sentinel, commands::ALL_PATTERNS).
    void trigger(uint8_t patternIndex);
    void triggerAllPatterns();
    // The three recovery gestures (FR9 / FR15).
    void requestUndo();
    void requestRevert();
    void requestKeep();

    // --- field selection -------------------------------------------------------
    bool selectField(Field field);
    Field selectedField() const;
    void cycleField(int delta);

    // --- IView -------------------------------------------------------------------
    void onEnter() override;
    state::UIState handleEvent(const state::UIState& state,
                               const events::Event& event) override;
    void render(const state::UIState& state) override;

private:
    // A newly selected operator starts with all of its supported targets.
    void refreshOperatorDefaults();
    // The packed FR10 level-1 override carried by the next posted command
    // (commands::packGenerativeTiming; 0 when the override is absent).
    uint8_t packedTimingForPost() const;
    // Adjust the currently selected field by `step` (the key front end).
    void adjustSelectedField(int step);

    hardware::LedMatrix& ledMatrix_;

    // View-local request state (all fixed-size scalars; see the class comment).
    Field field_ = Field::Operator;
    uint8_t operatorIndex_ = 0;
    sequencer::MutationTargetMask targetMask_ = 0;
    uint8_t targetCursor_ = 0;  // the FR7 target ENTER toggles
    uint8_t intensity_ = static_cast<uint8_t>(kDefaultIntensity);
    bool timingActive_ = false;
    sequencer::OperationTimingMode timingMode_ = sequencer::OperationTimingMode::Immediate;
};

}  // namespace ui
