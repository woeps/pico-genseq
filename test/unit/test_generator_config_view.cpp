// generative-engine-expansion task 18 — Generator UI View (FR1, AC8).
//
// Validates the capability-driven generator configuration view:
//   - generator selection per role, enumerated from the GeneratorRegistry
//     FILTERED by the role's capability bit (a gate generator never shows
//     up as a pitch option and vice versa);
//   - the control list is built ONLY from the selected generator's
//     GeneratorParamDefs (AC8: the UI shows just the controls the
//     generator actually uses — with Euclidean selected for the gate role
//     exactly steps/pulses/rotation appear, with the probability generator
//     exactly the probability control, with the scale-aware pitch
//     generator the low/high note controls, with the duration generator
//     the four duration controls);
//   - a test-only stub generator with custom ParameterDefs renders its
//     params WITHOUT any view code changes (the view never names a
//     concrete generator type — the plugin contract alone drives it);
//   - edits write through the ParameterBus BASE values: the stored live
//     sequencer state (the per-pattern generative working state) changes
//     and no modulation overlay is armed;
//   - every edit is clamped to the ParameterDef's min/max range.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_generator_config_view".
//
// The view's render path targets the real LedMatrix driver (Pico SDK PIO),
// so — like the reducer property test's commands:: seams — this TU supplies
// host-safe no-op definitions of the LedMatrix member functions instead of
// compiling the SDK-dependent LedMatrix.cpp.

#include "sequencer/GeneratorCapabilities.h"
#include "sequencer/GeneratorRegistry.h"
#include "sequencer/IGenerator.h"
#include "sequencer/ParameterAddress.h"
#include "sequencer/ParameterBus.h"
#include "sequencer/PatternGenerativeState.h"
#include "sequencer/TransportTimeline.h"
#include "sequencer/WorkingStateManager.h"
#include "sequencer/generators/EuclideanGateGenerator.h"
#include "sequencer/generators/NoteDurationGenerator.h"
#include "sequencer/generators/ProbabilityGateGenerator.h"
#include "sequencer/generators/ScaleAwarePitchGenerator.h"
#include "ui/Event.h"
#include "ui/hardware/LedMatrix.h"
#include "ui/state/UIState.h"
#include "ui/views/GeneratorConfigView.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using sequencer::GeneratorCapabilities;
using sequencer::GeneratorCapability;
using sequencer::GeneratorParamHint;
using sequencer::GeneratorRegistry;
using sequencer::GeneratorRole;
using sequencer::IGenerator;
using sequencer::ParameterAddress;
using sequencer::ParameterBus;
using sequencer::ParameterScope;
using sequencer::ParamId;
using sequencer::PatternGenerativeState;
using sequencer::TransportTimeline;
using sequencer::WorkingStateManager;
using ui::GeneratorConfigView;
using ui::events::Event;

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
GeneratorRegistry registry;
hardware::LedMatrix matrix{16};

// A fresh view over the shared fixture, with the four built-in plugins
// registered (RegisterGenerators.cpp is the ONLY built-in listing, AC1).
GeneratorConfigView makeBuiltInView() {
    sequencer::registerGenerators(registry);
    return GeneratorConfigView(registry, bus, matrix);
}

// Index of `name` in the view's control list, or -1 when absent.
int findControl(const GeneratorConfigView& view, const char* name) {
    for (uint8_t i = 0; i < view.controlCount(); ++i) {
        GeneratorConfigView::Control control;
        if (!view.controlAt(i, control)) return -1;
        if (std::strcmp(control.name, name) == 0) return static_cast<int>(i);
    }
    return -1;
}

