// generative-engine-expansion task 21 — Modulation Pattern UI View (FR11-
// FR14).
//
// Validates the modulation pattern editor + target configuration view:
//   - the length control is clamped to 1-16 (0 becomes 1, 17 becomes 16) and
//     writes through to the global ModulationPattern;
//   - only steps within the current length are editable or highlighted
//     (the step cursor can never leave the active length, and both step
//     edits and selections past it are refused);
//   - shortening and then re-lengthening restores the stored values (the
//     view never clears steps beyond the length);
//   - step values are clamped to 0-127 (absolute and relative edits);
//   - the target picker lists EVERY ParameterBus parameter, including the
//     global BPM, with the names/ranges/scopes taken verbatim from the
//     task-16 table through the bus;
//   - the scope defaults to per-pattern (FR13) and can be toggled to
//     global, keeping the address VALID (a scope-mismatched parameter is
//     re-pointed: global keeps only BPM, per-pattern keeps the first
//     per-pattern parameter), and a freshly added target keeps its
//     deliberately INVALID factory address (default behavior stays
//     stable);
//   - scaling, balance, step duration and playing-order edits write to
//     the ModulationTarget in the ModulationRegistry;
//   - target slots: ENTER/addTarget claims a slot in order, a full
//     registry refuses (the rejection latched for render), released slots
//     are not selectable, edits without a selected target are refused;
//   - the key front end cycles fields, moves the step cursor within the
//     length (leaving the field at the edge), edits values with a SHIFT
//     coarse step, claims a target on ENTER, mirrors the UI's selected
//     pattern into the per-pattern address slot, ignores non-key events
//     and returns the UIState unchanged; render is exercised on every
//     field.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_modulation_pattern_view".
//
// The view's render path targets the real LedMatrix driver (Pico SDK PIO),
// so — like test_generator_config_view / test_scale_selection_view /
// test_mutation_view — this TU supplies host-safe no-op definitions of the
// LedMatrix member functions instead of compiling the SDK-dependent
// LedMatrix.cpp.

#include "sequencer/ModulationRegistry.h"
#include "sequencer/ModulationTarget.h"
#include "sequencer/ParameterAddress.h"
#include "sequencer/ParameterBus.h"
#include "sequencer/TransportTimeline.h"
#include "sequencer/WorkingStateManager.h"
#include "ui/Event.h"
#include "ui/hardware/LedMatrix.h"
#include "ui/state/UIState.h"
#include "ui/views/ModulationPatternView.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using sequencer::ModulationOrder;
using sequencer::ModulationRegistry;
using sequencer::ParameterAddress;
using sequencer::ParameterBus;
using sequencer::ParameterScope;
using sequencer::ParamId;
using sequencer::StepUnit;
using sequencer::TransportTimeline;
using sequencer::WorkingStateManager;
using ui::events::Event;
using ui::KeyId;
using ui::mod::NONE;
using ui::mod::SHIFT;
using ui::ModulationPatternView;

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

TransportTimeline timeline;
WorkingStateManager working;
ParameterBus bus;
hardware::LedMatrix matrix{16};

// A fresh view over a fresh local registry (the registry is a small fixed
// POD; a local instance keeps every test independent — the process-wide
// g_modulationRegistry stays untouched).
ModulationPatternView makeView(ModulationRegistry& registry) {
    return ModulationPatternView(registry, bus, matrix);
}

// --- 1. the length control is clamped to 1-16 ----------------------------------

void test_length_clamped_to_1_16() {
    ModulationRegistry registry;
    ModulationPatternView view = makeView(registry);

    view.setPatternLength(0);
    check(view.patternLength() == 1, "length 0 clamps to 1");
    check(registry.pattern(0).length() == 1,
          "the clamped length writes through to the global pattern");

    view.setPatternLength(17);
    check(view.patternLength() == 16, "length 17 clamps to 16");
    check(registry.pattern(0).length() == 16, "the clamp is stored in the pattern");

    view.setPatternLength(-3);
    check(view.patternLength() == 1, "a negative length clamps to 1");

    view.setPatternLength(7);
    check(view.patternLength() == 7, "an in-range length applies as-is");

    // Relative edits clamp at both ends too.
    view.setPatternLength(15);
    view.adjustPatternLength(+5);
    check(view.patternLength() == 16, "relative length edit clamps at 16");
    view.adjustPatternLength(-30);
    check(view.patternLength() == 1, "relative length edit clamps at 1");
}

