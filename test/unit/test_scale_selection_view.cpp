// generative-engine-expansion task 19 — Scale Selection UI View (FR4);
// ui-integration T8 — command-based writes.
//
// Validates the per-pattern root note + scale selection view:
//   - the root cycles through the 12 pitch classes 0-11 with WRAP in both
//     directions (also through the key-event front end);
//   - the scale index cycles through ALL built-in templates, with the
//     displayed names taken verbatim from the common ScaleTemplates table;
//   - edits ride the COMMAND QUEUE (ui-integration T8): every committed
//     selection posts ONE PATTERN_SCALE_SET command carrying the pattern's
//     full (root, template) pair — verified through a RECORDING host double
//     of commands::sendPatternScaleCommand. The view holds NO ParameterBus
//     reference (this TU no longer even includes it); reads come from the
//     view's local UI cache (seeded with the factory defaults C Major,
//     updated by every write) and never from the sequencer's state, honoring
//     the no-shared-state-between-cores constraint;
//   - refused selections (out-of-range root / template / pattern slot)
//     cache nothing and post nothing;
//   - changing pattern A's scale never affects pattern B;
//   - ESCAPE returns the UIState to the PATTERNS parent view, and held
//     repeats are ignored.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_scale_selection_view".
//
// The view's render path targets the real LedMatrix driver (Pico SDK PIO),
// and command.cpp (the multicore FIFO) is not compiled host-side, so — like
// test_generator_config_view — this TU supplies host-safe definitions: no-op
// LedMatrix member functions and a RECORDING commands::sendPatternScaleCommand
// double.

#include "commands/command.h"
#include "common/ScaleTemplates.h"
#include "ui/Event.h"
#include "ui/hardware/LedMatrix.h"
#include "ui/state/UIState.h"
#include "ui/views/ScaleSelectionView.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

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

// --- host-safe commands:: seam (RECORDING double) -------------------------------
// ScaleSelectionView.cpp posts its writes through
// commands::sendPatternScaleCommand; command.cpp (the multicore FIFO) is not
// compiled host-side, so this TU links against this recording definition
// instead. Every posted PATTERN_SCALE_SET lands in the vector below, and the
// tests assert on the exact (pattern, root, template) payloads.

namespace commands {

std::vector<std::vector<int>> g_patternScaleCalls;  // {pattern, root, template}

void sendPatternScaleCommand(uint8_t patternIndex, uint8_t root,
                             uint8_t templateIndex) {
    g_patternScaleCalls.push_back({patternIndex, root, templateIndex});
}

}  // namespace commands