// Index (within the role's capability-filtered list) of the generator whose
// id matches, or -1 when absent.
int findGeneratorIndex(const GeneratorConfigView& view, GeneratorRole role,
                       const char* id) {
    const uint8_t count = view.generatorCountForRole(role);
    for (uint8_t i = 0; i < count; ++i) {
        const IGenerator* generator = view.generatorForRole(role, i);
        if (generator != nullptr && std::strcmp(generator->id(), id) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// One per-pattern gate-steps address.
ParameterAddress stepsAddress(uint8_t pattern) {
    ParameterAddress address;
    address.scope = ParameterScope::Pattern;
    address.patternIndex = pattern;
    address.paramId = ParamId::GateSteps;
    return address;
}

// --- 1. Euclidean selected: only gate role controls, steps/pulses/rotation ----

void test_euclidean_gate_controls() {
    GeneratorConfigView view = makeBuiltInView();

    // Capability-driven generator enumeration for the GATE role: exactly the
    // two GATE-capable built-ins, never the pitch or duration plugins.
    check(view.generatorCountForRole(GeneratorRole::GATE) == 2,
          "the gate role offers exactly the two gate-capable generators");
    check(findGeneratorIndex(view, GeneratorRole::GATE, "builtin.euclidean") == 0,
          "the Euclidean plugin is the first gate option");
    check(findGeneratorIndex(view, GeneratorRole::GATE, "builtin.probability") == 1,
          "the probability plugin is the second gate option");
    check(findGeneratorIndex(view, GeneratorRole::GATE, "builtin.pitch") == -1,
          "the pitch plugin never appears as a gate option");
    check(findGeneratorIndex(view, GeneratorRole::GATE, "builtin.duration") == -1,
          "the duration plugin never appears as a gate option");

    view.selectRole(GeneratorRole::GATE);
    check(view.selectedRole() == GeneratorRole::GATE, "selectRole selects the role");
    const int euclidean = findGeneratorIndex(view, GeneratorRole::GATE, "builtin.euclidean");
    check(view.selectGeneratorForRole(GeneratorRole::GATE,
                                      static_cast<uint8_t>(euclidean)),
          "selecting the Euclidean generator succeeds");
    check(std::strcmp(view.selectedGeneratorId(GeneratorRole::GATE),
                      "builtin.euclidean") == 0,
          "the gate role remembers the selected generator id");

    // AC8: exactly the generator's three ParameterDefs appear — steps,
    // pulses, rotation — and nothing else (no pitch/duration controls).
    check(view.controlCount() == 3,
          "the Euclidean generator shows exactly its three controls");

    GeneratorConfigView::Control control;
    check(view.controlAt(0, control) && std::strcmp(control.name, "Steps") == 0 &&
              control.minValue == 1 && control.maxValue == 64 &&
              control.defaultValue == 16 &&
              control.hint == GeneratorParamHint::Steps && control.editable,
          "control 0 is the editable Steps parameter (1-64, default 16)");
    check(view.controlAt(1, control) && std::strcmp(control.name, "Pulses") == 0 &&
              control.minValue == 0 && control.maxValue == 64 &&
              control.defaultValue == 4 &&
              control.hint == GeneratorParamHint::Steps && control.editable,
          "control 1 is the editable Pulses parameter (0-64, default 4)");
    check(view.controlAt(2, control) && std::strcmp(control.name, "Rotation") == 0 &&
              control.minValue == 0 && control.maxValue == 63 &&
              control.defaultValue == 0 &&
              control.hint == GeneratorParamHint::None && control.editable,
          "control 2 is the editable Rotation parameter (0-63, default 0)");
    check(findControl(view, "Low Note") == -1 && findControl(view, "Base Length") == -1,
          "no pitch or duration controls appear for the gate role");
}

// --- 2. Scale-aware pitch generator selected: pitch controls -------------------

void test_scale_pitch_controls() {
    GeneratorConfigView view = makeBuiltInView();

    check(view.generatorCountForRole(GeneratorRole::PITCH) == 1,
          "the pitch role offers exactly the one pitch-capable built-in");
    view.selectRole(GeneratorRole::PITCH);
    const int pitch = findGeneratorIndex(view, GeneratorRole::PITCH, "builtin.pitch");
    check(view.selectGeneratorForRole(GeneratorRole::PITCH,
                                      static_cast<uint8_t>(pitch)),
          "selecting the scale-aware pitch generator succeeds");

    check(view.controlCount() == 2,
          "the pitch generator shows exactly its two controls");
    GeneratorConfigView::Control control;
    check(view.controlAt(0, control) && std::strcmp(control.name, "Low Note") == 0 &&
              control.minValue == 0 && control.maxValue == 127 &&
              control.defaultValue == 48 &&
              control.hint == GeneratorParamHint::Note && control.editable,
          "control 0 is the editable Low Note parameter (0-127, default 48)");
    check(view.controlAt(1, control) && std::strcmp(control.name, "High Note") == 0 &&
              control.minValue == 0 && control.maxValue == 127 &&
              control.defaultValue == 72 &&
              control.hint == GeneratorParamHint::Note && control.editable,
          "control 1 is the editable High Note parameter (0-127, default 72)");
    check(findControl(view, "Steps") == -1,
          "no gate controls appear for the pitch role");

    // Role isolation: switching back to the gate role restores the gate
    // generator's controls, and the per-role selections are independent.
    view.selectRole(GeneratorRole::GATE);
    check(view.controlCount() == 3,
          "switching back to the gate role shows the gate controls again");
    check(std::strcmp(view.selectedGeneratorId(GeneratorRole::PITCH),
                      "builtin.pitch") == 0,
          "the pitch role keeps its own selection while another role is shown");

    // No velocity-capable built-in exists, so the velocity role offers no
    // generator and no controls (capability-driven, not name-driven).
    view.selectRole(GeneratorRole::VELOCITY);
    check(view.generatorCountForRole(GeneratorRole::VELOCITY) == 0,
          "the velocity role offers no built-in generator");
    check(view.controlCount() == 0,
          "a role without a generator shows no controls");
}

// --- 3. Probability generator selected: only the probability control ----------

void test_probability_controls() {
    GeneratorConfigView view = makeBuiltInView();

    view.selectRole(GeneratorRole::GATE);
    const int probability =
        findGeneratorIndex(view, GeneratorRole::GATE, "builtin.probability");
    check(view.selectGeneratorForRole(GeneratorRole::GATE,
                                      static_cast<uint8_t>(probability)),
          "selecting the probability generator succeeds");

    // AC8 taken literally: the probability generator declares ONE parameter,
    // so exactly ONE control appears — not the union of every gate
    // generator's controls.
    check(view.controlCount() == 1,
          "the probability generator shows exactly its one control");
    GeneratorConfigView::Control control;
    check(view.controlAt(0, control) && std::strcmp(control.name, "Probability") == 0 &&
              control.minValue == 0 && control.maxValue == 127 &&
              control.defaultValue == 64 &&
              control.hint == GeneratorParamHint::Percent && control.editable,
          "the single control is the editable Probability parameter (0-127)");
    check(findControl(view, "Steps") == -1 && findControl(view, "Rotation") == -1,
          "no Euclidean controls appear while the probability generator is selected");
}

// --- 4. Duration generator selected: only duration controls -------------------

void test_duration_controls() {
    GeneratorConfigView view = makeBuiltInView();

    check(view.generatorCountForRole(GeneratorRole::DURATION) == 1,
          "the duration role offers exactly the one duration-capable built-in");
    view.selectRole(GeneratorRole::DURATION);
    const int duration =
        findGeneratorIndex(view, GeneratorRole::DURATION, "builtin.duration");
    check(view.selectGeneratorForRole(GeneratorRole::DURATION,
                                      static_cast<uint8_t>(duration)),
          "selecting the duration generator succeeds");

    check(view.controlCount() == 4,
          "the duration generator shows exactly its four controls");
    GeneratorConfigView::Control control;
    check(view.controlAt(0, control) && std::strcmp(control.name, "Base Length") == 0 &&
              control.minValue == 1 && control.maxValue == 255 &&
              control.defaultValue == 24 && control.editable,
          "control 0 is the editable Base Length parameter (1-255, default 24)");
    check(view.controlAt(1, control) && std::strcmp(control.name, "Min Length") == 0 &&
              control.minValue == 1 && control.maxValue == 255 &&
              control.defaultValue == 1 && control.editable,
          "control 1 is the editable Min Length parameter (1-255, default 1)");
    check(view.controlAt(2, control) && std::strcmp(control.name, "Max Length") == 0 &&
              control.minValue == 1 && control.maxValue == 255 &&
              control.defaultValue == 48 && control.editable,
          "control 2 is the editable Max Length parameter (1-255, default 48)");
    check(view.controlAt(3, control) && std::strcmp(control.name, "Variation") == 0 &&
              control.minValue == 0 && control.maxValue == 127 &&
              control.defaultValue == 0 &&
              control.hint == GeneratorParamHint::Percent && control.editable,
          "control 3 is the editable Variation parameter (0-127, default 0)");
    check(findControl(view, "Steps") == -1 && findControl(view, "Low Note") == -1,
          "no gate or pitch controls appear for the duration role");
}

// --- 5. A test-only stub generator renders its custom ParameterDefs -----------

// A stub plugin with parameters NO built-in has: proves the view builds its
// control list purely from the IGenerator contract (AC8) — no view code
// change is needed to render a new plugin's controls.
class StubVelocityGenerator final : public IGenerator {
public:
    static constexpr uint8_t kParamDrive = 0;
    static constexpr uint8_t kParamAccent = 1;

    const char* id() const override { return "stub.velocity"; }
    const char* name() const override { return "Stub Velocity"; }
    GeneratorCapabilities capabilities() const override {
        return GeneratorCapability::VELOCITY;
    }
    const sequencer::GeneratorParamSet& paramDefs() const override { return defs_; }
    void generate(uint32_t, const sequencer::GeneratorContext&,
                  sequencer::GeneratorOutput&) const override {
        // The view never invokes generate(); the stub stays inert.
    }

private:
    sequencer::GeneratorParamSet defs_ = [] {
        sequencer::GeneratorParamSet set;
        set.defs[0] = {kParamDrive, "Drive", 3, 9, 5,
                       GeneratorParamHint::Percent};
        set.defs[1] = {kParamAccent, "Accent", -4, 12, 0,
                       GeneratorParamHint::None};
        set.count = 2;
        return set;
    }();
};

// Whether the control at `index` exists and is editable (has a bus address).
bool controlAtEditable(const GeneratorConfigView& view, uint8_t index) {
    GeneratorConfigView::Control control;
    return view.controlAt(index, control) && control.editable;
}

void test_stub_renders_custom_defs() {
    // A local registry holding ONLY the stub: the view must render its
    // custom ParameterDefs without any knowledge of the type.
    GeneratorRegistry stubRegistry;
    StubVelocityGenerator stub;
    check(stubRegistry.add(&stub), "the stub registers in a local registry");

    GeneratorConfigView view(stubRegistry, bus, matrix);
    view.selectRole(GeneratorRole::VELOCITY);

    check(view.generatorCountForRole(GeneratorRole::VELOCITY) == 1,
          "the stub appears for the role its capability declares");
    check(view.selectGeneratorForRole(GeneratorRole::VELOCITY, 0),
          "selecting the stub succeeds");

    check(view.controlCount() == 2,
          "the stub's two custom controls render");
    GeneratorConfigView::Control control;
    check(view.controlAt(0, control) && std::strcmp(control.name, "Drive") == 0 &&
              control.minValue == 3 && control.maxValue == 9 &&
              control.defaultValue == 5 &&
              control.hint == GeneratorParamHint::Percent,
          "the stub's custom Drive parameter renders verbatim");
    check(view.controlAt(1, control) && std::strcmp(control.name, "Accent") == 0 &&
              control.minValue == -4 && control.maxValue == 12 &&
              control.defaultValue == 0,
          "the stub's custom Accent parameter renders verbatim (negative min)");
    check(!view.controlAt(2, control),
          "no control exists beyond the stub's parameter count");

    // The stub's parameters have no address in the fixed task-16 parameter
    // table, so they render (read-only display shows the factory default)
    // but refuse edits — the edit is rejected, nothing is written.
    check(!controlAtEditable(view, 0),
          "the stub's custom parameter has no bus address (not editable)");
    check(view.controlValue(0, 0) == 5,
          "an unaddressed control displays its factory default");
    check(!view.setControlValue(0, 0, 7),
          "editing an unaddressed control is refused");
}

// --- 6. Edits write through the ParameterBus base values ----------------------

void test_edits_write_through_bus_base() {
    GeneratorConfigView view = makeBuiltInView();

    // Bind the bus to live sequencer state (the sequencer's own wiring).
    check(bus.bind(&timeline, &working), "the bus binds to live state");

    view.selectRole(GeneratorRole::GATE);
    check(view.selectGeneratorForRole(
              GeneratorRole::GATE,
              static_cast<uint8_t>(findGeneratorIndex(view, GeneratorRole::GATE,
                                                     "builtin.euclidean"))),
          "the Euclidean generator is selected for editing");
    const int steps = findControl(view, "Steps");
    check(steps == 0, "the Steps control is found");

    // A committed edit lands in the bus BASE value and the backing working
    // state — never an overlay.
    check(view.setControlValue(0, static_cast<uint8_t>(steps), 24),
          "setting Steps to 24 succeeds");
    check(bus.baseValue(stepsAddress(0)) == 24,
          "the edit writes the bus base value");
    check(bus.effectiveValue(stepsAddress(0)) == 24,
          "the effective value follows the base");
    check(working.working(0) != nullptr &&
              working.working(0)->generatorParam(
                  GeneratorRole::GATE, sequencer::EuclideanGateGenerator::kParamSteps) == 24,
          "the edit reaches the per-pattern generative working state");
    check(!bus.hasOverlay(stepsAddress(0)),
          "a config edit never arms a modulation overlay");

    // Per-pattern addressing: editing pattern 3 leaves pattern 0 alone.
    check(view.setControlValue(3, static_cast<uint8_t>(steps), 32),
          "setting Steps on pattern 3 succeeds");
    check(bus.baseValue(stepsAddress(3)) == 32,
          "pattern 3 carries its own edited base value");
    check(bus.baseValue(stepsAddress(0)) == 24,
          "pattern 0 is untouched by the pattern-3 edit");

    // The same write path through the key-event front end: UP on the
    // selected pattern adjusts the selected control by +1 (+5 with SHIFT).
    check(view.selectControl(static_cast<uint8_t>(steps)), "the control is selected");
    ui::state::UIState state;
    state.selectedPattern = 5;
    check(bus.setBase(stepsAddress(5), 16), "pattern 5 starts at steps 16");
    Event up = Event::keyPressed(ui::KeyId::UP, ui::mod::NONE, 0);
    view.handleEvent(state, up);
    check(bus.baseValue(stepsAddress(5)) == 17,
          "an UP key press adjusts the selected control through the bus (+1)");
    Event upCoarse = Event::keyPressed(ui::KeyId::UP, ui::mod::SHIFT, 0);
    view.handleEvent(state, upCoarse);
    check(bus.baseValue(stepsAddress(5)) == 22,
          "SHIFT+UP adjusts the selected control by the coarse step (+5)");
    Event down = Event::keyPressed(ui::KeyId::DOWN, ui::mod::NONE, 0);
    view.handleEvent(state, down);
    check(bus.baseValue(stepsAddress(5)) == 21,
          "a DOWN key press adjusts the selected control through the bus (-1)");
}

// --- 7. Edits are clamped to the ParameterDef min/max -------------------------

void test_clamping() {
    GeneratorConfigView view = makeBuiltInView();
    bus.bind(&timeline, &working);

    view.selectRole(GeneratorRole::GATE);
    check(view.selectGeneratorForRole(
              GeneratorRole::GATE,
              static_cast<uint8_t>(findGeneratorIndex(view, GeneratorRole::GATE,
                                                     "builtin.euclidean"))),
          "the Euclidean generator is selected for clamping checks");
    const int steps = findControl(view, "Steps");
    const int pulses = findControl(view, "Pulses");
    const int rotation = findControl(view, "Rotation");

    // An over-range write clamps to the ParameterDef max, an under-range
    // write to the min — the write still succeeds with the clamped value.
    check(view.setControlValue(0, static_cast<uint8_t>(steps), 999),
          "an over-range set is accepted (and clamped)");
    check(bus.baseValue(stepsAddress(0)) == 64,
          "an over-range set clamps to the def max (64)");
    check(view.setControlValue(0, static_cast<uint8_t>(steps), -50),
          "an under-range set is accepted (and clamped)");
    check(bus.baseValue(stepsAddress(0)) == 1,
          "an under-range set clamps to the def min (1)");

    // A relative adjust clamps at both ends of the range.
    check(view.setControlValue(1, static_cast<uint8_t>(pulses), 60),
          "pattern 1's pulses start at 60");
    check(view.adjustControl(1, static_cast<uint8_t>(pulses), +100),
          "a huge upward adjust is accepted (and clamped)");
    ParameterAddress pulsesAddress;
    pulsesAddress.scope = ParameterScope::Pattern;
    pulsesAddress.patternIndex = 1;
    pulsesAddress.paramId = ParamId::GatePulses;
    check(bus.baseValue(pulsesAddress) == 64,
          "the adjust clamps at the def max (64)");
    check(view.adjustControl(1, static_cast<uint8_t>(pulses), -1000),
          "a huge downward adjust is accepted (and clamped)");
    check(bus.baseValue(pulsesAddress) == 0,
          "the adjust clamps at the def min (0)");
    check(bus.baseValue(stepsAddress(0)) == 1,
          "the pattern-1 clamps never touched pattern 0's steps");
    check(view.adjustControl(0, static_cast<uint8_t>(rotation), -1),
          "adjusting below an at-min control succeeds (no further motion)");
    ParameterAddress rotationAddress;
    rotationAddress.scope = ParameterScope::Pattern;
    rotationAddress.patternIndex = 0;
    rotationAddress.paramId = ParamId::GateRotation;
    check(bus.baseValue(rotationAddress) == 0,
          "an at-min control stays at its min under a downward adjust");
}

}  // namespace

int main() {
    test_euclidean_gate_controls();
    test_scale_pitch_controls();
    test_probability_controls();
    test_duration_controls();
    test_stub_renders_custom_defs();
    test_edits_write_through_bus_base();
    test_clamping();

    if (failures == 0) {
        std::printf("test_generator_config_view: all checks passed\n");
        return 0;
    }
    std::printf("test_generator_config_view: %d check(s) failed\n", failures);
    return 1;
}