// --- 2. only steps within the current length are editable or highlighted -------

void test_only_in_length_steps_editable_or_highlighted() {
    ModulationRegistry registry;
    ModulationPatternView view = makeView(registry);
    view.setPatternLength(4);

    for (uint8_t i = 0; i < 4; ++i) {
        check(view.stepEditable(i), "in-length steps are editable");
        check(view.stepHighlighted(i), "in-length steps are highlighted");
    }
    for (uint8_t i = 4; i < ModulationRegistry::MAX_PATTERNS * 2; ++i) {
        check(!view.stepEditable(i), "steps past the length are not editable");
        check(!view.stepHighlighted(i), "steps past the length are not highlighted");
    }

    check(view.selectStep(2) && view.selectedStep() == 2,
          "an in-length step is selectable");
    check(!view.selectStep(4), "selecting a step past the length is refused");
    check(view.selectedStep() == 2, "a refused selection leaves the cursor");
    check(!view.setStepValue(4, 42), "editing a step past the length is refused");
    check(!view.setStepValue(16, 42), "editing beyond the fixed capacity is refused");

    view.setPatternLength(16);
    check(view.stepValue(4) == 0, "the refused edit wrote nothing");
    check(view.stepEditable(4) && view.stepHighlighted(4),
          "re-lengthening makes the step editable/highlighted again");

    // Shortening clamps a stale cursor back into the length.
    view.selectStep(9);
    view.setPatternLength(4);
    check(view.selectedStep() < 4, "the cursor stays within a shortened length");
}

// --- 3. shortening and then re-lengthening restores the stored values ----------

void test_shorten_relength_restores_values() {
    ModulationRegistry registry;
    ModulationPatternView view = makeView(registry);

    view.setPatternLength(16);
    for (uint8_t i = 0; i < 16; ++i) {
        check(view.setStepValue(i, static_cast<int>(i) * 8), "step value set");
    }

    view.setPatternLength(4);
    for (uint8_t i = 0; i < 4; ++i) {
        check(view.stepValue(i) == i * 8, "in-length values survive the shorten");
    }

    view.setPatternLength(16);
    for (uint8_t i = 0; i < 16; ++i) {
        check(view.stepValue(i) == i * 8,
              "re-lengthening restores every stored value");
    }

    // The values were never cleared in the pattern itself.
    for (uint8_t i = 0; i < 16; ++i) {
        check(registry.pattern(0).stepAt(i) == i * 8,
              "the pattern storage kept the shortened-out steps");
    }
}

// --- 4. step values are clamped to 0-127 ---------------------------------------

void test_step_values_clamped_to_0_127() {
    ModulationRegistry registry;
    ModulationPatternView view = makeView(registry);

    check(view.setStepValue(0, 300), "the edit applies");
    check(view.stepValue(0) == 127, "300 clamps to 127");
    check(view.setStepValue(0, -50), "the edit applies");
    check(view.stepValue(0) == 0, "-50 clamps to 0");
    check(registry.pattern(0).stepAt(0) == 0,
          "the clamped value is stored in the pattern");

    view.selectStep(1);
    view.setStepValue(1, 100);
    view.adjustStepValue(+50);
    check(view.stepValue(1) == 127, "a relative edit clamps high at 127");
    view.adjustStepValue(-200);
    check(view.stepValue(1) == 0, "a relative edit clamps low at 0");
}

// --- 5. the target picker lists every ParameterBus parameter ------------------

