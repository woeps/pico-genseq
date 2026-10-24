#pragma once

#include <cstdint>

#include "IView.h"
#include "../hardware/LedMatrix.h"
#include "../../sequencer/CommandInbox.h"
#include "../../sequencer/IMutationOperator.h"
#include "../../sequencer/MutationConstraints.h"
#include "../../sequencer/OperationTimingConfig.h"
#include "../../sequencer/mutations/MutationOperatorRegistry.h"

namespace ui {

// Mutation trigger view (generative-engine-expansion task 20, FR5-FR10,
// FR15).
//
// The operator's front end to the generative mutation layer: it assembles
// ONE mutation request from the registered operators (FR5), the FR7 target
// mask (restricted to the selected operator's supportedTargets — FR8
// operator control), the FR6 intensity (0-127) and an optional FR10
// level-1 per-invocation timing override; plus the three recovery
// gestures Undo / Revert / Keep (FR9, FR15).
//
// Every action is POSTED as a command to the bounded CommandInbox (task
// 14) — the view never mutates the working state itself. The commands ride
// the same fixed-capacity ring as every other cross-core operation as
// fixed-size POD passengers (makeMutateCommand / makeUndoCommand / ...),
// so posting is nonblocking and allocation-free. A FULL ring is surfaced
// as a rejected action (the returned SequenceId is kInvalidSequence and
// lastActionRejected() latches for the render path) — never a block, never
// a retry loop.
//
// The timing override is ABSENT by default: with no override supplied, the
// sequencer's three-level policy applies the operation's configured mode
// (level 2) or the global default (level 3) — the posted command's
// gen.timing.active stays false. setTimingOverride() arms level 1;
// OperationTimingMode::Inherit is refused (an override that delegates is
// not an override).
//
// Selection state is view-local by design (the request is assembled here
// and handed over as a command; UIState carries no mutation fields), so
// handleEvent returns the UIState unchanged. The mutated pattern comes
// from the UI state's selected pattern (or the explicit kAllPatterns
// sentinel through triggerAllPatterns()).
//
// Key mapping (documented, follows the sibling view conventions):
//   LEFT / RIGHT   - cycle the edited field (Operator / Target / Intensity
//                    / Timing, wrapping)
//   UP / DOWN      - adjust the edited field (SHIFT: coarse step)
//                      Operator: previous / next registered operator (wrap)
//                      Target:   walk the FR7 target cursor; ENTER toggles
//                      Intensity: +/- 1 (SHIFT: +/- coarse step)
//                      Timing:   cycle absent -> Immediate -> Beat -> Bar
//   ENTER          - on the Target field: toggle the cursor's target
//                    (refused for unsupported targets); otherwise: trigger
//                    the mutation on the selected pattern
//   U / R / K      - post Undo / Revert / Keep
//
// Global constraints honored here: no heap allocation, no unbounded loops
// (every operation is a bounded sweep over the fixed registry / the seven
// FR7 targets), fixed-capacity storage (scalar members only), never any
// direct working-state mutation (commands only), never a block (tryPush).
// The view never touches the tick path.
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
        Operator = 0,  // which registered mutation operator runs
        Target = 1,    // which FR7 targets the request may touch
        Intensity = 2, // how drastic the change may be (FR6)
        Timing = 3,    // the optional FR10 level-1 override
    };
    static constexpr uint8_t kFieldCount = 4;

    MutationView(sequencer::MutationOperatorRegistry& registry,
                 sequencer::CommandInbox& inbox,
                 hardware::LedMatrix& ledMatrix);

    // --- operator selection (FR5) -------------------------------------------
    // How many REGISTERED operators the view offers (the registry's count,
    // bounded by its fixed capacity).
    uint8_t operatorCount() const;
    // The index-th registered operator, or nullptr at / past the count.
    const sequencer::IMutationOperator* operatorAt(uint8_t index) const;
    // Select the index-th operator; a fresh operator starts with ALL of its
    // supported targets selected. False (no change) when out of range.
    bool selectOperator(uint8_t index);
    // Relative operator selection with wrap (a no-op on an empty registry).
    void cycleOperator(int delta);
    // The selected operator's id / display name ("" when none is selected).
    const char* selectedOperatorId() const;
    const char* selectedOperatorName() const;
    // The selected operator's registry index (0 when none is registered).
    uint8_t selectedOperatorIndex() const;

    // --- target selection (FR7, restricted by FR8 operator control) ---------
    // The MutationTargetMask the SELECTED operator supports (0 with no
    // operator selected).
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
    sequencer::MutationTarget targetAt(uint8_t index) const;
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

    // --- actions: posts to the CommandInbox (FR5/FR9/FR15) -------------------
    // Each returns the posted command's SequenceId, or kInvalidSequence
    // when the action was rejected WITHOUT blocking (a full ring, or no
    // selected operator). lastActionRejected() latches the outcome for the
    // render path (cleared by the next successful post).
    // Trigger the mutation of `patternIndex` (or kAllPatterns).
    sequencer::SequenceId trigger(uint8_t patternIndex);
    sequencer::SequenceId triggerAllPatterns();
    // The three recovery gestures (FR9 / FR15).
    sequencer::SequenceId requestUndo();
    sequencer::SequenceId requestRevert();
    sequencer::SequenceId requestKeep();
    // True when the most recent action was rejected (a full inbox / no
    // operator); the render path surfaces it.
    bool lastActionRejected() const;

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
    // The selected operator, or nullptr when the registry is empty / the
    // stored index is no longer registered (a cleared registry).
    const sequencer::IMutationOperator* selectedOperator() const;
    // A newly selected operator starts with all of its supported targets.
    void refreshOperatorDefaults();
    // The GenerativeTimingOverride carried by the next posted command
    // (inactive when the override is absent).
    sequencer::GenerativeTimingOverride timingOverrideForPost() const;
    // Push one command, latching the rejected-action flag. Never blocks.
    sequencer::SequenceId post(sequencer::InboxCommand cmd);
    // Adjust the currently selected field by `step` (the key front end).
    void adjustSelectedField(int step);

    sequencer::MutationOperatorRegistry& registry_;
    sequencer::CommandInbox& inbox_;
    hardware::LedMatrix& ledMatrix_;

    // View-local request state (all fixed-size scalars; see the class comment).
    Field field_ = Field::Operator;
    uint8_t operatorIndex_ = 0;
    sequencer::MutationTargetMask targetMask_ = 0;
    uint8_t targetCursor_ = 0;  // the FR7 target ENTER toggles
    uint8_t intensity_ = static_cast<uint8_t>(kDefaultIntensity);
    bool timingActive_ = false;
    sequencer::OperationTimingMode timingMode_ = sequencer::OperationTimingMode::Immediate;
    bool lastActionRejected_ = false;
};

}  // namespace ui
