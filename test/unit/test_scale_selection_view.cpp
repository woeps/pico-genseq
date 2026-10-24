// generative-engine-expansion task 19 — Scale Selection UI View (FR4).
//
// Validates the per-pattern root note + scale selection view:
//   - the root cycles through the 12 pitch classes 0-11 with WRAP in both
//     directions (also through the key-event front end);
//   - the scale index cycles through ALL built-in templates, with the
//     displayed names taken verbatim from the common ScaleTemplates table;
//   - every selection writes the pattern's scaleRoot / scaleTemplateIndex
//     through the ParameterBus BASE values (the live per-pattern generative
//     working state) and never arms a modulation overlay;
//   - changing pattern A's scale never affects pattern B.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_scale_selection_view".
//
// The view's render path targets the real LedMatrix driver (Pico SDK PIO),
// so — like test_generator_config_view — this TU supplies host-safe no-op
// definitions of the LedMatrix member functions instead of compiling the
// SDK-dependent LedMatrix.cpp.

#include "common/ScaleTemplates.h"
#include "sequencer/ParameterAddress.h"
#include "sequencer/ParameterBus.h"
#include "sequencer/PatternGenerativeState.h"
#include "sequencer/TransportTimeline.h"
#include "sequencer/WorkingStateManager.h"
#include "ui/Event.h"
#include "ui/hardware/LedMatrix.h"
#include "ui/state/UIState.h"
#include "ui/views/ScaleSelectionView.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using sequencer::ParameterAddress;
using sequencer::ParameterBus;
using sequencer::ParameterScope;
using sequencer::ParamId;
using sequencer::TransportTimeline;
using sequencer::WorkingStateManager;
using ui::ScaleSelectionView;
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
hardware::LedMatrix matrix{16};

// A fresh view over the shared fixture.
ScaleSelectionView makeView() { return ScaleSelectionView(bus, matrix); }

// One per-pattern scale-root / scale-template address.
ParameterAddress rootAddress(uint8_t pattern) {
    ParameterAddress address;
    address.scope = ParameterScope::Pattern;
    address.patternIndex = pattern;
    address.paramId = ParamId::ScaleRoot;
    return address;
}
ParameterAddress templateAddress(uint8_t pattern) {
    ParameterAddress address;
    address.scope = ParameterScope::Pattern;
    address.patternIndex = pattern;
    address.paramId = ParamId::ScaleTemplate;
    return address;
}

// --- 1. The root cycles through 0-11 with wrap --------------------------------

void test_root_cycles_with_wrap() {
    ScaleSelectionView view = makeView();
    check(bus.bind(&timeline, &working), "the bus binds to live state");

    // A direct selection writes the pattern's root.
    check(view.setRoot(0, 11), "selecting root 11 (B) succeeds");
    check(bus.baseValue(rootAddress(0)) == 11,
          "the root selection writes the bus base value");
    check(working.working(0) != nullptr && working.working(0)->scaleRoot == 11,
          "the root selection reaches the per-pattern working state");

    // Wrap in both directions: 11 -> 0 upward, 0 -> 11 downward.
    check(view.cycleRoot(0, +1), "an upward root cycle from 11 succeeds");
    check(bus.baseValue(rootAddress(0)) == 0, "the root wraps 11 -> 0 (C)");
    check(view.cycleRoot(0, -1), "a downward root cycle from 0 succeeds");
    check(bus.baseValue(rootAddress(0)) == 11, "the root wraps 0 -> 11 (B)");

    // Twelve upward cycles from B visit every pitch class exactly once and
    // land back on B (a full sweep of the 12 roots, all in range).
    uint16_t seen = 0;
    for (int i = 0; i < 12; ++i) {
        seen = static_cast<uint16_t>(seen | (1u << bus.baseValue(rootAddress(0))));
        check(view.cycleRoot(0, +1), "each of the twelve upward cycles succeeds");
    }
    check(seen == 0x0FFF, "twelve upward cycles visit every root 0-11 exactly once");
    check(bus.baseValue(rootAddress(0)) == 11,
          "twelve upward cycles from B end on B");

    // A coarse (multi-step) cycle wraps modulo 12 too: 9 + 5 -> 2.
    check(view.setRoot(0, 9), "selecting root 9 (A) succeeds");
    check(view.cycleRoot(0, +5), "a coarse upward root cycle succeeds");
    check(bus.baseValue(rootAddress(0)) == 2,
          "a coarse cycle wraps modulo 12 (9 + 5 -> 2)");

    // Out-of-range selections are REFUSED (never clamped into a valid root):
    // a root is a pitch class, so 12 / negative values name nothing.
    check(!view.setRoot(0, 12), "selecting root 12 is refused");
    check(!view.setRoot(0, -1), "selecting a negative root is refused");
    check(bus.baseValue(rootAddress(0)) == 2,
          "a refused root selection changes nothing");

    // Render on the root field runs to completion (the LedMatrix seam
    // observes nothing; this is a totality smoke under the sanitizers).
    ui::state::UIState state;
    view.render(state);
}

