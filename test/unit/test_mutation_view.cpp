// generative-engine-expansion task 20 — Mutation UI View (FR5-FR10, FR15);
// ui-integration T6 — command-queue refactor.
//
// Validates the mutation trigger view against the command-queue contract (the
// view holds NO registry and NO CommandInbox — the ONLY cross-core path is
// the command queue):
//   - the view's fixed metadata table mirrors the REAL built-in registry
//     EXACTLY (id / name / supported-target mask verbatim, in the SAME
//     registration order registerBuiltInMutations() uses — the order both
//     cores agree on, since a MUTATE command carries the operator by
//     INDEX);
//   - the target mask can only include the selected operator's
//     supportedTargets (FR8 operator control): unsupported toggles are
//     refused, and switching operators re-masks the selection;
//   - the intensity is clamped to 0-127 (FR6);
//   - Trigger posts ONE MUTATE command with the correct payload (operator
//     index, pattern index / the ALL_PATTERNS sentinel, target mask,
//     intensity, packed timing), verified through RECORDING host doubles of
//     commands::sendMutateCommand / sendUndoCommand / sendRevertCommand /
//     sendKeepCommand;
//   - Undo / Revert / Keep post the matching recovery commands (FR9, FR15);
//   - the timing override is ABSENT by default (the operation's configured
//     mode / the global default applies) and, when set, is carried in the
//     packed form of every posted command (FR10 level 1);
//   - ESCAPE returns the UIState to the PATTERNS parent view; held repeats
//     and releases are ignored;
//   - the sequencer core decodes the four generative commands: MUTATE
//     resolves the operator index through its OWN registry, applies the
//     mutation to the working state (pushing the undo snapshot first) and
//     honors the timing override (Immediate executes at once, the factory
//     QuantizeBeat default stages); UNDO / REVERT / KEEP drive the working
//     state's recovery model; an unknown operator index is refused
//     untouched; the ALL_PATTERNS sentinel fans out over the live slots.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_mutation_view".
//
// The view's render path targets the real LedMatrix driver (Pico SDK PIO),
// and command.cpp (the multicore FIFO) is not compiled host-side, so — like
// test_scale_selection_view — this TU supplies host-safe definitions: no-op
// LedMatrix member functions and RECORDING commands::send* doubles.

#include "FakeMonotonicClock.h"
#include "RecordingMidiOutput.h"
#include "commands/command.h"
#include "sequencer/MutationConstraints.h"
#include "sequencer/OperationTimingConfig.h"
#include "sequencer/mutations/MutationOperatorRegistry.h"
#include "sequencer.h"
#include "ui/Event.h"
#include "ui/hardware/LedMatrix.h"
#include "ui/state/UIState.h"
#include "ui/views/MutationView.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using sequencer::MutationTarget;
using sequencer::MutationTargetMask;
using sequencer::OperationTimingMode;
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

