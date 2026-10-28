// ui-integration task T14 — Settings global scale (FR4, FR12-FR14, AC7/AC10).
//
// Validates the combined Scale field on the global SettingsView and the
// Reducer::setScale state setter:
//   - ENTER cycles the edited field (bPM <-> Scale); held repeats are ignored;
//   - on the Scale field, UP/DOWN adjust the global scaleRoot with WRAP
//     across the 12 pitch classes 0-11 (SHIFT: coarse step 5) and LEFT/RIGHT
//     adjust the global scaleTemplateIndex with WRAP across ALL built-in
//     templates (SHIFT: coarse step 5);
//   - every change dispatches the SCALE_SET command through the Reducer:
//     state::setScale -> commands::sendScaleCommand, the SCALE_SET dispatch
//     (single FIFO word, exactly like BPM_SET) — verified through a RECORDING
//     host double of commands::sendScaleCommand. The view holds NO
//     ParameterBus reference and reads/writes only the UIState mirror
//     (state.scaleRoot / state.scaleTemplateIndex), honoring the
//     no-shared-state-between-cores constraint (FR17);
//   - rendering: the root is drawn as a NOTE NAME (C..B, sharps — the same
//     12-note spelling as the sibling views) on the matrix's note region and
//     the template as its NAME from common::ScaleTemplates (verbatim via
//     scaleNameAt, condensed to the 3-char matrix label slot via
//     scaleNameLabel) — verified through a RECORDING LedMatrix double;
//   - Reducer::setScale stores the pair (root WRAPS as a cyclic pitch class,
//     template CLAMPS to the fixed table) and posts exactly ONE
//     sendScaleCommand with the exact (root, template) payload;
//   - regression: the BPM field still adjusts BPM by 1 (SHIFT: 10) through
//     BPM_SET, LEFT/RIGHT stay unbound on the BPM field, and the BPM render
//     still draws the "bPM" label + number.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_settings_view".
//
// The view's render path targets the real LedMatrix driver (Pico SDK PIO)
// and SettingsView.cpp + Reducer.cpp reference the commands:: FIFO seam
// (command.cpp is not compiled host-side), so — like
// test_scale_selection_view — this TU supplies host-safe definitions: a
// RECORDING LedMatrix member double, a no-op Led double, a RECORDING
// commands::sendScaleCommand double, a RECORDING commands::sendCommand double
// and no-op sendGateSet / sendPitchSet / sendVelocitySet doubles.

#include "commands/command.h"
#include "common/ScaleTemplates.h"
#include "ui/Event.h"
#include "ui/Types.h"
#include "ui/hardware/Led.h"
#include "ui/hardware/LedMatrix.h"
#include "ui/state/Reducer.h"
#include "ui/state/UIState.h"
#include "ui/views/SettingsView.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using ui::SettingsView;
using ui::events::Event;
using ui::state::UIState;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

}  // namespace

// --- host-safe LedMatrix seam (RECORDING double) --------------------------------
// Defined in the REAL hardware namespace (outside the anonymous one) so they
// replace the SDK-dependent LedMatrix.cpp definitions at link time. Every
// draw call lands in the log below so the tests can assert what render()
// put on the matrix (label / note / number text).

namespace hardware {

std::vector<std::string> g_matrixDraws;

LedMatrix::LedMatrix(uint8_t pin) : pin(pin), buffer{}, dirty(false) {}
void LedMatrix::update() {}
void LedMatrix::clear() { dirty = true; }
void LedMatrix::setPixel(uint8_t, uint8_t, uint32_t) { dirty = true; }
void LedMatrix::fill(uint32_t) { dirty = true; }
void LedMatrix::drawNumber(int number, uint32_t) {
    dirty = true;
    g_matrixDraws.push_back("num:" + std::to_string(number));
}
void LedMatrix::drawLabel(const char (&text)[4], uint32_t) {
    dirty = true;
    g_matrixDraws.push_back(std::string("label:") + text[0] + text[1] + text[2]);
}
void LedMatrix::drawNote(const char (&noteStr)[4], uint32_t) {
    dirty = true;
    g_matrixDraws.push_back(std::string("note:") + noteStr[0] + noteStr[1] + noteStr[2]);
}

}  // namespace hardware

// --- host-safe Led seam (no-op double) -------------------------------------------
// The view stores the reference; the firmware-only GPIO driver (Led.cpp) is
// not compiled host-side.