namespace {

// --- shared fixture -----------------------------------------------------------

hardware::LedMatrix matrix{16};

// A fresh view over the shared fixture, with the recording seam cleared so
// each test asserts on exactly its own posted commands.
ScaleSelectionView makeView() {
    commands::g_patternScaleCalls.clear();
    return ScaleSelectionView(matrix);
}

// How many PATTERN_SCALE_SET commands have been posted so far.
size_t postedCount() { return commands::g_patternScaleCalls.size(); }

// The last posted payload, as {patternIndex, root, template}.
std::vector<int> lastCall() {
    if (commands::g_patternScaleCalls.empty()) return {};
    return commands::g_patternScaleCalls.back();
}

bool lastCallWas(uint8_t patternIndex, int root, int templateIndex) {
    const std::vector<int> call = lastCall();
    return call.size() == 3 && call[0] == patternIndex && call[1] == root &&
           call[2] == templateIndex;
}

// --- 1. The root cycles through 0-11 with wrap --------------------------------

void test_root_cycles_with_wrap() {
    ScaleSelectionView view = makeView();

    // A fresh view reads the factory defaults (C Major) everywhere and has
    // posted nothing: construction is pure Core0 state, no cross-core write.
    check(view.rootValue(0) == 0, "a fresh view reads root 0 (C)");
    check(view.scaleIndexValue(0) == 0, "a fresh view reads template 0 (Major)");
    check(postedCount() == 0, "constructing the view posts no command");

    // A direct selection caches the pattern's root and posts exactly ONE
    // PATTERN_SCALE_SET carrying the full (root, template) pair.
    check(view.setRoot(0, 11), "selecting root 11 (B) succeeds");
    check(view.rootValue(0) == 11, "the root selection updates the UI cache");
    check(postedCount() == 1, "a root selection posts exactly one command");
    check(lastCallWas(0, 11, 0),
          "the command carries the pattern's full root + template pair");

    // Wrap in both directions: 11 -> 0 upward, 0 -> 11 downward.
    check(view.cycleRoot(0, +1), "an upward root cycle from 11 succeeds");
    check(view.rootValue(0) == 0, "the root wraps 11 -> 0 (C)");
    check(lastCallWas(0, 0, 0), "the wrapped cycle posts the wrapped pair");
    check(view.cycleRoot(0, -1), "a downward root cycle from 0 succeeds");
    check(view.rootValue(0) == 11, "the root wraps 0 -> 11 (B)");

    // Twelve upward cycles from B visit every pitch class exactly once and
    // land back on B (a full sweep of the 12 roots, all in range) — each
    // one posting its own command.
    uint16_t seen = 0;
    const size_t postedBeforeSweep = postedCount();
    for (int i = 0; i < 12; ++i) {
        seen = static_cast<uint16_t>(seen | (1u << view.rootValue(0)));
        check(view.cycleRoot(0, +1), "each of the twelve upward cycles succeeds");
    }
    check(seen == 0x0FFF, "twelve upward cycles visit every root 0-11 exactly once");
    check(view.rootValue(0) == 11, "twelve upward cycles from B end on B");
    check(postedCount() == postedBeforeSweep + 12,
          "each cycle in the sweep posts its own command");

    // A coarse (multi-step) cycle wraps modulo 12 too: 9 + 5 -> 2.
    check(view.setRoot(0, 9), "selecting root 9 (A) succeeds");
    check(view.cycleRoot(0, +5), "a coarse upward root cycle succeeds");
    check(view.rootValue(0) == 2, "a coarse cycle wraps modulo 12 (9 + 5 -> 2)");

    // Out-of-range selections are REFUSED (never clamped into a valid root):
    // a root is a pitch class, so 12 / negative values name nothing. A
    // refused selection caches nothing and posts nothing.
    const size_t postedBeforeRefusal = postedCount();
    check(!view.setRoot(0, 12), "selecting root 12 is refused");
    check(!view.setRoot(0, -1), "selecting a negative root is refused");
    check(view.rootValue(0) == 2, "a refused root selection changes nothing");
    check(postedCount() == postedBeforeRefusal,
          "a refused root selection posts nothing");

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
    // picks among real templates only — and nothing is cached or posted.
    const size_t postedBeforeRefusal = postedCount();
    check(!view.setScaleIndex(1, count), "selecting template `count` is refused");
    check(!view.setScaleIndex(1, -1), "selecting a negative template is refused");
    check(view.scaleIndexValue(1) == 0, "a refused template selection changes nothing");
    check(postedCount() == postedBeforeRefusal,
          "a refused template selection posts nothing");

    // Cycling sweeps ALL templates with wrap at both ends; every committed
    // cycle posts exactly ONE command whose payload keeps the pattern's
    // cached root untouched.
    check(view.setScaleIndex(1, 0), "selecting template 0 succeeds");
    check(view.setRoot(1, 7), "the pattern's root is set to 7 (G)");
    uint32_t seen = 0;
    const size_t postedBeforeSweep = postedCount();
    for (int i = 0; i < count; ++i) {
        seen |= (1u << static_cast<uint32_t>(view.scaleIndexValue(1)));
        check(view.cycleScaleIndex(1, +1), "each upward template cycle succeeds");
        check(lastCallWas(1, 7, view.scaleIndexValue(1)),
              "each template cycle posts the new pair with the root preserved");
    }
    check(seen == ((1u << static_cast<uint32_t>(count)) - 1u),
          "the upward cycles visit every template index exactly once");
    check(view.scaleIndexValue(1) == 0,
          "count upward cycles from template 0 wrap back to 0");
    check(view.rootValue(1) == 7,
          "the template cycles never touch the cached root");
    check(postedCount() == postedBeforeSweep + count,
          "each template cycle posts exactly one command");
    check(view.cycleScaleIndex(1, -1), "a downward template cycle from 0 succeeds");
    check(view.scaleIndexValue(1) == count - 1,
          "the template index wraps 0 -> last downward");

    // Render on the scale field runs to completion.
    view.selectField(ScaleSelectionView::Field::Scale);
    ui::state::UIState state;
    view.render(state);
}

// --- 3. Every committed selection posts ONE PATTERN_SCALE_SET command ----------

void test_selection_posts_commands() {
    ScaleSelectionView view = makeView();

    // A committed root selection updates the local UI cache and posts the
    // full pair for the pattern slot.
    check(view.setRoot(3, 7), "setting pattern 3's root succeeds");
    check(view.rootValue(3) == 7, "the root selection updates the UI cache");
    check(postedCount() == 1 && lastCallWas(3, 7, 0),
          "the root selection posts exactly one (3, 7, 0) command");

    // The same for the template selection — the command re-carries the
    // pattern's already-cached root.
    check(view.setScaleIndex(3, 12), "setting pattern 3's template succeeds");
    check(view.scaleIndexValue(3) == 12,
          "the template selection updates the UI cache");
    check(postedCount() == 2 && lastCallWas(3, 7, 12),
          "the template selection posts one command re-carrying the root");

    // An invalid pattern slot is refused outright: nothing cached, nothing
    // posted (the same 16-slot bound the sequencer's pattern state has).
    check(!view.setRoot(16, 4), "a root selection on slot 16 is refused");
    check(!view.setScaleIndex(16, 4), "a template selection on slot 16 is refused");
    check(view.rootValue(16) == 0 && view.scaleIndexValue(16) == 0,
          "an invalid pattern slot reads as the defaults, never a write");
    check(postedCount() == 2, "a refused slot posts nothing");

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
    check(view.rootValue(4) == 1,
          "an UP key press advances the selected pattern's root (+1)");
    check(lastCallWas(4, 1, 0), "the UP key edit posts the advanced pair");
    Event upCoarse = Event::keyPressed(ui::KeyId::UP, ui::mod::SHIFT, 0);
    view.handleEvent(state, upCoarse);
    check(view.rootValue(4) == 6,
          "SHIFT+UP advances the root by the coarse step (+5)");
    check(lastCallWas(4, 6, 0), "the coarse edit posts the advanced pair");
    Event down = Event::keyPressed(ui::KeyId::DOWN, ui::mod::NONE, 0);
    view.handleEvent(state, down);
    check(view.rootValue(4) == 5, "a DOWN key press lowers the root by 1");
    check(view.scaleIndexValue(4) == 0,
          "root key edits never touch the template index");

    // RIGHT/LEFT toggle the edited field; UP then cycles the template.
    view.handleEvent(state, Event::keyPressed(ui::KeyId::RIGHT, ui::mod::NONE, 0));
    check(view.selectedField() == ScaleSelectionView::Field::Scale,
          "RIGHT moves to the scale field");
    view.handleEvent(state, up);
    check(view.scaleIndexValue(4) == 1,
          "an UP key press advances the scale template");
    check(lastCallWas(4, 5, 1),
          "the template key edit posts the pair with the root preserved");
    check(view.rootValue(4) == 5, "a scale key edit never touches the root");
    view.handleEvent(state, Event::keyPressed(ui::KeyId::LEFT, ui::mod::NONE, 0));
    check(view.selectedField() == ScaleSelectionView::Field::Root,
          "LEFT moves back to the root field");

    // A non-key event changes nothing (the view returns the state as-is)
    // and posts nothing.
    const size_t postedBefore = postedCount();
    const ui::state::UIState unchanged = state;
    const ui::state::UIState result = view.handleEvent(
        state, Event::keyReleased(ui::KeyId::UP, ui::mod::NONE, 0));
    check(result.currentView == unchanged.currentView &&
              result.selectedPattern == unchanged.selectedPattern,
          "a released key event leaves the UI state untouched");
    check(postedCount() == postedBefore,
          "a released key event posts no command");
}

// --- 4. Changing pattern A's scale never affects pattern B --------------------

void test_pattern_isolation() {
    ScaleSelectionView view = makeView();

    // Configure two distinct patterns; each command names exactly one slot.
    check(view.setRoot(2, 5) && view.setScaleIndex(2, 7),
          "pattern A (slot 2) gets root 5 / template 7");
    check(view.setRoot(9, 9) && view.setScaleIndex(9, 3),
          "pattern B (slot 9) gets root 9 / template 3");
    check(view.rootValue(2) == 5 && view.scaleIndexValue(2) == 7,
          "pattern A keeps its own scale in the UI cache");
    check(view.rootValue(9) == 9 && view.scaleIndexValue(9) == 3,
          "pattern B keeps its own scale in the UI cache");
    check(postedCount() == 4, "the four selections post four commands");
    check(lastCallWas(9, 9, 3), "the last command names pattern B only");

    // Re-editing B leaves A exactly as it was — and posts nothing for A.
    check(view.setRoot(9, 0) && view.setScaleIndex(9, 1),
          "pattern B is re-selected to C Major");
    check(view.rootValue(2) == 5 && view.scaleIndexValue(2) == 7,
          "pattern A is untouched by the pattern B edits");
    check(lastCallWas(9, 0, 1), "the re-edit posts pattern B's pair only");
    std::vector<std::vector<int>> calls = commands::g_patternScaleCalls;
    bool anyPatternA = false;
    for (const std::vector<int>& call : calls) {
        if (call.size() == 3 && call[0] == 2) anyPatternA = true;
    }
    check(!anyPatternA || (view.rootValue(2) == 5 && view.scaleIndexValue(2) == 7),
          "no pattern B edit ever retargets pattern A's scale");

    // Through the key front end: an edit on the SELECTED pattern (slot 12)
    // stays on that pattern; A and B keep their scales.
    ui::state::UIState state;
    state.selectedPattern = 12;
    view.selectField(ScaleSelectionView::Field::Root);
    view.handleEvent(state, Event::keyPressed(ui::KeyId::UP, ui::mod::NONE, 0));
    check(view.rootValue(12) == 1,
          "the key edit lands on the selected pattern only");
    check(lastCallWas(12, 1, 0), "the key edit posts the selected pattern only");
    check(view.rootValue(2) == 5 && view.scaleIndexValue(2) == 7,
          "pattern A keeps its scale under another pattern's key edit");
    check(view.rootValue(9) == 0 && view.scaleIndexValue(9) == 1,
          "pattern B keeps its (re-selected) scale");
}

// --- 5. ESCAPE returns to the PATTERNS parent view ------------------------------

void test_escape_returns_to_patterns() {
    ScaleSelectionView view = makeView();

    ui::state::UIState state;
    state.currentView = ui::state::ViewId::SCALE_SELECTION;
    state.selectedPattern = 3;

    // A pressed ESCAPE walks straight back to PATTERNS (FR11).
    Event esc = Event::keyPressed(ui::KeyId::ESCAPE, ui::mod::NONE, 0);
    const ui::state::UIState next = view.handleEvent(state, esc);
    check(next.currentView == ui::state::ViewId::PATTERNS,
          "a pressed ESCAPE returns the state to PATTERNS");
    check(state.selectedPattern == next.selectedPattern,
          "the back-out leaves the rest of the state untouched");
    check(postedCount() == 0, "the back-out posts no scale command");

    // A HELD repeat is ignored — the view stays put until the key is
    // actually pressed again.
    Event escHeld = Event::keyHeld(ui::KeyId::ESCAPE, ui::mod::NONE, 0);
    const ui::state::UIState held = view.handleEvent(state, escHeld);
    check(held.currentView == ui::state::ViewId::SCALE_SELECTION,
          "a held ESCAPE repeat does not navigate");

    // Every other key leaves the current view unchanged.
    Event up = Event::keyPressed(ui::KeyId::UP, ui::mod::NONE, 0);
    const ui::state::UIState afterUp = view.handleEvent(state, up);
    check(afterUp.currentView == ui::state::ViewId::SCALE_SELECTION,
          "an UP press does not navigate away from the view");
}

}  // namespace

int main() {
    test_root_cycles_with_wrap();
    test_scale_cycles_all_templates();
    test_selection_posts_commands();
    test_pattern_isolation();
    test_escape_returns_to_patterns();

    if (failures == 0) {
        std::printf("test_scale_selection_view: all checks passed\n");
        return 0;
    }
    std::printf("test_scale_selection_view: %d check(s) failed\n", failures);
    return 1;
}