constexpr uint16_t gateDensityMask() {
    return sequencer::maskOf(MutationTarget::GATE_DENSITY);
}
constexpr uint16_t pitchContentsMask() {
    return sequencer::maskOf(MutationTarget::PITCH_CONTENTS);
}
constexpr uint16_t transformTargetsMask() {
    return sequencer::maskOf(MutationTarget::PITCH_ORDERING) |
           sequencer::maskOf(MutationTarget::VELOCITY_ORDERING) |
           sequencer::maskOf(MutationTarget::EUCLIDEAN_PARAMS);
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

// --- host-safe commands:: seam (RECORDING doubles) -----------------------------
// MutationView.cpp posts every action through commands::sendMutateCommand /
// sendUndoCommand / sendRevertCommand / sendKeepCommand; command.cpp (the
// multicore FIFO) is not compiled host-side, so this TU links against these
// recording definitions instead. Every posted command lands in the vectors
// below, and the tests assert on the exact payloads.

namespace commands {

struct MutateCall {
    uint8_t operatorIndex;
    uint8_t patternIndex;
    uint16_t targets;
    uint8_t intensity;
    uint8_t timing;
};
std::vector<MutateCall> g_mutateCalls;
std::vector<uint8_t> g_undoCalls;
std::vector<uint8_t> g_revertCalls;
std::vector<uint8_t> g_keepCalls;

void sendMutateCommand(uint8_t operatorIndex, uint8_t patternIndex,
                       uint16_t targets, uint8_t intensity, uint8_t timing) {
    g_mutateCalls.push_back({operatorIndex, patternIndex, targets, intensity, timing});
}
void sendUndoCommand(uint8_t timing) { g_undoCalls.push_back(timing); }
void sendRevertCommand(uint8_t timing) { g_revertCalls.push_back(timing); }
void sendKeepCommand(uint8_t timing) { g_keepCalls.push_back(timing); }

void resetPostedCommands() {
    g_mutateCalls.clear();
    g_undoCalls.clear();
    g_revertCalls.clear();
    g_keepCalls.clear();
}

}  // namespace commands

// --- the metadata mirror pins the view's table to the REAL registry ------------

void test_operator_metadata_mirrors_registry() {
    // The view's fixed metadata copy must list EXACTLY the built-in
    // operators registerBuiltInMutations() registers — same order (a MUTATE
    // command carries the operator by index), same ids, same names, same
    // supported-target masks. Drift breaks the cross-core contract, so the
    // real registry (header-only, SDK-free) is checked entry by entry.
    sequencer::MutationOperatorRegistry registry;
    sequencer::registerBuiltInMutations(registry);

    check(MutationView::operatorCount() == registry.count(),
          "metadata count equals the registered built-in count");
    check(registry.count() == 10, "ten built-in operators are registered");
    check(MutationView::operatorCount() == 10, "the metadata lists all ten");
    for (uint8_t i = 0; i < registry.count(); ++i) {
        const sequencer::IMutationOperator* op = registry.at(i);
        const bool idOk = std::strcmp(MutationView::operatorIdAt(i), op->id()) == 0;
        const bool nameOk = std::strcmp(MutationView::operatorNameAt(i), op->name()) == 0;
        const bool targetsOk = MutationView::operatorTargetsAt(i) == op->supportedTargets();
        if (!idOk || !nameOk || !targetsOk) {
            std::printf("FAIL: metadata entry %u differs from the registry (%s)\n",
                        i, op->id());
            ++failures;
        }
    }
    // Total accessors past the count.
    check(std::strcmp(MutationView::operatorIdAt(10), "") == 0,
          "operatorIdAt past the count is empty");
    check(std::strcmp(MutationView::operatorNameAt(200), "") == 0,
          "operatorNameAt far past the count is empty");
    check(MutationView::operatorTargetsAt(10) == 0,
          "operatorTargetsAt past the count is 0");
}

void test_operator_selection() {
    hardware::LedMatrix led{0};
    MutationView view{led};

    // A fresh view selects operator 0 (Density) with all of its supported
    // targets on.
    check(view.selectedOperatorIndex() == 0, "fresh view selects operator 0");
    check(std::strcmp(view.selectedOperatorId(), "builtin.mutate-density") == 0,
          "operator 0 is the density operator");
    check(std::strcmp(view.selectedOperatorName(), "Density") == 0,
          "operator 0's display name is Density");
    check(view.targetMask() == gateDensityMask(),
          "operator 0 starts with GATE_DENSITY selected");

    check(view.selectOperator(5), "selectOperator(5) is accepted");
    check(view.selectedOperatorIndex() == 5, "operator 5 is selected");
    check(std::strcmp(view.selectedOperatorId(), "builtin.rotate") == 0,
          "index 5 is the rotate operator");
    check(!view.selectOperator(10), "selectOperator past the count is refused");
    check(view.selectedOperatorIndex() == 5, "a refused selection changes nothing");
    check(!view.selectOperator(255), "selectOperator(255) is refused");

    view.cycleOperator(+1);
    check(view.selectedOperatorIndex() == 6, "cycleOperator(+1) walks forward");
    view.cycleOperator(-8);  // 6 - 8 wraps to 8
    check(view.selectedOperatorIndex() == 8, "cycleOperator wraps modulo the count");
}

void test_target_mask_restricted_to_operator() {
    hardware::LedMatrix led{0};
    MutationView view{led};

    // FR8 operator control: Density supports GATE_DENSITY only.
    check(view.supportedTargets() == gateDensityMask(),
          "density supports GATE_DENSITY only");
    check(!view.toggleTarget(MutationTarget::PITCH_CONTENTS),
          "an unsupported target toggle is refused");
    check(view.targetMask() == gateDensityMask(), "a refused toggle changes nothing");
    view.clearTargets();
    check(view.targetMask() == 0, "clearTargets empties the mask");
    check(view.toggleTarget(MutationTarget::GATE_DENSITY),
          "the supported target toggle is accepted (on)");
    check(view.hasTarget(MutationTarget::GATE_DENSITY), "GATE_DENSITY is selected");
    check(view.toggleTarget(MutationTarget::GATE_DENSITY),
          "the supported target toggle is accepted (off)");
    check(view.targetMask() == 0, "the second toggle cleared the bit");
    view.selectAllSupportedTargets();
    check(view.targetMask() == gateDensityMask(),
          "selectAllSupportedTargets selects the operator's mask");

    // The FR7 target cursor (fixed documented order, total accessor).
    check(view.targetAt(0) == MutationTarget::GATE_DENSITY, "target order 0");
    check(view.targetAt(6) == MutationTarget::EUCLIDEAN_PARAMS, "target order 6");
    check(view.targetAt(7) == MutationTarget::NONE, "targetAt past the count is NONE");
    check(view.selectTargetIndex(3), "selectTargetIndex(3) is accepted");
    check(view.selectedTargetIndex() == 3, "the target cursor moved");
    check(!view.selectTargetIndex(7), "selectTargetIndex past the count is refused");

    // Switching to the Rotate transform operator re-masks (FR8): the pitch
    // contents bit density selected is dropped, the transform targets are
    // all on.
    check(view.selectOperator(5), "rotate is selectable");
    check(view.targetMask() == transformTargetsMask(),
          "a fresh operator starts with its supported targets selected");
    check(!view.toggleTarget(MutationTarget::GATE_DENSITY),
          "density's target is refused for rotate");
    check(view.toggleTarget(MutationTarget::PITCH_ORDERING),
          "rotate's ordering target toggles");
    check(view.targetMask() ==
          (transformTargetsMask() & ~sequencer::maskOf(MutationTarget::PITCH_ORDERING)),
          "the toggled bit left the mask");
}

void test_intensity_clamped() {
    hardware::LedMatrix led{0};
    MutationView view{led};

    check(view.intensity() == MutationView::kDefaultIntensity,
          "the default intensity is 64");
    view.setIntensity(200);
    check(view.intensity() == 127, "intensity clamps at 127");
    view.setIntensity(-40);
    check(view.intensity() == 0, "intensity clamps at 0");
    view.adjustIntensity(-5);
    check(view.intensity() == 0, "adjusting below 0 stays clamped");
    view.adjustIntensity(+1000);
    check(view.intensity() == 127, "adjusting above 127 stays clamped");
}

void test_trigger_posts_mutate_command() {
    hardware::LedMatrix led{0};
    MutationView view{led};
    commands::resetPostedCommands();

    // Trigger on an explicit pattern: ONE MUTATE command with the selected
    // operator's INDEX, the pattern index, the selected FR7 mask, the FR6
    // intensity and the packed (absent) timing override.
    view.selectOperator(5);  // Rotate
    view.setIntensity(42);
    view.trigger(3);
    check(commands::g_mutateCalls.size() == 1,
          "trigger posts exactly one MUTATE command");
    if (commands::g_mutateCalls.size() == 1) {
        const commands::MutateCall& call = commands::g_mutateCalls[0];
        check(call.operatorIndex == 5, "MUTATE carries the operator index");
        check(call.patternIndex == 3, "MUTATE carries the pattern index");
        check(call.targets == transformTargetsMask(),
              "MUTATE carries the selected target mask");
        check(call.intensity == 42, "MUTATE carries the intensity");
        check(call.timing == 0, "MUTATE carries the absent override as 0");
    }

    // The all-patterns form carries the commands::ALL_PATTERNS sentinel.
    commands::resetPostedCommands();
    view.triggerAllPatterns();
    check(commands::g_mutateCalls.size() == 1, "triggerAllPatterns posts once");
    if (commands::g_mutateCalls.size() == 1) {
        check(commands::g_mutateCalls[0].patternIndex == commands::ALL_PATTERNS,
              "the all-patterns form carries the 0xFF sentinel");
    }

    // ENTER on the Operator field triggers the selected pattern; ENTER on
    // the Target field only toggles the target (no command).
    ui::state::UIState state;
    state.selectedPattern = 7;
    commands::resetPostedCommands();
    view.handleEvent(state, Event::keyPressed(KeyId::ENTER, NONE, 0));
    check(commands::g_mutateCalls.size() == 1,
          "ENTER on the operator field posts one MUTATE");
    if (commands::g_mutateCalls.size() == 1) {
        check(commands::g_mutateCalls[0].patternIndex == 7,
              "ENTER triggers the state's selected pattern");
    }
    commands::resetPostedCommands();
    view.selectField(MutationView::Field::Target);
    view.handleEvent(state, Event::keyPressed(KeyId::ENTER, NONE, 0));
    check(commands::g_mutateCalls.empty(),
          "ENTER on the target field posts no command (it toggles)");
}

void test_recovery_commands() {
    hardware::LedMatrix led{0};
    MutationView view{led};
    commands::resetPostedCommands();

    view.requestUndo();
    view.requestRevert();
    view.requestKeep();
    check(commands::g_undoCalls.size() == 1, "requestUndo posts one UNDO");
    check(commands::g_revertCalls.size() == 1, "requestRevert posts one REVERT");
    check(commands::g_keepCalls.size() == 1, "requestKeep posts one KEEP");
    check(commands::g_mutateCalls.empty(), "the recovery requests post no MUTATE");
    check(commands::g_undoCalls[0] == 0 && commands::g_revertCalls[0] == 0 &&
              commands::g_keepCalls[0] == 0,
          "the recovery commands carry the absent override as 0");

    // The U / R / K key front end posts the same commands.
    commands::resetPostedCommands();
    ui::state::UIState state;
    view.handleEvent(state, Event::keyPressed(KeyId::U, NONE, 0));
    view.handleEvent(state, Event::keyPressed(KeyId::R, NONE, 0));
    view.handleEvent(state, Event::keyPressed(KeyId::K, NONE, 0));
    check(commands::g_undoCalls.size() == 1 && commands::g_revertCalls.size() == 1 &&
              commands::g_keepCalls.size() == 1,
          "U/R/K post the recovery commands");

    // A RELEASED key posts nothing and leaves the state untouched.
    commands::resetPostedCommands();
    const ui::state::UIState unchanged = state;
    const ui::state::UIState after = view.handleEvent(
        state, Event::keyReleased(KeyId::U, NONE, 0));
    check(commands::g_mutateCalls.empty() && commands::g_undoCalls.empty() &&
              commands::g_revertCalls.empty() && commands::g_keepCalls.empty(),
          "a released key posts nothing");
    check(after.currentView == unchanged.currentView &&
              after.selectedPattern == unchanged.selectedPattern,
          "a released key leaves the UIState untouched");
}

void test_timing_override_carried() {
    hardware::LedMatrix led{0};
    MutationView view{led};

    // ABSENT by default (levels 2/3 decide); Inherit is refused.
    check(!view.timingOverrideActive(), "the override is absent by default");
    check(!view.setTimingOverride(OperationTimingMode::Inherit),
          "Inherit is refused as an override");
    check(!view.timingOverrideActive(), "the refused override armed nothing");

    // Arm QuantizeBar: every posted command carries the packed override.
    check(view.setTimingOverride(OperationTimingMode::QuantizeBar),
          "QuantizeBar is accepted as an override");
    check(view.timingOverrideActive() && view.timingOverride() == OperationTimingMode::QuantizeBar,
          "the override reads back");
    commands::resetPostedCommands();
    view.selectOperator(0);
    view.trigger(2);
    view.requestUndo();
    view.requestRevert();
    view.requestKeep();
    const uint8_t packed = commands::packGenerativeTiming(
        true, static_cast<uint8_t>(OperationTimingMode::QuantizeBar));
    check(commands::g_mutateCalls.size() == 1 && commands::g_mutateCalls[0].timing == packed,
          "MUTATE carries the packed override");
    check(commands::g_undoCalls.size() == 1 && commands::g_undoCalls[0] == packed,
          "UNDO carries the packed override");
    check(commands::g_revertCalls.size() == 1 && commands::g_revertCalls[0] == packed,
          "REVERT carries the packed override");
    check(commands::g_keepCalls.size() == 1 && commands::g_keepCalls[0] == packed,
          "KEEP carries the packed override");

    // The pack/unpack helpers round-trip the three armed modes.
    for (uint8_t ordinal = 0; ordinal < 3; ++ordinal) {
        const uint8_t p = commands::packGenerativeTiming(true, ordinal);
        check(commands::generativeTimingActive(p) &&
                  commands::generativeTimingMode(p) == ordinal,
              "pack/unpack round-trips the mode");
    }
    check(!commands::generativeTimingActive(0), "0 unpacks to an absent override");

    // clearTimingOverride returns to the absent default; cycleTimingOverride
    // walks absent -> Immediate -> Beat -> Bar -> absent.
    view.clearTimingOverride();
    check(!view.timingOverrideActive(), "clearTimingOverride disarms");
    view.cycleTimingOverride(+1);
    check(view.timingOverrideActive() &&
              view.timingOverride() == OperationTimingMode::Immediate,
          "the cycle arms Immediate");
    view.cycleTimingOverride(+1);
    check(view.timingOverride() == OperationTimingMode::QuantizeBeat,
          "the cycle reaches QuantizeBeat");
    view.cycleTimingOverride(+1);
    check(view.timingOverride() == OperationTimingMode::QuantizeBar,
          "the cycle reaches QuantizeBar");
    view.cycleTimingOverride(+1);
    check(!view.timingOverrideActive(), "the cycle wraps to absent");
    view.cycleTimingOverride(-1);
    check(view.timingOverride() == OperationTimingMode::QuantizeBar,
          "the cycle walks backwards to QuantizeBar");
}

void test_escape_returns_to_patterns() {
    hardware::LedMatrix led{0};
    MutationView view{led};

    ui::state::UIState state;
    state.currentView = ui::state::ViewId::MUTATION;
    state.selectedPattern = 4;
    commands::resetPostedCommands();

    Event esc = Event::keyPressed(KeyId::ESCAPE, NONE, 0);
    const ui::state::UIState next = view.handleEvent(state, esc);
    check(next.currentView == ui::state::ViewId::PATTERNS,
          "ESCAPE returns to the PATTERNS parent view");
    check(next.selectedPattern == 4, "ESCAPE preserves the selected pattern");
    check(commands::g_mutateCalls.empty() && commands::g_undoCalls.empty() &&
              commands::g_revertCalls.empty() && commands::g_keepCalls.empty(),
          "ESCAPE posts no command");

    // Held repeats and releases are ignored.
    Event escHeld = Event::keyHeld(KeyId::ESCAPE, NONE, 0);
    const ui::state::UIState held = view.handleEvent(state, escHeld);
    check(held.currentView == ui::state::ViewId::MUTATION,
          "a held ESCAPE repeat is ignored");
    const ui::state::UIState released = view.handleEvent(
        state, Event::keyReleased(KeyId::ESCAPE, NONE, 0));
    check(released.currentView == ui::state::ViewId::MUTATION,
          "a released ESCAPE is ignored");

    // A non-key event is ignored too.
    const ui::state::UIState other = view.handleEvent(
        state, Event{});  // default-constructed: KEY_PRESSED of an unused id
    check(other.currentView == ui::state::ViewId::MUTATION,
          "an unrelated event leaves the view");
}

void test_key_frontend_and_render() {
    hardware::LedMatrix led{0};
    MutationView view{led};
    ui::state::UIState state;

    // Field cycling wraps; selection is total.
    check(view.selectedField() == MutationView::Field::Operator,
          "the operator field is selected first");
    check(!view.selectField(static_cast<MutationView::Field>(9)),
          "a foreign field byte is refused");
    view.cycleField(+1);
    check(view.selectedField() == MutationView::Field::Target, "RIGHT walks to Target");
    view.cycleField(+3);
    check(view.selectedField() == MutationView::Field::Operator,
          "field cycling wraps back to Operator");

    // UP/DOWN adjust the selected field; SHIFT is coarse.
    view.selectField(MutationView::Field::Intensity);
    view.setIntensity(50);
    view.handleEvent(state, Event::keyPressed(KeyId::UP, SHIFT, 0));
    check(view.intensity() == 60, "SHIFT+UP is the coarse intensity step");
    view.handleEvent(state, Event::keyPressed(KeyId::DOWN, NONE, 0));
    check(view.intensity() == 59, "DOWN steps the intensity down by 1");
    view.selectField(MutationView::Field::Operator);
    view.handleEvent(state, Event::keyPressed(KeyId::UP, NONE, 0));
    check(view.selectedOperatorIndex() == 1, "UP on the operator field walks forward");
    view.selectField(MutationView::Field::Target);
    view.handleEvent(state, Event::keyPressed(KeyId::UP, NONE, 0));
    check(view.selectedTargetIndex() == 1, "UP on the target field walks the cursor");
    view.selectField(MutationView::Field::Timing);
    view.handleEvent(state, Event::keyPressed(KeyId::UP, NONE, 0));
    check(view.timingOverrideActive(), "UP on the timing field arms the override");

    // render() total: onEnter plus render over every field.
    view.onEnter();
    view.render(state);
    view.selectField(MutationView::Field::Target);
    view.render(state);
    view.selectField(MutationView::Field::Intensity);
    view.render(state);
    view.selectField(MutationView::Field::Timing);
    view.render(state);
}

// --- the sequencer core's FIFO generative handlers (ui-integration T6) --------

namespace {

constexpr uint8_t kOpDensity = 0;      // builtin.mutate-density (index 0)
constexpr uint8_t kImmediateOverride = // the packed level-1 Immediate override
    commands::packGenerativeTiming(true, static_cast<uint8_t>(OperationTimingMode::Immediate));

commands::CommandMessage mutateMessage(uint8_t opIndex, uint8_t patternIndex,
                                       uint16_t targets, uint8_t intensity,
                                       uint8_t timing) {
    commands::CommandMessage msg;
    msg.cmd = commands::Command::MUTATE;
    msg.param1 = opIndex;
    msg.param2 = patternIndex;
    msg.mutateTargets = targets;
    msg.mutateIntensity = intensity;
    msg.genTiming = timing;
    return msg;
}

commands::CommandMessage recoveryMessage(commands::Command cmd,
                                         uint8_t timing) {
    commands::CommandMessage msg;
    msg.cmd = cmd;
    msg.param1 = timing;  // the packed override rides param1 (see command.cpp)
    msg.genTiming = timing;
    return msg;
}

int pulsesOf(const sequencer::Sequencer& seq) {
    const sequencer::PatternGenerativeState* state = seq.generativeState(0);
    return state ? state->generatorParam(sequencer::GeneratorRole::GATE,
                                         sequencer::EuclideanGateGenerator::kParamPulses)
                 : -1;
}

}  // namespace

void test_fifo_generative_dispatch() {
    RecordingMidiOutput midi;
    FakeMonotonicClock clock;
    sequencer::Sequencer seq(midi, clock);

    // Seed a real working state: a 16-step Euclidean gate with 4 pulses.
    // (A density mutation moves the pulse count; the magnitude is ALWAYS >= 1,
    // so 4 can never stay 4.)
    sequencer::PatternGenerativeState* state = seq.generativeState(0);
    check(state != nullptr, "pattern 0 has a working state");
    state->setGeneratorParam(sequencer::GeneratorRole::GATE,
                             sequencer::EuclideanGateGenerator::kParamSteps, 16);
    state->setGeneratorParam(sequencer::GeneratorRole::GATE,
                             sequencer::EuclideanGateGenerator::kParamPulses, 4);

    // KEEP first: anchor the baseline at the seeded state so Revert has a
    // defined target (the factory baseline is the all-zero state).
    seq.processCommand(recoveryMessage(commands::Command::KEEP, 0));
    check(seq.workingStateManager().undoStack().empty(),
          "an empty-stack KEEP is a no-op that clears nothing");
    check(pulsesOf(seq) == 4, "KEEP preserves the working state");
    check(seq.workingStateManager().baseline(0) != nullptr &&
              seq.workingStateManager().baseline(0)->generatorParam(
                  sequencer::GeneratorRole::GATE,
                  sequencer::EuclideanGateGenerator::kParamPulses) == 4,
          "KEEP anchored the baseline at the seeded state");

    // MUTATE (Immediate override): applies at once through the sequencer's
    // OWN registry — the working state changes and the undo snapshot is
    // pushed BEFORE the operator runs.
    seq.processCommand(mutateMessage(kOpDensity, 0, gateDensityMask(), 64,
                                     kImmediateOverride));
    check(pulsesOf(seq) != 4, "the density MUTATE changed the pulse count");
    check(seq.workingStateManager().undoStack().size() == 1,
          "an applied mutation pushed one undo snapshot");
    check(seq.pendingGenerativeCount() == 0,
          "an Immediate MUTATE leaves nothing staged");

    // UNDO: restores the pre-mutation state.
    seq.processCommand(recoveryMessage(commands::Command::UNDO, 0));
    check(pulsesOf(seq) == 4, "UNDO restored the pre-mutation state");
    check(seq.workingStateManager().undoStack().empty(), "UNDO popped the stack");

    // MUTATE then REVERT: every pattern's baseline restored, stack cleared.
    seq.processCommand(mutateMessage(kOpDensity, 0, gateDensityMask(), 64,
                                     kImmediateOverride));
    check(pulsesOf(seq) != 4, "the second MUTATE changed the pulse count");
    seq.processCommand(recoveryMessage(commands::Command::REVERT, 0));
    check(pulsesOf(seq) == 4, "REVERT restored the kept baseline");
    check(seq.workingStateManager().undoStack().empty(), "REVERT cleared the stack");

    // MUTATE then KEEP: the baseline re-anchors at the mutated state, so a
    // later REVERT restores THAT point.
    seq.processCommand(mutateMessage(kOpDensity, 0, gateDensityMask(), 64,
                                     kImmediateOverride));
    const int mutated = pulsesOf(seq);
    check(mutated != 4, "the third MUTATE changed the pulse count");
    seq.processCommand(recoveryMessage(commands::Command::KEEP, 0));
    check(seq.workingStateManager().undoStack().empty(),
          "KEEP cleared the undo stack");
    seq.processCommand(recoveryMessage(commands::Command::REVERT, 0));
    check(pulsesOf(seq) == mutated, "REVERT after KEEP restores the kept point");

    // An unknown operator index is refused untouched (the validation path).
    const int before = pulsesOf(seq);
    const size_t stackBefore = seq.workingStateManager().undoStack().size();
    seq.processCommand(mutateMessage(200, 0, gateDensityMask(), 64,
                                     kImmediateOverride));
    check(pulsesOf(seq) == before, "an unknown operator index changes nothing");
    check(seq.workingStateManager().undoStack().size() == stackBefore,
          "an unknown operator index pushes no snapshot");
    check(seq.pendingGenerativeCount() == 0, "nothing was staged either");
    // An out-of-range pattern slot is refused the same way.
    seq.processCommand(mutateMessage(kOpDensity, 9, gateDensityMask(), 64,
                                     kImmediateOverride));
    check(pulsesOf(seq) == before, "an unknown pattern slot changes nothing");

    // MUTATE with NO override: the factory level-2 default for Mutate is
    // QuantizeBeat, so the command STAGES (bounded pending list) instead of
    // executing immediately.
    seq.processCommand(mutateMessage(kOpDensity, 0, gateDensityMask(), 64, 0));
    check(seq.pendingGenerativeCount() == 1, "a default-timing MUTATE stages");
    check(pulsesOf(seq) == before, "a staged MUTATE has not executed yet");

    // A QuantizeBar override stages too; the pending list holds both.
    seq.processCommand(mutateMessage(kOpDensity, 0, gateDensityMask(), 64,
                                      commands::packGenerativeTiming(
                                          true, static_cast<uint8_t>(
                                                    OperationTimingMode::QuantizeBar))));
    check(seq.pendingGenerativeCount() == 2, "a QuantizeBar MUTATE stages as well");

    // The ALL_PATTERNS sentinel fans out over the live slots. Re-seed the
    // working state first: the earlier mutations may have driven the pulse
    // count onto a boundary (0 or 16), where the deterministic redraw of an
    // identical command draws the same clamped no-op. From an interior value
    // the density mutation ALWAYS moves the count (magnitude >= 1).
    seq.generativeState(0)->setGeneratorParam(
        sequencer::GeneratorRole::GATE, sequencer::EuclideanGateGenerator::kParamPulses, 4);
    const int beforeFanout = pulsesOf(seq);
    check(beforeFanout == 4, "the working state re-seeded to an interior value");
    seq.processCommand(mutateMessage(kOpDensity, commands::ALL_PATTERNS,
                                     gateDensityMask(), 64, kImmediateOverride));
    check(pulsesOf(seq) != beforeFanout,
          "the ALL_PATTERNS MUTATE reached pattern 0");

    // UNDO with the override carried in param1 decodes and applies.
    seq.processCommand(recoveryMessage(commands::Command::UNDO, kImmediateOverride));
    check(pulsesOf(seq) == beforeFanout,
          "UNDO (override in param1) restored the fan-out mutation");
}

int main() {
    test_operator_metadata_mirrors_registry();
    test_operator_selection();
    test_target_mask_restricted_to_operator();
    test_intensity_clamped();
    test_trigger_posts_mutate_command();
    test_recovery_commands();
    test_timing_override_carried();
    test_escape_returns_to_patterns();
    test_key_frontend_and_render();
    test_fifo_generative_dispatch();
    if (failures == 0) {
        std::printf("test_mutation_view: all checks passed\n");
        return 0;
    }
    std::printf("test_mutation_view: %d check(s) failed\n", failures);
    return 1;
}