namespace hardware {

Led::Led(uint8_t pin)
    : pin(pin), state(false), blinking(false),
      onTime(0), offTime(0), lastToggleTime(0) {}
void Led::update() {}
void Led::on() {}
void Led::off() {}
void Led::toggle() {}
void Led::blink(uint32_t, uint32_t) {}

}  // namespace hardware

// --- host-safe commands:: seam (RECORDING doubles) --------------------------------
// SettingsView.cpp + Reducer.cpp post their writes through the commands::
// FIFO seam; command.cpp (the multicore FIFO) is not compiled host-side, so
// this TU links against these definitions instead. sendScaleCommand is the
// SCALE_SET dispatch (command.h: "Send the global scale ... to the sequencer
// core"), so every recorded call is one SCALE_SET with its (root, template)
// payload. sendCommand records the plain single-word commands (BPM_SET etc).

namespace commands {

std::vector<std::vector<int>> g_scaleCalls;   // {root, template}
std::vector<std::vector<int>> g_commandCalls; // {cmd, param1, param2}

void sendScaleCommand(uint8_t root, uint8_t templateIndex) {
    g_scaleCalls.push_back({root, templateIndex});
}

void sendCommand(Command cmd, uint8_t param1, uint8_t param2) {
    g_commandCalls.push_back({static_cast<int>(cmd), param1, param2});
}

void sendGateSet(uint8_t, const std::vector<bool>&) {
    // No-op host double.
}

void sendPitchSet(uint8_t, uint8_t, common::PlayingOrder,
                  const std::vector<uint8_t>&) {
    // No-op host double.
}

void sendVelocitySet(uint8_t, uint8_t, common::PlayingOrder,
                     const std::vector<uint8_t>&) {
    // No-op host double.
}

}  // namespace commands