void test_param_picker_lists_every_bus_parameter() {
    ModulationRegistry registry;
    ModulationPatternView view = makeView(registry);

    check(view.paramCount() == sequencer::kParamIdCount,
          "the picker lists the whole parameter table");
    bool sawBpm = false;
    uint8_t globalCount = 0;
    for (uint8_t i = 0; i < view.paramCount(); ++i) {
        ModulationPatternView::ParamOption option;
        check(view.paramAt(i, option), "paramAt succeeds in range");
        const sequencer::ParamInfo& info = sequencer::kParamInfos[i];
        check(std::strcmp(option.name, info.name) == 0,
              "picker names come verbatim from the bus table");
        check(option.minValue == info.minValue && option.maxValue == info.maxValue,
              "picker ranges come verbatim from the bus table");
        check(option.scope == info.scope, "picker scopes come verbatim from the table");
        if (info.scope == sequencer::ParameterScope::Global) {
            ++globalCount;
            if (i == 0) sawBpm = true;
        }
    }
    check(sawBpm, "global BPM is picker entry 0");
    check(globalCount == 1, "exactly one global parameter (the BPM)");

    ModulationPatternView::ParamOption option;
    check(!view.paramAt(view.paramCount(), option),
          "an out-of-range picker entry is refused");

    // The picker cursor cycles the full table with wrap.
    view.cycleParam(-1);
    check(view.selectedParam() == view.paramCount() - 1,
          "the picker cursor wraps backwards over the full table");
    view.cycleParam(+1);
    check(view.selectedParam() == 0, "the picker cursor wraps forwards");
}

// --- 6. the scope defaults to per-pattern and toggles to global ----------------

void test_scope_default_per_pattern_toggles_global() {
    ModulationRegistry registry;
    ModulationPatternView view = makeView(registry);
    const uint8_t slot = view.addTarget();
    check(slot == 0, "the first target claims slot 0");
    check(view.hasSelectedTarget(), "the claimed slot is selected");

    // FR13: per-pattern by default.
    check(view.targetScope() == sequencer::ParameterScope::Pattern,
          "the factory scope is per-pattern");
    // The factory address stays deliberately INVALID: an unconfigured
    // target modulates nothing (default behavior stays stable).
    check(!sequencer::validAddress(registry.target(slot).address),
          "an unconfigured target keeps its invalid factory address");

    view.toggleTargetScope();
    check(view.targetScope() == sequencer::ParameterScope::Global,
          "the scope toggles to global");
    // The factory paramId is BPM (the only global parameter): global scope
    // binds a VALID BPM address.
    check(sequencer::validAddress(registry.target(slot).address),
          "the global scope binds a valid address");
    check(registry.target(slot).address.paramId == sequencer::ParamId::GlobalBpm,
          "the global target addresses the BPM");
    check(view.selectedParam() == 0, "the picker cursor follows the binding");

    view.toggleTargetScope();
    check(view.targetScope() == sequencer::ParameterScope::Pattern,
          "the scope toggles back to per-pattern");
    // BPM is global-only: the re-point keeps the address valid.
    check(sequencer::validAddress(registry.target(slot).address),
          "the per-pattern toggle re-points to a valid address");
    check(registry.target(slot).address.paramId == sequencer::ParamId::GateSteps,
          "the first per-pattern parameter is re-pointed");
}

void test_scope_toggle_repoints_mismatched_param() {
    ModulationRegistry registry;
    ModulationPatternView view = makeView(registry);
    const uint8_t slot = view.addTarget();
    view.selectAddressPatternSlot(5);
    check(view.selectParam(2), "Gate Pulses (per-pattern) selected");
    check(registry.target(slot).address.paramId == sequencer::ParamId::GatePulses,
          "the selected parameter is bound");

    check(view.setTargetScope(sequencer::ParameterScope::Global),
          "the scope switch applies");
    check(registry.target(slot).address.paramId == sequencer::ParamId::GlobalBpm,
          "a global toggle re-points a per-pattern parameter to the BPM");
    check(sequencer::validAddress(registry.target(slot).address),
          "the re-pointed global address is valid");

    check(view.setTargetScope(sequencer::ParameterScope::Pattern),
          "the scope switch applies");
    check(registry.target(slot).address.paramId == sequencer::ParamId::GateSteps,
          "a per-pattern toggle re-points the BPM to the first per-pattern param");
    check(registry.target(slot).address.patternIndex == 5,
          "the per-pattern address keeps the selected pattern slot");
    check(sequencer::validAddress(registry.target(slot).address),
          "the re-pointed per-pattern address is valid");
    check(view.selectedParam() == 1, "the picker cursor follows the re-point");
}

// --- 7. scaling/balance/duration/order edits write to the ModulationTarget ----

