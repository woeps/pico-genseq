// generative-engine-expansion task 18 — Generator UI View (FR1, AC8);
// ui-integration T5 — command-based writes.
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
//   - edits ride the COMMAND QUEUE (ui-integration T5): every committed
//     edit posts ONE GEN_PARAM_SET command carrying the (pattern, paramId,
//     clamped value) payload — verified through a RECORDING host double of
//     commands::sendGenParamCommand. The view holds NO ParameterBus
//     reference; reads come from its local UI cache (seeded with the
//     task-16 factory defaults, updated by every write) and never from the
//     sequencer's state, honoring the no-shared-state-between-cores
//     constraint;
//   - every edit is clamped to the ParameterDef's min/max range before it
//     is cached and sent;
//   - ESCAPE returns the UIState to the GATE_SET parent view, and held
//     repeats are ignored;
//   - the sequencer-side GEN_PARAM_SET handler (Sequencer::processCommand)
//     applies a decoded message as a plain ParameterBus BASE write into the
//     live per-pattern working state — clamped by the bus, never an overlay
//     — and refuses out-of-range pattern slots and non-generator ids.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_generator_config_view".
//
// The view's render path targets the real LedMatrix driver (Pico SDK PIO),
// and command.cpp (the multicore FIFO) is not compiled host-side, so — like
// the reducer property test's commands:: seams — this TU supplies host-safe
// definitions: no-op LedMatrix member functions and a RECORDING
// commands::sendGenParamCommand double.

#include "commands/command.h"
#include "sequencer/GeneratorCapabilities.h"
#include "sequencer/GeneratorRegistry.h"
#include "sequencer/IGenerator.h"
#include "sequencer/ParameterAddress.h"
#include "sequencer/PatternSnapshot.h"
#include "sequencer.h"
#include "sequencer/generators/EuclideanGateGenerator.h"
#include "sequencer/generators/NoteDurationGenerator.h"
#include "sequencer/generators/ProbabilityGateGenerator.h"
#include "sequencer/generators/ScaleAwarePitchGenerator.h"
#include "ui/Event.h"
#include "ui/hardware/LedMatrix.h"
#include "ui/state/UIState.h"
#include "ui/views/GeneratorConfigView.h"
#include "FakeMonotonicClock.h"
#include "RecordingMidiOutput.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using sequencer::GeneratorCapabilities;
using sequencer::GeneratorCapability;
using sequencer::GeneratorParamHint;
using sequencer::GeneratorRegistry;
using sequencer::GeneratorRole;
using sequencer::IGenerator;
using sequencer::ParameterAddress;
using sequencer::ParameterScope;
using sequencer::ParamId;
using sequencer::PatternSnapshot;
using sequencer::Sequencer;
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

// --- host-safe commands:: seam (RECORDING double) -------------------------------
// GeneratorConfigView.cpp posts its writes through
// commands::sendGenParamCommand; command.cpp (the multicore FIFO) is not
// compiled host-side, so this TU links against this recording definition
// instead. Every posted GEN_PARAM_SET lands in the vector below, and the
// tests assert on the exact (pattern, paramId, value) payloads.

namespace commands {

std::vector<std::vector<int>> g_genParamCalls;  // {patternIndex, paramId, value}

void sendGenParamCommand(uint8_t patternIndex, uint8_t paramId, int16_t value) {
    g_genParamCalls.push_back({patternIndex, paramId, value});
}

}  // namespace commands

