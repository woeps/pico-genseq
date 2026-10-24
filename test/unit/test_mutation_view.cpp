// generative-engine-expansion task 20 — Mutation UI View (FR5-FR10, FR15).
//
// Validates the mutation trigger view against the REAL mutation plugin
// contract and the bounded CommandInbox:
//   - the operator list comes from MutationOperatorRegistry (the view
//     enumerates exactly the registered operators, id/name verbatim);
//   - the target mask can only include the selected operator's
//     supportedTargets (FR8 operator control): unsupported toggles are
//     refused, and switching operators re-masks the selection;
//   - the intensity is clamped to 0-127 (FR6);
//   - Trigger posts a MutateCmd with the correct payload (operator id,
//     pattern index / all patterns, target mask, intensity, default
//     invariants / distance);
//   - Undo / Revert / Keep post the matching commands (FR9, FR15);
//   - the timing override is ABSENT by default (the operation's configured
//     mode / the global default applies) and, when set, is carried in the
//     command (FR10 level 1);
//   - a full inbox is surfaced as a rejected action without blocking (the
//     push returns immediately, enqueues nothing and the view reports the
//     rejection so render can indicate it).
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_mutation_view".
//
// The view's render path targets the real LedMatrix driver (Pico SDK PIO),
// so — like test_generator_config_view / test_scale_selection_view — this TU
// supplies host-safe no-op definitions of the LedMatrix member functions
// instead of compiling the SDK-dependent LedMatrix.cpp.

#include "sequencer/CommandInbox.h"
#include "sequencer/IMutationOperator.h"
#include "sequencer/MutationConstraints.h"
#include "sequencer/OperationTimingConfig.h"
#include "sequencer/mutations/MutationOperatorRegistry.h"
#include "ui/Event.h"
#include "ui/hardware/LedMatrix.h"
#include "ui/state/UIState.h"
#include "ui/views/MutationView.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using sequencer::CommandInbox;
using sequencer::GenerativeCommandKind;
using sequencer::InboxCommand;
using sequencer::MutationOperatorRegistry;
using sequencer::MutationTarget;
using sequencer::MutationTargetMask;
using sequencer::OperationTimingMode;
using sequencer::SequenceId;
using ui::events::Event;
using ui::KeyId;
using ui::mod::NONE;
using ui::mod::SHIFT;
using ui::MutationView;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

}  // namespace

// --- host-safe LedMatrix seam (see the file comment) --------------------------
// Defined in the REAL hardware namespace (outside the anonymous one) so they
// replace the SDK-dependent LedMatrix.cpp definitions at link time.

namespace hardware {
LedMatrix::LedMatrix(uint8_t pin) : pin(pin), buffer{}, dirty(false) {}
void LedMatrix::update() {}
void LedMatrix::clear() { dirty = true; }
void LedMatrix::setPixel(uint8_t, uint8_t, uint32_t) { dirty = true; }
void LedMatrix::fill(uint32_t) { dirty = true; }
void LedMatrix::drawNumber(int, uint32_t) { dirty = true; }
void LedMatrix::drawLabel(const char (&)[4], uint32_t) { dirty = true; }
void LedMatrix::drawNote(const char (&)[4], uint32_t) { dirty = true; }
}  // namespace hardware