void test_config_edits_write_the_modulation_target() {
    ModulationRegistry registry;
    ModulationPatternView view = makeView(registry);
    const uint8_t slot = view.addTarget();
    const sequencer::ModulationTarget& target = registry.target(slot);

    // The per-pattern address slot the view writes for per-pattern params.
    check(view.selectAddressPatternSlot(3) && view.addressPatternSlot() == 3,
          "the address pattern slot is selectable");
    check(!view.selectAddressPatternSlot(16),
          "an out-of-capacity pattern slot is refused");

    // Parameter selection writes the whole address (FR12).
    check(view.selectParam(1), "Gate Steps selected");
    ParameterAddress expected;
    expected.scope = sequencer::ParameterScope::Pattern;
    expected.patternIndex = 3;
    expected.paramId = sequencer::ParamId::GateSteps;
    check(target.address == expected, "selectParam writes the target address");
    check(view.selectParam(view.paramCount()) == false,
          "an out-of-range parameter is refused");
    check(view.selectParam(0), "global BPM selectable");
    check(target.address.scope == sequencer::ParameterScope::Global &&
              target.address.paramId == sequencer::ParamId::GlobalBpm,
          "selecting the BPM writes the global scope");

    // FR14 scaling: clamped to 0-127.
    check(view.setTargetScaling(200), "the scaling edit applies");
    check(target.scaleFactor == 127, "scaling 200 clamps to 127");
    view.setTargetScaling(-5);
    check(target.scaleFactor == 0, "a negative scaling clamps to 0");
    view.adjustTargetScaling(+70);
    check(target.scaleFactor == 70, "a relative scaling edit writes through");

    // FR14 balance: clamped to -127..127.
    check(view.setTargetBalance(-200), "the balance edit applies");
    check(target.balance == -127, "balance -200 clamps to -127");
    view.setTargetBalance(33);
    check(target.balance == 33, "an in-range balance applies");
    view.adjustTargetBalance(-100);
    check(target.balance == -67, "a relative balance edit writes through");

    // FR14 step duration: unit + clamped length.
    check(view.setStepDuration(sequencer::StepUnit::Bar, 99), "the duration applies");
    check(target.stepDuration.unit == sequencer::StepUnit::Bar &&
              target.stepDuration.length == 16,
          "the step duration clamps to bar/16");
    view.setStepDurationLength(0);
    check(target.stepDuration.length == 1, "a zero duration length clamps to 1");
    check(!view.setStepDuration(static_cast<sequencer::StepUnit>(7), 1),
          "a foreign duration unit is refused");
    view.toggleStepDurationUnit();
    check(target.stepDuration.unit == sequencer::StepUnit::Beat,
          "the duration unit toggles back to beat");

    // FR11 playing order.
    check(view.setTargetOrder(sequencer::ModulationOrder::PingPong),
          "the order edit applies");
    check(target.order == sequencer::ModulationOrder::PingPong,
          "the playing order writes through");
    check(!view.setTargetOrder(static_cast<sequencer::ModulationOrder>(9)),
          "a foreign playing order is refused");
    view.cycleTargetOrder(+1);
    check(target.order == sequencer::ModulationOrder::Random,
          "the order cycles forward to random");
    view.cycleTargetOrder(-1);
    check(target.order == sequencer::ModulationOrder::PingPong,
          "the order cycles back");

    // Which global pattern drives the target.
    check(view.setTargetPattern(5), "the pattern binding applies");
    check(target.patternIndex == 5, "the pattern binding writes through");
    check(!view.setTargetPattern(static_cast<uint8_t>(ModulationRegistry::MAX_PATTERNS)),
          "an out-of-pool pattern binding is refused");
}

// --- 8. target slot lifecycle: claim/select/release/full registry --------------