namespace {

// --- shared fixture -----------------------------------------------------------

GeneratorRegistry registry;
hardware::LedMatrix matrix{16};

// A fresh view over the shared registry, with the four built-in plugins
// registered (RegisterGenerators.cpp is the ONLY built-in listing, AC1).
GeneratorConfigView makeBuiltInView() {
    sequencer::registerGenerators(registry);
    return GeneratorConfigView(registry, matrix);
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

// The last posted GEN_PARAM_SET payload, as {patternIndex, paramId, value}.
std::vector<int> lastGenParamCall() {
    if (commands::g_genParamCalls.empty()) return {};
    return commands::g_genParamCalls.back();
}

bool lastGenParamWas(uint8_t patternIndex, uint8_t paramId, int16_t value) {
    const std::vector<int> call = lastGenParamCall();
    return call.size() == 3 && call[0] == patternIndex && call[1] == paramId &&
           call[2] == value;
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

// Whether the control at `index` exists and is editable (has a table address).
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

    GeneratorConfigView view(stubRegistry, matrix);
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
    // but refuse edits — the edit is rejected and NOTHING is posted.
    check(!controlAtEditable(view, 0),
          "the stub's custom parameter has no table address (not editable)");
    check(view.controlValue(0, 0) == 5,
          "an unaddressed control displays its factory default");
    const size_t postedBefore = commands::g_genParamCalls.size();
    check(!view.setControlValue(0, 0, 7),
          "editing an unaddressed control is refused");
    check(commands::g_genParamCalls.size() == postedBefore,
          "a refused edit posts no GEN_PARAM_SET command");
}

// --- 6. Edits post GEN_PARAM_SET commands and update the local cache ----------

void test_edits_post_commands() {
    commands::g_genParamCalls.clear();
    GeneratorConfigView view = makeBuiltInView();

    view.selectRole(GeneratorRole::GATE);
    check(view.selectGeneratorForRole(
              GeneratorRole::GATE,
              static_cast<uint8_t>(findGeneratorIndex(view, GeneratorRole::GATE,
                                                     "builtin.euclidean"))),
          "the Euclidean generator is selected for editing");
    const int steps = findControl(view, "Steps");
    check(steps == 0, "the Steps control is found");

    // The local UI cache starts at the task-16 factory default (16), with
    // no sequencer involved — Core0 reads only its own mirror.
    check(view.controlValue(0, static_cast<uint8_t>(steps)) == 16,
          "an untouched control reads the factory default from the UI cache");

    // A committed edit posts exactly ONE GEN_PARAM_SET with the clamped
    // value, and updates the cache the same instant.
    check(view.setControlValue(0, static_cast<uint8_t>(steps), 24),
          "setting Steps to 24 succeeds");
    check(commands::g_genParamCalls.size() == 1,
          "the edit posted exactly one GEN_PARAM_SET command");
    check(lastGenParamWas(0, static_cast<uint8_t>(ParamId::GateSteps), 24),
          "the command carries (pattern 0, GateSteps, 24)");
    check(view.controlValue(0, static_cast<uint8_t>(steps)) == 24,
          "the cache reflects the new value without any sequencer");

    // Per-pattern addressing: editing pattern 3 posts its own command and
    // leaves pattern 0's cached value alone.
    check(view.setControlValue(3, static_cast<uint8_t>(steps), 32),
          "setting Steps on pattern 3 succeeds");
    check(lastGenParamWas(3, static_cast<uint8_t>(ParamId::GateSteps), 32),
          "the pattern-3 command carries (pattern 3, GateSteps, 32)");
    check(view.controlValue(3, static_cast<uint8_t>(steps)) == 32,
          "pattern 3 carries its own cached value");
    check(view.controlValue(0, static_cast<uint8_t>(steps)) == 24,
          "pattern 0 is untouched by the pattern-3 edit");

    // An out-of-range pattern slot is refused (nothing cached, nothing sent).
    const size_t posted = commands::g_genParamCalls.size();
    check(!view.setControlValue(200, static_cast<uint8_t>(steps), 8),
          "an out-of-range pattern slot is refused");
    check(commands::g_genParamCalls.size() == posted,
          "the refused write posted no command");

    // The same write path through the key-event front end: UP on the
    // selected pattern adjusts the selected control by +1 (+5 with SHIFT),
    // each press posting its own command computed from the CACHE.
    check(view.selectControl(static_cast<uint8_t>(steps)), "the control is selected");
    ui::state::UIState state;
    state.selectedPattern = 5;
    Event up = Event::keyPressed(ui::KeyId::UP, ui::mod::NONE, 0);
    view.handleEvent(state, up);
    check(lastGenParamWas(5, static_cast<uint8_t>(ParamId::GateSteps), 17),
          "an UP key press posts a +1 adjust from the cached default (+1)");
    Event upCoarse = Event::keyPressed(ui::KeyId::UP, ui::mod::SHIFT, 0);
    view.handleEvent(state, upCoarse);
    check(lastGenParamWas(5, static_cast<uint8_t>(ParamId::GateSteps), 22),
          "SHIFT+UP posts a +5 adjust from the cached value");
    Event down = Event::keyPressed(ui::KeyId::DOWN, ui::mod::NONE, 0);
    view.handleEvent(state, down);
    check(lastGenParamWas(5, static_cast<uint8_t>(ParamId::GateSteps), 21),
          "a DOWN key press posts a -1 adjust from the cached value");
    check(view.controlValue(5, static_cast<uint8_t>(steps)) == 21,
          "the cache followed the key-driven adjustments");
}

// --- 7. Edits are clamped to the ParameterDef min/max -------------------------

void test_clamping() {
    commands::g_genParamCalls.clear();
    GeneratorConfigView view = makeBuiltInView();

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
    // write to the min — the write still succeeds, and the POSTED value is
    // the clamped one.
    check(view.setControlValue(0, static_cast<uint8_t>(steps), 999),
          "an over-range set is accepted (and clamped)");
    check(lastGenParamWas(0, static_cast<uint8_t>(ParamId::GateSteps), 64),
          "the posted command clamps to the def max (64)");
    check(view.controlValue(0, static_cast<uint8_t>(steps)) == 64,
          "the cache holds the clamped value");
    check(view.setControlValue(0, static_cast<uint8_t>(steps), -50),
          "an under-range set is accepted (and clamped)");
    check(lastGenParamWas(0, static_cast<uint8_t>(ParamId::GateSteps), 1),
          "the posted command clamps to the def min (1)");

    // A relative adjust clamps at both ends of the range.
    check(view.setControlValue(1, static_cast<uint8_t>(pulses), 60),
          "pattern 1's pulses start at 60");
    check(view.adjustControl(1, static_cast<uint8_t>(pulses), +100),
          "a huge upward adjust is accepted (and clamped)");
    check(lastGenParamWas(1, static_cast<uint8_t>(ParamId::GatePulses), 64),
          "the posted adjust clamps at the def max (64)");
    check(view.adjustControl(1, static_cast<uint8_t>(pulses), -1000),
          "a huge downward adjust is accepted (and clamped)");
    check(lastGenParamWas(1, static_cast<uint8_t>(ParamId::GatePulses), 0),
          "the posted adjust clamps at the def min (0)");
    check(view.controlValue(0, static_cast<uint8_t>(steps)) == 1,
          "the pattern-1 clamps never touched pattern 0's steps");
    check(view.adjustControl(0, static_cast<uint8_t>(rotation), -1),
          "adjusting below an at-min control succeeds (no further motion)");
    check(lastGenParamWas(0, static_cast<uint8_t>(ParamId::GateRotation), 0),
          "an at-min control posts its own min under a downward adjust");
}

// --- 8. ESCAPE returns to the GATE_SET parent view -----------------------------

void test_escape_returns_to_gate_set() {
    GeneratorConfigView view = makeBuiltInView();

    ui::state::UIState state;
    state.currentView = ui::state::ViewId::GENERATOR_CONFIG;

    // A pressed ESCAPE walks straight back to GATE_SET (FR11).
    Event esc = Event::keyPressed(ui::KeyId::ESCAPE, ui::mod::NONE, 0);
    const ui::state::UIState next = view.handleEvent(state, esc);
    check(next.currentView == ui::state::ViewId::GATE_SET,
          "a pressed ESCAPE returns the state to GATE_SET");
    check(state.selectedPattern == next.selectedPattern,
          "the back-out leaves the rest of the state untouched");

    // A HELD repeat is ignored — the view stays put until the key is
    // actually pressed again.
    Event escHeld = Event::keyHeld(ui::KeyId::ESCAPE, ui::mod::NONE, 0);
    const ui::state::UIState held = view.handleEvent(state, escHeld);
    check(held.currentView == ui::state::ViewId::GENERATOR_CONFIG,
          "a held ESCAPE repeat does not navigate");

    // Every other key leaves the current view unchanged.
    Event up = Event::keyPressed(ui::KeyId::UP, ui::mod::NONE, 0);
    const ui::state::UIState afterUp = view.handleEvent(state, up);
    check(afterUp.currentView == ui::state::ViewId::GENERATOR_CONFIG,
          "an UP press does not navigate away from the view");
}

// --- 9. The sequencer applies GEN_PARAM_SET as a plain bus base write ---------

void test_sequencer_gen_param_set_handler() {
    RecordingMidiOutput out;
    FakeMonotonicClock clock{1000};
    Sequencer seq(out, clock);

    // The live core1 path: a decoded GEN_PARAM_SET message (pattern index,
    // param id, 16-bit value) applied through the allocation-free
    // snapshot-receiving overload.
    commands::CommandMessage msg;
    msg.cmd = commands::Command::GEN_PARAM_SET;
    msg.param1 = 0;
    msg.param2 = static_cast<uint8_t>(ParamId::GateSteps);
    msg.paramValue = 24;
    PatternSnapshot snapshot;
    seq.processCommand(msg, snapshot);

    check(seq.generativeState(0) != nullptr &&
              seq.generativeState(0)->generatorParam(
                  GeneratorRole::GATE,
                  sequencer::EuclideanGateGenerator::kParamSteps) == 24,
          "GEN_PARAM_SET writes the per-pattern generative working state");
    check(seq.parameterBus().baseValue(stepsAddress(0)) == 24,
          "the write is a BASE value (what the UI and undo read)");
    check(!seq.parameterBus().hasOverlay(stepsAddress(0)),
          "GEN_PARAM_SET never arms a modulation overlay");

    // The bus clamps the value into the parameter's table range.
    msg.paramValue = 999;
    seq.processCommand(msg);
    check(seq.parameterBus().baseValue(stepsAddress(0)) == 64,
          "an over-range command clamps to the table max (64)");

    // An out-of-range pattern slot is refused (canApplyCommand) and leaves
    // the working state untouched.
    msg.param1 = 5;  // only one pattern exists
    msg.paramValue = 40;
    seq.processCommand(msg);
    check(seq.parameterBus().baseValue(stepsAddress(0)) == 64,
          "a command for a nonexistent pattern slot is refused");

    // A parameter id outside the generator-parameter span (the pattern-scope
    // scale root) is refused by the handler — GEN_PARAM_SET writes exactly
    // the generator parameters.
    const sequencer::PatternGenerativeState beforeRefusal = *seq.generativeState(0);
    msg.param1 = 0;
    msg.param2 = static_cast<uint8_t>(ParamId::ScaleRoot);
    msg.paramValue = 7;
    seq.processCommand(msg);
    check(seq.generativeState(0) != nullptr &&
              std::memcmp(&beforeRefusal, seq.generativeState(0),
                          sizeof(beforeRefusal)) == 0,
          "a non-generator parameter id is refused untouched");

    // The scalar overload is consistent: adding a pattern opens its slot
    // for a GEN_PARAM_SET write.
    commands::CommandMessage add;
    add.cmd = commands::Command::PATTERN_ADD;
    seq.processCommand(add);
    msg.param1 = 1;
    msg.param2 = static_cast<uint8_t>(ParamId::PitchLowNote);
    msg.paramValue = 55;
    seq.processCommand(msg);
    ParameterAddress lowNote;
    lowNote.scope = ParameterScope::Pattern;
    lowNote.patternIndex = 1;
    lowNote.paramId = ParamId::PitchLowNote;
    check(seq.parameterBus().baseValue(lowNote) == 55,
          "a newly added pattern's slot accepts GEN_PARAM_SET");
    check(seq.parameterBus().baseValue(stepsAddress(0)) == 64,
          "the pattern-1 write never touched pattern 0");
}

}  // namespace

int main() {
    test_euclidean_gate_controls();
    test_scale_pitch_controls();
    test_probability_controls();
    test_duration_controls();
    test_stub_renders_custom_defs();
    test_edits_post_commands();
    test_clamping();
    test_escape_returns_to_gate_set();
    test_sequencer_gen_param_set_handler();

    if (failures == 0) {
        std::printf("test_generator_config_view: all checks passed\n");
        return 0;
    }
    std::printf("test_generator_config_view: %d check(s) failed\n", failures);
    return 1;
}