namespace {

// --- shared fixture ---------------------------------------------------------------

hardware::Led led{16};
hardware::LedMatrix matrix{16};

// A fresh view over the shared fixture with both recording seams cleared, so
// each test asserts on exactly its own posted commands and drawn pixels.
SettingsView makeView() {
    commands::g_scaleCalls.clear();
    commands::g_commandCalls.clear();
    hardware::g_matrixDraws.clear();
    return SettingsView(led, matrix);
}

size_t postedScaleCount() { return commands::g_scaleCalls.size(); }

bool lastScaleCallWas(int root, int templateIdx) {
    if (commands::g_scaleCalls.empty()) return false;
    const std::vector<int>& call = commands::g_scaleCalls.back();
    return call.size() == 2 && call[0] == root && call[1] == templateIdx;
}

Event press(ui::KeyId id, uint8_t mods = ui::mod::NONE) {
    return Event::keyPressed(id, mods, 0);
}

Event hold(ui::KeyId id, uint8_t mods = ui::mod::NONE) {
    return Event::keyHeld(id, mods, 0);
}

// --- 1. ENTER cycles the edited field (bPM <-> Scale) -------------------------------

void test_enter_cycles_field() {
    SettingsView view = makeView();

    check(view.selectedField() == SettingsView::Field::BPM,
          "a fresh view edits the BPM field (existing behavior preserved)");

    const UIState state;
    UIState next = view.handleEvent(state, press(ui::KeyId::ENTER));
    check(view.selectedField() == SettingsView::Field::SCALE,
          "a pressed ENTER moves to the Scale field");
    check(postedScaleCount() == 0,
          "cycling the field posts no scale command");
    check(next.bpm == state.bpm && next.scaleRoot == state.scaleRoot,
          "cycling the field changes no values");

    next = view.handleEvent(next, press(ui::KeyId::ENTER));
    check(view.selectedField() == SettingsView::Field::BPM,
          "a second ENTER cycles back to the BPM field");

    // Held repeats are ignored, like the sibling views.
    view.handleEvent(next, hold(ui::KeyId::ENTER));
    check(view.selectedField() == SettingsView::Field::BPM,
          "a held ENTER repeat does not cycle the field");
}

// --- 2. Scale field: UP/DOWN adjust the root with wrap ------------------------------

void test_up_down_adjust_root_with_wrap() {
    SettingsView view = makeView();
    view.selectField(SettingsView::Field::SCALE);

    UIState state;
    state.scaleRoot = 0;
    state.scaleTemplateIndex = 0;

    // UP walks the root up one pitch class at a time.
    UIState next = view.handleEvent(state, press(ui::KeyId::UP));
    check(next.scaleRoot == 1, "UP on the Scale field moves the root 0 -> 1 (C#)");
    check(next.scaleTemplateIndex == 0, "UP leaves the template untouched");
    check(postedScaleCount() == 1, "one root change posts exactly one SCALE_SET");
    check(lastScaleCallWas(1, 0), "the SCALE_SET carries the full (root, template) pair");

    // DOWN from the bottom wraps to B.
    next = view.handleEvent(state, press(ui::KeyId::DOWN));
    check(next.scaleRoot == 11, "DOWN from root 0 wraps to 11 (B)");
    check(lastScaleCallWas(11, 0), "the wrapped root rides the SCALE_SET pair");

    // UP from the top wraps to C.
    UIState top = state;
    top.scaleRoot = 11;
    next = view.handleEvent(top, press(ui::KeyId::UP));
    check(next.scaleRoot == 0, "UP from root 11 wraps to 0 (C)");
    check(lastScaleCallWas(0, 0), "the wrapped SCALE_SET carries root 0");

    // SHIFT is the coarse step (5), sibling-view convention.
    next = view.handleEvent(state, press(ui::KeyId::UP, ui::mod::SHIFT));
    check(next.scaleRoot == 5, "SHIFT+UP steps the root by 5 (coarse)");
    check(lastScaleCallWas(5, 0), "the coarse root step posts its pair");

    // Held repeats keep adjusting, like the BPM field always has.
    next = view.handleEvent(state, hold(ui::KeyId::UP));
    check(next.scaleRoot == 1, "a held UP repeat adjusts the root too");
}

// --- 3. Scale field: LEFT/RIGHT adjust the template with wrap ----------------------

void test_left_right_adjust_template_with_wrap() {
    SettingsView view = makeView();
    view.selectField(SettingsView::Field::SCALE);

    const int count = SettingsView::scaleTemplateCount();
    check(count == common::scaleTemplateCount(),
          "the view exposes the full built-in template count");

    UIState state;
    state.scaleRoot = 3;
    state.scaleTemplateIndex = 0;

    // RIGHT walks the template index up one entry at a time.
    UIState next = view.handleEvent(state, press(ui::KeyId::RIGHT));
    check(next.scaleTemplateIndex == 1, "RIGHT moves the template 0 -> 1");
    check(next.scaleRoot == 3, "RIGHT leaves the root untouched");
    check(postedScaleCount() == 1, "one template change posts exactly one SCALE_SET");
    check(lastScaleCallWas(3, 1), "the SCALE_SET carries the full (root, template) pair");

    // LEFT from the bottom wraps to the last built-in template.
    next = view.handleEvent(state, press(ui::KeyId::LEFT));
    check(next.scaleTemplateIndex == count - 1,
          "LEFT from template 0 wraps to the last template");
    check(lastScaleCallWas(3, count - 1),
          "the wrapped template rides the SCALE_SET pair");

    // RIGHT from the top wraps back to the first template.
    UIState topTemplate = state;
    topTemplate.scaleTemplateIndex = static_cast<uint8_t>(count - 1);
    next = view.handleEvent(topTemplate, press(ui::KeyId::RIGHT));
    check(next.scaleTemplateIndex == 0, "RIGHT from the last template wraps to 0");
    check(lastScaleCallWas(3, 0), "the wrapped SCALE_SET carries template 0");

    // SHIFT is the coarse step (5).
    next = view.handleEvent(state, press(ui::KeyId::RIGHT, ui::mod::SHIFT));
    check(next.scaleTemplateIndex == 5, "SHIFT+RIGHT steps the template by 5 (coarse)");
    check(lastScaleCallWas(3, 5), "the coarse template step posts its pair");
}

// --- 4. BPM regression: vertical keys still drive BPM, horizontal stay unbound -------

void test_bpm_field_regression() {
    SettingsView view = makeView();

    UIState state;
    state.bpm = 120;

    UIState next = view.handleEvent(state, press(ui::KeyId::UP));
    check(next.bpm == 121, "UP on the BPM field still increments the BPM");
    check(next.scaleRoot == 0 && next.scaleTemplateIndex == 0,
          "a BPM edit leaves the scale untouched");
    check(postedScaleCount() == 0, "a BPM edit posts no scale command");
    check(!commands::g_commandCalls.empty() &&
              commands::g_commandCalls.back()[0] ==
                  static_cast<int>(commands::Command::BPM_SET),
          "a BPM edit still posts BPM_SET");

    next = view.handleEvent(state, press(ui::KeyId::UP, ui::mod::SHIFT));
    check(next.bpm == 130, "SHIFT+UP on the BPM field still steps by 10");

    // LEFT/RIGHT are unbound on the BPM field: no value change, no command.
    const size_t commandsBefore = commands::g_commandCalls.size();
    next = view.handleEvent(state, press(ui::KeyId::LEFT));
    next = view.handleEvent(state, press(ui::KeyId::RIGHT));
    check(next.bpm == 120 && next.scaleRoot == 0 && next.scaleTemplateIndex == 0,
          "LEFT/RIGHT on the BPM field change nothing");
    check(commands::g_commandCalls.size() == commandsBefore && postedScaleCount() == 0,
          "LEFT/RIGHT on the BPM field post nothing");
}

// --- 5. Note-name rendering of the root ----------------------------------------------

void test_root_note_name_rendering() {
    // The full 12 pitch classes spell C..B with sharps (the sibling views'
    // convention: no accidental renders '-').
    const char* expected[SettingsView::kRootCount] = {
        "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-",
    };
    for (int root = 0; root < SettingsView::kRootCount; root++) {
        char note[4];
        SettingsView::rootNoteName(root, note);
        check(std::strncmp(note, expected[root], 2) == 0 && note[3] == '\0',
              "the root pitch class renders as a note name");
    }

    // Total accessor: out-of-range roots clamp to the nearest pitch class.
    char low[4];
    SettingsView::rootNoteName(-1, low);
    check(std::strncmp(low, "C-", 2) == 0, "a negative root clamps to C");
    char high[4];
    SettingsView::rootNoteName(12, high);
    check(std::strncmp(high, "B-", 2) == 0, "an over-range root clamps to B");

    // render() draws the CURRENT root as that note name on the matrix.
    SettingsView view = makeView();
    view.selectField(SettingsView::Field::SCALE);
    UIState state;
    state.scaleRoot = 1;   // C#
    state.scaleTemplateIndex = 0;
    view.render(state);
    check(hardware::g_matrixDraws.size() == 2,
          "the Scale render draws the label and the note");
    check(hardware::g_matrixDraws.back() == "note:C# ",
          "the Scale render draws the root's note name (C#)");
}

// --- 6. Scale-name rendering of the template -------------------------------------------

void test_scale_name_rendering() {
    // scaleNameAt returns the VERBATIM ScaleTemplates names.
    check(std::strcmp(SettingsView::scaleNameAt(0), "Major") == 0,
          "template 0 renders as Major");
    check(std::strcmp(SettingsView::scaleNameAt(1), "Natural Minor") == 0,
          "template 1 renders as Natural Minor");
    for (int i = 0; i < common::scaleTemplateCount(); i++) {
        check(std::strcmp(SettingsView::scaleNameAt(i),
                          common::SCALE_TEMPLATES[i].name) == 0,
              "every template renders its verbatim ScaleTemplates name");
    }

    // scaleNameLabel condenses the verbatim name to the 3-char matrix label:
    // truncation only, never invented text.
    char major[4];
    SettingsView::scaleNameLabel(0, major);
    check(std::strcmp(major, "Maj") == 0, "\"Major\" condenses to \"Maj\"");
    char natMinor[4];
    SettingsView::scaleNameLabel(1, natMinor);
    check(std::strcmp(natMinor, "Nat") == 0,
          "\"Natural Minor\" condenses to \"Nat\"");
    char last[4];
    SettingsView::scaleNameLabel(common::scaleTemplateCount() - 1, last);
    check(std::strncmp(last, common::SCALE_TEMPLATES[common::scaleTemplateCount() - 1].name, 3) == 0,
          "the last template condenses its own name's first three characters");
    char clampedLow[4];
    SettingsView::scaleNameLabel(-1, clampedLow);
    check(std::strcmp(clampedLow, "Maj") == 0, "a negative index clamps to Major");
    char clampedHigh[4];
    SettingsView::scaleNameLabel(999, clampedHigh);
    check(std::strncmp(clampedHigh,
                       common::SCALE_TEMPLATES[common::scaleTemplateCount() - 1].name, 3) == 0,
          "an over-range index clamps to the last template");

    // render() draws the CURRENT template's name in the label region.
    SettingsView view = makeView();
    view.selectField(SettingsView::Field::SCALE);
    UIState state;
    state.scaleRoot = 0;
    state.scaleTemplateIndex = 1;   // Natural Minor
    view.render(state);
    check(hardware::g_matrixDraws.size() == 2,
          "the Scale render draws the label and the note");
    check(hardware::g_matrixDraws.front() == "label:Nat",
          "the Scale render draws the template's scale name (Nat)");
    check(hardware::g_matrixDraws.back() == "note:C- ",
          "the Scale render still draws the root's note name (C)");

    // BPM render regression: the "bPM" label + the number.
    SettingsView bpmView = makeView();
    UIState bpmState;
    bpmState.bpm = 120;
    bpmView.render(bpmState);
    check(hardware::g_matrixDraws.front() == "label:bPM",
          "the BPM render still draws the bPM label");
    check(hardware::g_matrixDraws.back() == "num:120",
          "the BPM render still draws the BPM number");
}

// --- 7. Reducer::setScale: state mirror + exactly ONE SCALE_SET ------------------------

void test_reducer_set_scale() {
    UIState state;
    state.scaleRoot = 0;
    state.scaleTemplateIndex = 0;

    makeView();  // clear the recording seams

    ui::state::setScale(state, 3, 2);
    check(state.scaleRoot == 3, "setScale stores the root in the UIState mirror");
    check(state.scaleTemplateIndex == 2, "setScale stores the template index");
    check(postedScaleCount() == 1, "setScale posts exactly ONE scale command");
    check(lastScaleCallWas(3, 2),
          "the SCALE_SET carries the exact (root, template) payload");

    // The root is a CYCLIC pitch class: out-of-range values wrap, never clamp.
    ui::state::setScale(state, 12, 5);
    check(state.scaleRoot == 0, "root 12 wraps to pitch class 0 (C)");
    check(lastScaleCallWas(0, 5), "the wrapped root rides the SCALE_SET");
    ui::state::setScale(state, 255, 5);
    check(state.scaleRoot == 255 % 12, "an arbitrary over-range root wraps by 12");
    check(lastScaleCallWas(255 % 12, 5), "the wrapped root is what crossed the cores");

    // The template index CLAMPS to the fixed built-in table at both ends.
    ui::state::setScale(state, 4, 200);
    check(state.scaleTemplateIndex == common::scaleTemplateCount() - 1,
          "an over-range template clamps to the last built-in template");
    check(lastScaleCallWas(4, common::scaleTemplateCount() - 1),
          "the clamped template is what crossed the cores");
}

// --- 8. The Scale field is GLOBAL: no pattern slot in the payload -----------------------

void test_scale_is_global() {
    // The SCALE_SET payload is the bare (root, template) pair — no pattern
    // index — because the scale is a TRUE global like the BPM (FR12-FR14):
    // one value in a single source of truth, read by every pattern.
    SettingsView view = makeView();
    view.selectField(SettingsView::Field::SCALE);

    UIState state;
    state.patternCount = 4;
    state.selectedPattern = 2;
    state.scaleRoot = 0;
    state.scaleTemplateIndex = 0;

    UIState next = view.handleEvent(state, press(ui::KeyId::UP));
    check(next.scaleRoot == 1, "the edit applies with patterns present");
    check(postedScaleCount() == 1, "exactly one SCALE_SET crossed the cores");
    check(lastScaleCallWas(1, 0),
          "the payload is the bare global pair, no pattern slot");
    check(next.selectedPattern == 2 && next.patternCount == 4,
          "the edit leaves the pattern selection untouched");
}

}  // namespace

int main() {
    test_enter_cycles_field();
    test_up_down_adjust_root_with_wrap();
    test_left_right_adjust_template_with_wrap();
    test_bpm_field_regression();
    test_root_note_name_rendering();
    test_scale_name_rendering();
    test_reducer_set_scale();
    test_scale_is_global();

    if (failures == 0) {
        std::printf("test_settings_view: all checks passed\n");
        return 0;
    }
    std::printf("test_settings_view: %d check(s) failed\n", failures);
    return 1;
}