void test_target_slot_lifecycle() {
    ModulationRegistry registry;
    ModulationPatternView view = makeView(registry);

    check(!view.hasSelectedTarget(), "a fresh registry has no selected target");
    for (uint8_t i = 0; i < ModulationRegistry::MAX_TARGETS; ++i) {
        check(view.addTarget() == i, "slots are claimed in order");
        check(!view.lastActionRejected(), "a successful claim clears the latch");
    }
    check(view.addTarget() == ModulationPatternView::kNoTarget,
          "a full registry refuses the next target");
    check(view.lastActionRejected(), "the refusal is latched for render");

    check(view.selectTargetSlot(3) && view.selectedTargetSlot() == 3,
          "an active slot is selectable");
    check(view.removeSelectedTarget(), "the selected slot is released");
    check(!view.selectTargetSlot(3), "a released slot is not selectable");
    check(view.selectTargetSlot(2), "another active slot is selectable");
    check(view.addTarget() == 3, "the freed slot is reused first");
    check(!view.lastActionRejected(), "the reuse clears the latch");

    // Edits without a selected target are refused (nothing is written).
    ModulationRegistry empty;
    ModulationPatternView emptyView = makeView(empty);
    check(!emptyView.selectParam(1), "param selection is refused without a target");
    check(!emptyView.setTargetScope(sequencer::ParameterScope::Global),
          "a scope edit is refused without a target");
    check(!emptyView.setTargetScaling(64), "a scaling edit is refused without a target");
    check(!emptyView.setTargetBalance(0), "a balance edit is refused without a target");
    check(!emptyView.setStepDuration(sequencer::StepUnit::Bar, 2),
          "a duration edit is refused without a target");
    check(!emptyView.setTargetOrder(sequencer::ModulationOrder::Reverse),
          "an order edit is refused without a target");
    check(!emptyView.setTargetPattern(1),
          "a pattern binding is refused without a target");
    check(emptyView.targetScope() == sequencer::ParameterScope::Pattern,
          "reads without a target stay total (the FR13 default)");
    check(emptyView.targetScaling() == 64, "scaling reads stay total (factory)");

    // A pre-populated registry auto-selects its first active slot.
    ModulationRegistry preloaded;
    preloaded.addTarget();
    ModulationPatternView preloadedView = makeView(preloaded);
    check(preloadedView.hasSelectedTarget() && preloadedView.selectedTargetSlot() == 0,
          "the constructor selects the first active target");
}

// --- 9. the key front end and the render path ----------------------------------