namespace {

// --- shared fixture -----------------------------------------------------------

// A LOCAL registry populated with the REAL built-in operator bank (the same
// registerBuiltInMutations the firmware startup calls), so the view is tested
// against the actual plugin contract without touching the process-wide
// g_mutationRegistry.
MutationOperatorRegistry registry;
CommandInbox inbox;
hardware::LedMatrix matrix{16};

// Drain the shared inbox so every section starts empty.
void drainInbox() {
    InboxCommand drained{};
    while (inbox.tryPop(drained)) {}
}

// A fresh view over the shared fixture.
MutationView makeView() {
    registerBuiltInMutations(registry);
    drainInbox();
    return MutationView(registry, inbox, matrix);
}

// Pop the oldest pushed command (asserting one exists).
InboxCommand pop() {
    InboxCommand cmd{};
    check(inbox.tryPop(cmd), "a pushed command can be popped");
    return cmd;
}

// --- 1. The operator list comes from MutationOperatorRegistry ----------------

void test_operator_list_from_registry() {
    MutationView view = makeView();

    check(view.operatorCount() == registry.count(),
          "the view offers exactly the registry's operator count");
    check(view.operatorCount() == 10,
          "the built-in bank registers the ten T9/T10 operators");

    // Every offered operator IS the registered one (pointer identity), with
    // its id and name exposed verbatim; past the count the accessor is null.
    for (uint8_t i = 0; i < view.operatorCount(); ++i) {
        check(view.operatorAt(i) == registry.at(i),
              "the view lists the registered operators themselves");
        check(view.operatorAt(i) != nullptr &&
                  view.operatorAt(i)->id() != nullptr &&
                  view.operatorAt(i)->name() != nullptr,
              "every offered operator exposes id and name");
    }
    check(view.operatorAt(view.operatorCount()) == nullptr,
          "an over-range operator index reads as null");

    // A fresh view shows the first registered operator.
    check(std::strcmp(view.selectedOperatorId(), registry.at(0)->id()) == 0,
          "a fresh view selects the first registered operator");
    check(std::strcmp(view.selectedOperatorId(), "builtin.mutate-density") == 0,
          "the first built-in operator is builtin.mutate-density");
    check(std::strcmp(view.selectedOperatorName(), registry.at(0)->name()) == 0,
          "the selected operator's display name is exposed verbatim");

    // Direct selection by index; over-range selections are refused (never
    // clamped).
    check(view.selectOperator(5), "selecting operator index 5 succeeds");
    check(std::strcmp(view.selectedOperatorId(), "builtin.rotate") == 0,
          "operator index 5 is builtin.rotate (registration order)");
    check(!view.selectOperator(view.operatorCount()),
          "selecting past the last operator is refused");
    check(std::strcmp(view.selectedOperatorId(), "builtin.rotate") == 0,
          "a refused operator selection changes nothing");

    // Cycling wraps in both directions across the whole list.
    check(view.selectOperator(0), "re-selecting operator 0 succeeds");
    view.cycleOperator(-1);
    check(view.selectedOperatorIndex() == view.operatorCount() - 1,
          "cycling down from the first operator wraps to the last");
    view.cycleOperator(+1);
    check(view.selectedOperatorIndex() == 0,
          "cycling up from the last operator wraps to the first");
    view.cycleOperator(+3);
    check(view.selectedOperatorIndex() == 3,
          "a coarse upward cycle advances by the step");
}

// --- 2. The target mask only includes the selected operator's targets --------

void test_target_mask_restricted_to_operator() {
    MutationView view = makeView();

    // Density supports ONLY gate density: its bit toggles, foreign bits are
    // refused outright and change nothing (FR8 operator control).
    check(view.supportedTargets() ==
              sequencer::maskOf(MutationTarget::GATE_DENSITY),
          "the selected operator's supported targets are exposed");
    check(view.targetMask() == view.supportedTargets(),
          "a fresh operator starts with all of its supported targets");
    check(view.toggleTarget(MutationTarget::GATE_DENSITY),
          "toggling a supported target succeeds");
    check(view.targetMask() == 0, "toggling the only target clears it");
    check(!view.hasTarget(MutationTarget::GATE_DENSITY),
          "hasTarget reports the cleared bit");
    check(!view.toggleTarget(MutationTarget::PITCH_ORDERING),
          "toggling a target the operator does not support is refused");
    check(!view.toggleTarget(MutationTarget::PITCH_CONTENTS),
          "toggling pitch contents under a density operator is refused");
    check(!view.toggleTarget(MutationTarget::EUCLIDEAN_PARAMS),
          "toggling euclidean params under a density operator is refused");
    check(view.targetMask() == 0,
          "the refused toggles change the mask in no way");

    // Explicit clear + select-all helpers stay inside the supported set.
    view.selectAllSupportedTargets();
    check(view.targetMask() == view.supportedTargets(),
          "select-all fills exactly the supported targets");
    view.clearTargets();
    check(view.targetMask() == 0, "clearing the target mask empties it");

    // The FR7 target cursor walks the seven fixed targets; toggles through
    // the cursor go through the same operator gate.
    check(view.targetAt(0) == MutationTarget::GATE_DENSITY &&
              view.targetAt(6) == MutationTarget::EUCLIDEAN_PARAMS,
          "the target cursor walks the seven FR7 targets in order");
    check(!view.selectTargetIndex(sequencer::kMutationTargetCount),
          "an over-range target cursor selection is refused");
    check(view.selectTargetIndex(6), "selecting target cursor 6 succeeds");
    check(view.selectedTargetIndex() == 6, "the cursor selection reads back");
    check(!view.toggleTarget(view.targetAt(6)),
          "the cursor's unsupported target cannot be toggled either");

    // Switching to Rotate re-masks the selection: a bit the new operator
    // does not support is DROPPED, never carried over.
    view.selectAllSupportedTargets();  // GATE_DENSITY only, under Density
    check(view.selectOperator(5), "switching to builtin.rotate succeeds");
    check(view.targetMask() ==
              (sequencer::maskOf(MutationTarget::PITCH_ORDERING) |
               sequencer::maskOf(MutationTarget::VELOCITY_ORDERING) |
               sequencer::maskOf(MutationTarget::EUCLIDEAN_PARAMS)),
          "switching operators re-masks to the new supported targets");
    check((view.targetMask() & ~view.supportedTargets()) == 0,
          "the target mask never exceeds the operator's supported targets");
    check(!view.toggleTarget(MutationTarget::GATE_DENSITY),
          "gate density is refused under rotate");
    check(!view.toggleTarget(MutationTarget::NOTE_DURATION),
          "note duration is refused under rotate");
    check(view.targetMask() ==
              (sequencer::maskOf(MutationTarget::PITCH_ORDERING) |
               sequencer::maskOf(MutationTarget::VELOCITY_ORDERING) |
               sequencer::maskOf(MutationTarget::EUCLIDEAN_PARAMS)),
          "the refused toggles leave the rotate mask intact");
}

// --- 3. Intensity is clamped to 0-127 -----------------------------------------

void test_intensity_clamped() {
    MutationView view = makeView();

    check(view.intensity() == 64,
          "the intensity slider starts at the documented midpoint 64");
    view.setIntensity(127);
    check(view.intensity() == 127, "intensity 127 is kept");
    view.setIntensity(128);
    check(view.intensity() == 127, "intensity 128 clamps to 127 (FR6)");
    view.setIntensity(200);
    check(view.intensity() == 127, "intensity 200 clamps to 127");
    view.setIntensity(0);
    check(view.intensity() == 0, "intensity 0 is kept");
    view.setIntensity(-1);
    check(view.intensity() == 0, "a negative intensity clamps to 0");
    view.setIntensity(64);
    check(view.intensity() == 64, "intensity 64 is kept verbatim");

    // Relative adjustment clamps at both ends too.
    view.adjustIntensity(+1000);
    check(view.intensity() == 127, "adjusting far past the top clamps to 127");
    view.adjustIntensity(-1000);
    check(view.intensity() == 0, "adjusting far past the bottom clamps to 0");
    view.setIntensity(120);
    view.adjustIntensity(+10);
    check(view.intensity() == 127, "a small overshoot still clamps to 127");
}

// --- 4. Trigger posts a MutateCmd with the correct payload --------------------

void test_trigger_posts_mutate_command() {
    MutationView view = makeView();

    check(view.selectOperator(5), "rotate is selected");
    view.clearTargets();
    check(view.toggleTarget(MutationTarget::PITCH_ORDERING) &&
              view.toggleTarget(MutationTarget::VELOCITY_ORDERING),
          "two rotate targets are selected");
    view.setIntensity(42);

    const SequenceId seq = view.trigger(3);
    check(seq != sequencer::kInvalidSequence,
          "trigger posts the mutation command");
    check(!view.lastActionRejected(),
          "a successful post is not surfaced as rejected");

    const InboxCommand cmd = pop();
    check(cmd.gen.kind == GenerativeCommandKind::Mutate,
          "the posted command is a Mutate command");
    check(std::strcmp(cmd.gen.mutate.opId, "builtin.rotate") == 0,
          "the payload carries the selected operator's id");
    check(cmd.gen.mutate.patternIndex == 3,
          "the payload carries the requested pattern index");
    check(cmd.gen.mutate.targets ==
              (sequencer::maskOf(MutationTarget::PITCH_ORDERING) |
               sequencer::maskOf(MutationTarget::VELOCITY_ORDERING)),
          "the payload carries the selected target mask");
    check(cmd.gen.mutate.intensity == 42,
          "the payload carries the FR6 intensity");
    check(cmd.gen.mutate.invariants == 0,
          "the payload carries no invariants by default");
    check(cmd.gen.mutate.maxDistance == sequencer::kUnlimitedMutationDistance,
          "the payload carries the default distance limit");
    check(cmd.gen.timing.active == false,
          "no timing override is supplied by default");
    check(cmd.sequence == seq,
          "the posted command carries the returned sequence id");

    // The all-patterns sentinel is carried verbatim (FR5's fan-out shape).
    check(view.trigger(sequencer::kAllPatterns) != sequencer::kInvalidSequence,
          "triggering every pattern posts too");
    check(pop().gen.mutate.patternIndex == sequencer::kAllPatterns,
          "the all-patterns sentinel is carried in the payload");

    // The key front end (ENTER on the operator field) triggers on the
    // SELECTED pattern of the UI state.
    ui::state::UIState state;
    state.selectedPattern = 9;
    view.handleEvent(state, Event::keyPressed(KeyId::ENTER, NONE, 0));
    const InboxCommand keyCmd = pop();
    check(keyCmd.gen.kind == GenerativeCommandKind::Mutate &&
              keyCmd.gen.mutate.patternIndex == 9 &&
              std::strcmp(keyCmd.gen.mutate.opId, "builtin.rotate") == 0,
          "ENTER triggers the mutation on the UI state's selected pattern");
}

// --- 5. Undo / Revert / Keep post the matching commands -----------------------

void test_recovery_commands() {
    MutationView view = makeView();

    check(view.requestUndo() != sequencer::kInvalidSequence,
          "requesting undo posts a command");
    check(view.requestRevert() != sequencer::kInvalidSequence,
          "requesting revert posts a command");
    check(view.requestKeep() != sequencer::kInvalidSequence,
          "requesting keep posts a command");

    // The three recovery commands pop in push order, each with its own kind
    // and no mutation payload of its own.
    check(pop().gen.kind == GenerativeCommandKind::Undo,
          "the first posted recovery command is Undo");
    check(pop().gen.kind == GenerativeCommandKind::Revert,
          "the second posted recovery command is Revert");
    check(pop().gen.kind == GenerativeCommandKind::Keep,
          "the third posted recovery command is Keep");

    // The key front end posts the same three commands.
    ui::state::UIState state;
    view.handleEvent(state, Event::keyPressed(KeyId::U, NONE, 0));
    view.handleEvent(state, Event::keyPressed(KeyId::R, NONE, 0));
    view.handleEvent(state, Event::keyPressed(KeyId::K, NONE, 0));
    check(pop().gen.kind == GenerativeCommandKind::Undo,
          "the U key posts Undo");
    check(pop().gen.kind == GenerativeCommandKind::Revert,
          "the R key posts Revert");
    check(pop().gen.kind == GenerativeCommandKind::Keep,
          "the K key posts Keep");

    // A released key event posts nothing and leaves the state untouched.
    const ui::state::UIState before;
    const ui::state::UIState after =
        view.handleEvent(before, Event::keyReleased(KeyId::U, NONE, 0));
    check(after.currentView == before.currentView &&
              after.selectedPattern == before.selectedPattern,
          "a released key event leaves the UI state untouched");
    InboxCommand drained{};
    check(!inbox.tryPop(drained), "a released key event posts nothing");
}

// --- 6. The timing override: absent by default, carried when set -------------

void test_timing_override() {
    MutationView view = makeView();

    // ABSENT by default: the operation's configured mode (level 2) and the
    // global default (level 3) decide — the posted command says so.
    check(!view.timingOverrideActive(),
          "the timing override is absent by default");
    check(view.trigger(0) != sequencer::kInvalidSequence, "a trigger posts");
    check(pop().gen.timing.active == false,
          "the default trigger carries no timing override");

    // Inherit is not an override (it delegates); setting it is refused.
    check(!view.setTimingOverride(OperationTimingMode::Inherit),
          "Inherit is refused as an override (it is not one)");
    check(!view.timingOverrideActive(),
          "the refused Inherit leaves the override absent");

    // A set override is carried in the command (FR10 level 1).
    check(view.setTimingOverride(OperationTimingMode::QuantizeBar),
          "setting a bar-quantized override succeeds");
    check(view.timingOverrideActive(), "the override is now active");
    check(view.timingOverride() == OperationTimingMode::QuantizeBar,
          "the active override reads back as QuantizeBar");
    check(view.trigger(0) != sequencer::kInvalidSequence, "a trigger posts");
    {
        const InboxCommand cmd = pop();
        check(cmd.gen.timing.active == true,
              "the override is carried as active");
        check(cmd.gen.timing.mode ==
                  static_cast<uint8_t>(OperationTimingMode::QuantizeBar),
              "the override carries the QuantizeBar mode ordinal");
    }

    // The recovery commands carry the same per-invocation override.
    check(view.setTimingOverride(OperationTimingMode::QuantizeBeat),
          "setting a beat-quantized override succeeds");
    check(view.requestUndo() != sequencer::kInvalidSequence, "undo posts");
    {
        const InboxCommand cmd = pop();
        check(cmd.gen.kind == GenerativeCommandKind::Undo &&
                  cmd.gen.timing.active == true &&
                  cmd.gen.timing.mode ==
                      static_cast<uint8_t>(OperationTimingMode::QuantizeBeat),
              "Undo carries the active per-invocation override");
    }

    // Clearing the override restores the absent default.
    view.clearTimingOverride();
    check(!view.timingOverrideActive(), "clearing deactivates the override");
    check(view.requestKeep() != sequencer::kInvalidSequence, "keep posts");
    check(pop().gen.timing.active == false,
          "after clearing, the command carries no override again");

    // Cycling walks absent -> Immediate -> Beat -> Bar -> absent.
    view.clearTimingOverride();
    view.cycleTimingOverride(+1);
    check(view.timingOverrideActive() &&
              view.timingOverride() == OperationTimingMode::Immediate,
          "the first upward timing cycle lands on Immediate");
    view.cycleTimingOverride(+1);
    check(view.timingOverride() == OperationTimingMode::QuantizeBeat,
          "the second upward timing cycle lands on QuantizeBeat");
    view.cycleTimingOverride(+1);
    check(view.timingOverride() == OperationTimingMode::QuantizeBar,
          "the third upward timing cycle lands on QuantizeBar");
    view.cycleTimingOverride(+1);
    check(!view.timingOverrideActive(),
          "the fourth upward timing cycle returns to absent");
    view.cycleTimingOverride(-1);
    check(view.timingOverride() == OperationTimingMode::QuantizeBar,
          "a downward cycle from absent lands on QuantizeBar");
}

// --- 7. A full inbox is surfaced as a rejected action without blocking --------

void test_full_inbox_rejected_action() {
    MutationView view = makeView();

    // Fill the bounded ring with unrelated pending commands.
    bool allAccepted = true;
    for (int i = 0; i < inbox.capacity(); ++i) {
        if (inbox.tryPush(sequencer::makeMutateCommand(
                "builtin.rotate", 0,
                sequencer::maskOf(MutationTarget::PITCH_ORDERING), 0)) ==
            sequencer::kInvalidSequence) {
            allAccepted = false;
        }
    }
    check(allAccepted, "the fixture fills the bounded ring");
    check(inbox.full(), "the ring is full");

    // The trigger returns IMMEDIATELY (never blocks: tryPush is nonblocking
    // by contract), enqueues nothing and the rejection is surfaced for the
    // render path.
    const SequenceId refused = view.trigger(0);
    check(refused == sequencer::kInvalidSequence,
          "a trigger against a full inbox returns kInvalidSequence");
    check(view.lastActionRejected(),
          "the full inbox is surfaced as a rejected action");
    check(inbox.count() == inbox.capacity(),
          "the rejected trigger enqueues nothing");

    check(view.requestUndo() == sequencer::kInvalidSequence,
          "undo against a full inbox is refused too");
    check(view.requestRevert() == sequencer::kInvalidSequence,
          "revert against a full inbox is refused too");
    check(view.requestKeep() == sequencer::kInvalidSequence,
          "keep against a full inbox is refused too");
    check(inbox.count() == inbox.capacity(),
          "the rejected recovery commands enqueue nothing either");

    // A freed slot makes the view usable again and clears the rejection.
    InboxCommand drained{};
    check(inbox.tryPop(drained), "one slot is freed");
    check(view.trigger(0) != sequencer::kInvalidSequence,
          "a trigger succeeds again once a slot is free");
    check(!view.lastActionRejected(),
          "a successful post clears the rejected-action indication");
    check(pop().gen.kind == GenerativeCommandKind::Mutate,
          "the re-armed trigger posts a Mutate command");

    // Drain the fixture's fill commands so later sections start empty.
    drainInbox();
    check(inbox.empty(), "the fixture drains the ring again");

    // An empty registry cannot trigger at all: that is surfaced the same way
    // (a rejected action, nothing posted).
    MutationOperatorRegistry emptyRegistry;
    MutationView emptyView(emptyRegistry, inbox, matrix);
    check(emptyView.operatorCount() == 0, "the empty registry offers nothing");
    check(std::strcmp(emptyView.selectedOperatorId(), "") == 0,
          "an empty registry has no selected operator id");
    check(emptyView.trigger(0) == sequencer::kInvalidSequence,
          "triggering without a selected operator is refused");
    check(emptyView.lastActionRejected(),
          "the operator-less trigger is surfaced as a rejected action");
    check(inbox.empty(), "the operator-less trigger posts nothing");
    emptyView.cycleOperator(+1);
    check(emptyView.operatorCount() == 0,
          "cycling on an empty registry changes nothing");
}

// --- 8. The key front end + render totality ------------------------------------

void test_key_frontend_and_render() {
    MutationView view = makeView();

    // Field navigation: RIGHT cycles Operator -> Target -> Intensity ->
    // Timing and wraps; LEFT wraps back.
    check(view.selectedField() == MutationView::Field::Operator,
          "a fresh view edits the operator field");
    ui::state::UIState state;
    view.handleEvent(state, Event::keyPressed(KeyId::RIGHT, NONE, 0));
    check(view.selectedField() == MutationView::Field::Target,
          "RIGHT moves to the target field");
    view.handleEvent(state, Event::keyPressed(KeyId::RIGHT, NONE, 0));
    check(view.selectedField() == MutationView::Field::Intensity,
          "RIGHT moves to the intensity field");
    view.handleEvent(state, Event::keyPressed(KeyId::RIGHT, NONE, 0));
    check(view.selectedField() == MutationView::Field::Timing,
          "RIGHT moves to the timing field");
    view.handleEvent(state, Event::keyPressed(KeyId::RIGHT, NONE, 0));
    check(view.selectedField() == MutationView::Field::Operator,
          "RIGHT wraps around to the operator field");
    view.handleEvent(state, Event::keyPressed(KeyId::LEFT, NONE, 0));
    check(view.selectedField() == MutationView::Field::Timing,
          "LEFT wraps back to the timing field");

    // Direct field selection is total over the four fields; a foreign enum
    // byte is refused (the selection stays valid).
    check(view.selectField(MutationView::Field::Intensity),
          "selecting the intensity field succeeds");
    check(view.selectedField() == MutationView::Field::Intensity,
          "the field selection reads back");
    check(!view.selectField(static_cast<MutationView::Field>(99)),
          "an out-of-range field selection is refused");

    // UP/DOWN adjust the selected field; SHIFT is the coarse step.
    view.setIntensity(64);
    view.handleEvent(state, Event::keyPressed(KeyId::UP, NONE, 0));
    check(view.intensity() == 65, "UP steps the intensity up by 1");
    view.handleEvent(state, Event::keyPressed(KeyId::UP, SHIFT, 0));
    check(view.intensity() == 65 + MutationView::kCoarseStep,
          "SHIFT+UP steps the intensity by the coarse step");
    view.handleEvent(state, Event::keyPressed(KeyId::DOWN, NONE, 0));
    check(view.intensity() == 64 + MutationView::kCoarseStep,
          "DOWN steps the intensity back down by 1");

    // On the operator field UP/DOWN cycle the operators (with wrap).
    check(view.selectField(MutationView::Field::Operator),
          "back on the operator field");
    view.selectOperator(0);
    view.handleEvent(state, Event::keyPressed(KeyId::DOWN, NONE, 0));
    check(view.selectedOperatorIndex() == view.operatorCount() - 1,
          "DOWN on the operator field cycles with wrap");
    view.handleEvent(state, Event::keyPressed(KeyId::UP, NONE, 0));
    check(view.selectedOperatorIndex() == 0,
          "UP on the operator field cycles back");

    // On the target field UP/DOWN walk the FR7 target cursor and ENTER
    // toggles the cursor's target through the same operator gate.
    view.selectOperator(0);  // density: supports GATE_DENSITY only
    check(view.selectField(MutationView::Field::Target),
          "onto the target field");
    check(view.selectTargetIndex(0), "the cursor starts at gate density");
    check(view.hasTarget(MutationTarget::GATE_DENSITY),
          "density's target starts selected");
    view.handleEvent(state, Event::keyPressed(KeyId::ENTER, NONE, 0));
    check(!view.hasTarget(MutationTarget::GATE_DENSITY),
          "ENTER toggles the cursor's supported target");
    check(view.selectTargetIndex(1), "the cursor moves to pitch contents");
    view.handleEvent(state, Event::keyPressed(KeyId::UP, NONE, 0));
    check(view.selectedTargetIndex() == 2,
          "UP walks the target cursor");
    view.handleEvent(state, Event::keyPressed(KeyId::DOWN, NONE, 0));
    check(view.selectedTargetIndex() == 1,
          "DOWN walks the target cursor back");
    view.handleEvent(state, Event::keyPressed(KeyId::ENTER, NONE, 0));
    check(!view.hasTarget(MutationTarget::PITCH_CONTENTS),
          "ENTER on an unsupported target toggles nothing (still clear)");

    // On the timing field UP/DOWN cycle the override (absent -> modes).
    check(view.selectField(MutationView::Field::Timing),
          "onto the timing field");
    view.handleEvent(state, Event::keyPressed(KeyId::UP, NONE, 0));
    check(view.timingOverrideActive() &&
              view.timingOverride() == OperationTimingMode::Immediate,
          "UP on the timing field activates the Immediate override");

    // A non-key event changes nothing and posts nothing.
    const ui::state::UIState before;
    const ui::state::UIState after =
        view.handleEvent(before, Event::keyReleased(KeyId::ENTER, NONE, 0));
    check(after.currentView == before.currentView &&
              after.selectedPattern == before.selectedPattern,
          "a released key event leaves the UI state untouched");
    InboxCommand drained{};
    check(!inbox.tryPop(drained), "the released key event posts nothing");

    // ENTER on a non-target field triggers the mutation (pattern from the
    // UI state).
    state.selectedPattern = 2;
    check(view.selectField(MutationView::Field::Operator),
          "ENTER-side check: operator field selected");
    view.handleEvent(state, Event::keyPressed(KeyId::ENTER, NONE, 0));
    const InboxCommand keyCmd = pop();
    check(keyCmd.gen.kind == GenerativeCommandKind::Mutate &&
              keyCmd.gen.mutate.patternIndex == 2,
          "ENTER on the operator field triggers a mutation");

    // onEnter + render on every field runs to completion (the LedMatrix seam
    // observes nothing; a totality smoke under the sanitizers).
    view.onEnter();
    view.render(state);
    view.selectField(MutationView::Field::Operator);
    view.render(state);
    view.selectField(MutationView::Field::Target);
    view.render(state);
    view.selectField(MutationView::Field::Intensity);
    view.render(state);
    view.selectField(MutationView::Field::Timing);
    view.render(state);
}

}  // namespace

int main() {
    test_operator_list_from_registry();
    test_target_mask_restricted_to_operator();
    test_intensity_clamped();
    test_trigger_posts_mutate_command();
    test_recovery_commands();
    test_timing_override();
    test_full_inbox_rejected_action();
    test_key_frontend_and_render();

    if (failures == 0) {
        std::printf("test_mutation_view: all checks passed\n");
        return 0;
    }
    std::printf("test_mutation_view: %d check(s) failed\n", failures);
    return 1;
}