// --- 2. The scale index cycles through all templates (names from the table) ---

void test_scale_cycles_all_templates() {
    ScaleSelectionView view = makeView();

    const int count = view.scaleCount();
    check(count == common::scaleTemplateCount(),
          "the view offers exactly the built-in template count");
    check(count >= 20 && count <= 30,
          "the template count stays inside the FR3 window (20-30)");

    // Every displayed name comes verbatim from the ScaleTemplates table, is
    // non-empty, and no two templates share a name (bounded 29x29 sweep).
    for (int i = 0; i < count; ++i) {
        const char* name = view.scaleNameAt(i);
        check(name != nullptr && name[0] != '\0',
              "every template displays a non-empty name");
        check(std::strcmp(name, common::SCALE_TEMPLATES[i].name) == 0,
              "the displayed name matches the ScaleTemplates entry verbatim");
        for (int j = i + 1; j < count; ++j) {
            check(std::strcmp(name, view.scaleNameAt(j)) != 0,
                  "no two templates share a display name");
        }
    }
    check(std::strcmp(view.scaleNameAt(0), "Major") == 0,
          "template 0 displays as Major");

    // The name accessor is TOTAL: out-of-range indices clamp, never read
    // out of bounds.
    check(std::strcmp(view.scaleNameAt(count),
                      common::SCALE_TEMPLATES[count - 1].name) == 0,
          "an over-range name index clamps to the last template");
    check(std::strcmp(view.scaleNameAt(-1), common::SCALE_TEMPLATES[0].name) == 0,
          "a negative name index clamps to the first template");

    // An out-of-range index selection is refused (never clamped): the user
    // picks among real templates only.
    check(!view.setScaleIndex(1, count), "selecting template `count` is refused");
    check(!view.setScaleIndex(1, -1), "selecting a negative template is refused");

    // Cycling sweeps ALL templates with wrap at both ends.
    check(view.setScaleIndex(1, 0), "selecting template 0 succeeds");
    uint32_t seen = 0;
    for (int i = 0; i < count; ++i) {
        seen |= (1u << static_cast<uint32_t>(bus.baseValue(templateAddress(1))));
        check(view.cycleScaleIndex(1, +1),
              "each upward template cycle succeeds");
    }
    check(seen == ((1u << static_cast<uint32_t>(count)) - 1u),
          "the upward cycles visit every template index exactly once");
    check(bus.baseValue(templateAddress(1)) == 0,
          "count upward cycles from template 0 wrap back to 0");
    check(view.cycleScaleIndex(1, -1), "a downward template cycle from 0 succeeds");
    check(bus.baseValue(templateAddress(1)) == count - 1,
          "the template index wraps 0 -> last downward");

    // Render on the scale field runs to completion.
    view.selectField(ScaleSelectionView::Field::Scale);
    ui::state::UIState state;
    view.render(state);
}

// --- 3. Selections write through the ParameterBus base values -----------------