void test_key_frontend_and_render() {
    ModulationRegistry registry;
    ModulationPatternView view = makeView(registry);
    ui::state::UIState state;

    check(view.selectedField() == ModulationPatternView::Field::Pattern,
          "the default field is Pattern");
    check(!view.selectField(static_cast<ModulationPatternView::Field>(11)),
          "a foreign field ordinal is refused");

    ui::state::UIState after = view.handleEvent(
        state, Event::keyPressed(KeyId::RIGHT, NONE, 0));
    check(view.selectedField() == ModulationPatternView::Field::Length,
          "RIGHT cycles the field");
    check(after.selectedPattern == state.selectedPattern,
          "handleEvent returns the state unchanged");

    // Non-key events are ignored entirely.
    view.handleEvent(state, Event::keyReleased(KeyId::UP, NONE, 0));
    check(view.selectedField() == ModulationPatternView::Field::Length,
          "a key release changes nothing");

    // UP/DOWN adjust the length; SHIFT is a coarse step.
    view.handleEvent(state, Event::keyPressed(KeyId::DOWN, SHIFT, 0));
    check(view.patternLength() == 6, "SHIFT+DOWN coarsely shortens the length");
    view.handleEvent(state, Event::keyPressed(KeyId::UP, NONE, 0));
    check(view.patternLength() == 7, "UP lengthens the pattern");

    // On the Step field LEFT/RIGHT move the cursor within the length and
    // leave the field at the edge.
    view.selectField(ModulationPatternView::Field::Step);
    view.setPatternLength(4);
    view.selectStep(0);
    view.handleEvent(state, Event::keyPressed(KeyId::RIGHT, NONE, 0));
    check(view.selectedStep() == 1, "RIGHT moves the step cursor");
    view.handleEvent(state, Event::keyPressed(KeyId::RIGHT, NONE, 0));
    view.handleEvent(state, Event::keyPressed(KeyId::RIGHT, NONE, 0));
    check(view.selectedStep() == 3, "the cursor reaches the last in-length step");
    view.handleEvent(state, Event::keyPressed(KeyId::RIGHT, NONE, 0));
    check(view.selectedStep() == 3, "the cursor never leaves the length");
    check(view.selectedField() == ModulationPatternView::Field::Target,
          "RIGHT at the edge leaves the step field");
    view.handleEvent(state, Event::keyPressed(KeyId::LEFT, NONE, 0));
    check(view.selectedField() == ModulationPatternView::Field::Step,
          "LEFT from the target field re-enters the step field");
    check(view.selectedStep() == 3, "re-entering keeps the cursor");
    view.handleEvent(state, Event::keyPressed(KeyId::LEFT, NONE, 0));
    view.handleEvent(state, Event::keyPressed(KeyId::LEFT, NONE, 0));
    view.handleEvent(state, Event::keyPressed(KeyId::LEFT, NONE, 0));
    check(view.selectedStep() == 0, "LEFT walks the cursor back to step 0");
    view.handleEvent(state, Event::keyPressed(KeyId::LEFT, NONE, 0));
    check(view.selectedField() == ModulationPatternView::Field::Length,
          "LEFT at step 0 leaves the step field backwards");

    // UP/DOWN on the step field edit the cursor step's value (SHIFT coarse).
    view.selectField(ModulationPatternView::Field::Step);
    view.selectStep(0);
    view.handleEvent(state, Event::keyPressed(KeyId::UP, SHIFT, 0));
    check(view.stepValue(0) == 10, "SHIFT+UP coarsely edits the step value");

    // ENTER on the Target field claims a target slot.
    view.selectField(ModulationPatternView::Field::Target);
    view.handleEvent(state, Event::keyPressed(KeyId::ENTER, NONE, 0));
    check(view.hasSelectedTarget() && view.selectedTargetSlot() == 0,
          "ENTER claims a target slot");
    check(!view.lastActionRejected(), "the claim was accepted");

    // The per-pattern address slot mirrors the UI's selected pattern.
    state.selectedPattern = 7;
    view.handleEvent(state, Event::keyPressed(KeyId::LEFT, NONE, 0));
    view.selectParam(1);
    check(registry.target(0).address.patternIndex == 7,
          "the address slot follows the UI's selected pattern");

    // UP/DOWN on the Scope field toggles per-pattern <-> global.
    view.selectField(ModulationPatternView::Field::Scope);
    view.handleEvent(state, Event::keyPressed(KeyId::UP, NONE, 0));
    check(view.targetScope() == sequencer::ParameterScope::Global,
          "UP toggles the scope to global");

    // ENTER on the Duration field toggles the beat/bar unit.
    view.selectField(ModulationPatternView::Field::Duration);
    check(view.targetStepDuration().unit == sequencer::StepUnit::Beat,
          "the factory duration unit is beat");
    view.handleEvent(state, Event::keyPressed(KeyId::ENTER, NONE, 0));
    check(view.targetStepDuration().unit == sequencer::StepUnit::Bar,
          "ENTER toggles the duration unit to bar");

    // The render path is exercised on EVERY field (with a target selected,
    // a global scope and a rejection latched).
    view.addTarget();  // fills to two targets; keep the latch clear
    for (int f = 0; f <= 10; ++f) {
        check(view.selectField(static_cast<ModulationPatternView::Field>(f)),
              "field ordinal selectable");
        view.render(state);
    }
    view.onEnter();
    check(!view.lastActionRejected(), "a successful claim keeps the latch clear");

    // cycleField wraps in both directions.
    view.selectField(ModulationPatternView::Field::Pattern);
    view.cycleField(-1);
    check(view.selectedField() == ModulationPatternView::Field::Order,
          "cycleField wraps backwards");

    // The edited global pattern cycles with wrap.
    check(view.selectEditedPattern(static_cast<uint8_t>(ModulationRegistry::MAX_PATTERNS - 1)),
          "the last pattern is selectable");
    view.cycleEditedPattern(+1);
    check(view.editedPattern() == 0, "the pattern selection wraps");
    check(!view.selectEditedPattern(static_cast<uint8_t>(ModulationRegistry::MAX_PATTERNS)),
          "an out-of-pool pattern is refused");
}

}  // namespace

int main() {
    // The bus is metadata-only for this view (paramInfo), but bind it to live
    // state anyway so the fixture mirrors the firmware wiring.
    check(bus.bind(&timeline, &working), "the bus binds to live state");

    test_length_clamped_to_1_16();
    test_only_in_length_steps_editable_or_highlighted();
    test_shorten_relength_restores_values();
    test_step_values_clamped_to_0_127();
    test_param_picker_lists_every_bus_parameter();
    test_scope_default_per_pattern_toggles_global();
    test_scope_toggle_repoints_mismatched_param();
    test_config_edits_write_the_modulation_target();
    test_target_slot_lifecycle();
    test_key_frontend_and_render();

    if (failures == 0) {
        std::printf("test_modulation_pattern_view: all checks passed\n");
        return 0;
    }
    std::printf("test_modulation_pattern_view: %d check(s) failed\n", failures);
    return 1;
}