void test_selection_writes_through_bus() {
    ScaleSelectionView view = makeView();

    // A committed root selection lands in the bus BASE value and the
    // backing working state — never an overlay.
    check(view.setRoot(3, 7), "setting pattern 3's root succeeds");
    check(bus.baseValue(rootAddress(3)) == 7,
          "the root selection writes the bus base value");
    check(bus.effectiveValue(rootAddress(3)) == 7,
          "the effective value follows the base");
    check(working.working(3) != nullptr && working.working(3)->scaleRoot == 7,
          "the root selection reaches the per-pattern working state");
    check(!bus.hasOverlay(rootAddress(3)),
          "a scale-selection edit never arms a modulation overlay");

    // The same for the template selection.
    check(view.setScaleIndex(3, 12), "setting pattern 3's template succeeds");
    check(bus.baseValue(templateAddress(3)) == 12,
          "the template selection writes the bus base value");
    check(working.working(3) != nullptr &&
              working.working(3)->scaleTemplateIndex == 12,
          "the template selection reaches the per-pattern working state");
    check(!bus.hasOverlay(templateAddress(3)),
          "a template edit never arms a modulation overlay");

    // An invalid pattern slot is refused outright.
    check(!view.setRoot(16, 4), "a root selection on slot 16 is refused");
    check(!view.setScaleIndex(16, 4), "a template selection on slot 16 is refused");

    // --- the same writes through the key-event front end ---------------------
    ui::state::UIState state;
    state.selectedPattern = 4;
    check(view.setRoot(4, 0) && view.setScaleIndex(4, 0),
          "pattern 4 starts at C Major");
    view.selectField(ScaleSelectionView::Field::Root);
    check(view.selectedField() == ScaleSelectionView::Field::Root,
          "selectField selects the root field");

    Event up = Event::keyPressed(ui::KeyId::UP, ui::mod::NONE, 0);
    view.handleEvent(state, up);
    check(bus.baseValue(rootAddress(4)) == 1,
          "an UP key press advances the selected pattern's root (+1)");
    Event upCoarse = Event::keyPressed(ui::KeyId::UP, ui::mod::SHIFT, 0);
    view.handleEvent(state, upCoarse);
    check(bus.baseValue(rootAddress(4)) == 6,
          "SHIFT+UP advances the root by the coarse step (+5)");
    Event down = Event::keyPressed(ui::KeyId::DOWN, ui::mod::NONE, 0);
    view.handleEvent(state, down);
    check(bus.baseValue(rootAddress(4)) == 5,
          "a DOWN key press lowers the root by 1");
    check(bus.baseValue(templateAddress(4)) == 0,
          "root key edits never touch the template index");

    // RIGHT/LEFT toggle the edited field; UP then cycles the template.
    view.handleEvent(state, Event::keyPressed(ui::KeyId::RIGHT, ui::mod::NONE, 0));
    check(view.selectedField() == ScaleSelectionView::Field::Scale,
          "RIGHT moves to the scale field");
    view.handleEvent(state, up);
    check(bus.baseValue(templateAddress(4)) == 1,
          "an UP key press advances the scale template");
    check(bus.baseValue(rootAddress(4)) == 5,
          "a scale key edit never touches the root");
    view.handleEvent(state, Event::keyPressed(ui::KeyId::LEFT, ui::mod::NONE, 0));
    check(view.selectedField() == ScaleSelectionView::Field::Root,
          "LEFT moves back to the root field");

    // A non-key event changes nothing (the view returns the state as-is).
    const ui::state::UIState unchanged = state;
    const ui::state::UIState result = view.handleEvent(
        state, Event::keyReleased(ui::KeyId::UP, ui::mod::NONE, 0));
    check(result.currentView == unchanged.currentView &&
              result.selectedPattern == unchanged.selectedPattern,
          "a released key event leaves the UI state untouched");
}

// --- 4. Changing pattern A's scale never affects pattern B --------------------

void test_pattern_isolation() {
    ScaleSelectionView view = makeView();

    // Configure two distinct patterns.
    check(view.setRoot(2, 5) && view.setScaleIndex(2, 7),
          "pattern A (slot 2) gets root 5 / template 7");
    check(view.setRoot(9, 9) && view.setScaleIndex(9, 3),
          "pattern B (slot 9) gets root 9 / template 3");
    check(bus.baseValue(rootAddress(2)) == 5 &&
              bus.baseValue(templateAddress(2)) == 7,
          "pattern A keeps its own scale");
    check(bus.baseValue(rootAddress(9)) == 9 &&
              bus.baseValue(templateAddress(9)) == 3,
          "pattern B keeps its own scale");
    check(working.working(2) != nullptr && working.working(2)->scaleRoot == 5 &&
              working.working(2)->scaleTemplateIndex == 7,
          "pattern A's working state carries its own scale");
    check(working.working(9) != nullptr && working.working(9)->scaleRoot == 9 &&
              working.working(9)->scaleTemplateIndex == 3,
          "pattern B's working state carries its own scale");

    // Re-editing B leaves A exactly as it was.
    check(view.setRoot(9, 0) && view.setScaleIndex(9, 1),
          "pattern B is re-selected to C Major");
    check(bus.baseValue(rootAddress(2)) == 5 && bus.baseValue(templateAddress(2)) == 7,
          "pattern A is untouched by the pattern B edits");
    check(working.working(2)->scaleRoot == 5 &&
              working.working(2)->scaleTemplateIndex == 7,
          "pattern A's working state is untouched by the pattern B edits");

    // Through the key front end: an edit on the SELECTED pattern (slot 12)
    // stays on that pattern; A and B keep their scales.
    ui::state::UIState state;
    state.selectedPattern = 12;
    view.selectField(ScaleSelectionView::Field::Root);
    view.handleEvent(state, Event::keyPressed(ui::KeyId::UP, ui::mod::NONE, 0));
    check(bus.baseValue(rootAddress(12)) == 1,
          "the key edit lands on the selected pattern only");
    check(bus.baseValue(rootAddress(2)) == 5 && bus.baseValue(templateAddress(2)) == 7,
          "pattern A keeps its scale under another pattern's key edit");
    check(bus.baseValue(rootAddress(9)) == 0 && bus.baseValue(templateAddress(9)) == 1,
          "pattern B keeps its (re-selected) scale");
}

}  // namespace

int main() {
    test_root_cycles_with_wrap();
    test_scale_cycles_all_templates();
    test_selection_writes_through_bus();
    test_pattern_isolation();

    if (failures == 0) {
        std::printf("test_scale_selection_view: all checks passed\n");
        return 0;
    }
    std::printf("test_scale_selection_view: %d check(s) failed\n", failures);
    return 1;
}
