// generative-engine-expansion task 21 — Modulation UI (FR7-FR14, FR16);
// ui-integration T7 — command-queue refactor.
//
// Validates the modulation pattern list / target configuration view against
// the command-queue contract (the view holds NO ModulationRegistry and NO
// ParameterBus — the ONLY cross-core path is the command queue):
//   - the view enters on the PATTERN LIST page listing every global
//     modulation pattern of the fixed pool (FR16); the cursor moves freely
//     (wrapping) and ENTER drills into the cursor pattern's TARGET
//     CONFIGURATION, selecting the pattern's FIRST bound target (FR8);
//   - TARGET claims post MOD_TARGET_ADD carrying the driving pattern index;
//     a full 16-slot pool is surfaced as a rejected action (never a block,
//     never a growth) and an out-of-pool pattern posts nothing;
//   - TAB / LEFT / RIGHT cycle the drilled pattern's bound targets only
//     (other patterns' targets are never reachable from the page);
//   - the parameter picker mirrors the REAL task-16 table EXACTLY (name /
//     range / scope verbatim, in the SAME ParamId order the sequencer's
//     handler resolves the ids by — the table both cores share);
//   - every configuration edit posts ONE MOD_TARGET_SET_PARAM word with the
//     packed (slot, field, value) payload — verified through a RECORDING
//     host double of commands::sendCommand, the ONE primitive the view's
//     inline senders (sendModTargetAddCommand / sendModTargetSetParamCommand)
//     funnel into;
//   - the scope binding rule holds on the UI side: selecting a parameter
//     binds the table's scope (an explicit selection is always VALIDLY
//     addressed), switching the scope re-points a scope-mismatched
//     parameter (global keeps only the BPM, per-pattern falls back to the
//     first per-pattern parameter), and a factory-claimed target keeps its
//     deliberately INVALID address until configured;
//   - the FR14 config fields clamp (scale 0-127, balance -127..127,
//     duration length 1-16) and post their field values (balance as a
//     signed two's-complement byte);
//   - ESCAPE backs out two levels: target configuration -> pattern list
//     (the UIState unchanged), pattern list -> the previous global view
//     (PATTERNS by default, overridable by the wiring layer); held
//     repeats and releases are ignored;
//   - the sequencer core decodes the two commands: MOD_TARGET_ADD claims
//     the first free slot of its OWN ModulationRegistry bound to the
//     command's pattern (factory configuration, full pool a graceful
//     refusal, foreign pattern refused by canApplyCommand);
//     MOD_TARGET_SET_PARAM writes one validated field of one ACTIVE slot
//     (ParamId adopts the table's scope, Scope re-points a mismatched
//     parameter, scale/balance/duration clamp, foreign ordinals refused,
//     inactive slots and unknown fields dropped) — resolving against the
//     process-wide g_modulationRegistry, never against any UI state.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_modulation_pattern_view".
//
// The view's render path targets the real LedMatrix driver (Pico SDK PIO),
// and command.cpp (the multicore FIFO) is not compiled host-side, so — like
// test_mutation_view — this TU supplies host-safe definitions: no-op
// LedMatrix member functions and a RECORDING commands::sendCommand double
// (the inline senders in command.h funnel every posted word through it).

#include "FakeMonotonicClock.h"
#include "RecordingMidiOutput.h"
#include "commands/command.h"
#include "sequencer/ModulationRegistry.h"
#include "sequencer/ModulationTarget.h"
#include "sequencer/ParameterAddress.h"
#include "sequencer.h"
#include "ui/Event.h"
#include "ui/hardware/LedMatrix.h"
#include "ui/state/UIState.h"
#include "ui/views/ModulationPatternView.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using sequencer::ModulationOrder;
using sequencer::ParameterScope;
using sequencer::ParamId;
using sequencer::StepUnit;
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

// --- host-safe commands:: seam (a RECORDING double) ---------------------------
// The view posts every action through commands::sendModTargetAddCommand /
// commands::sendModTargetSetParamCommand — INLINE senders in command.h that
// both funnel into commands::sendCommand — so ONE recording double of that
// primitive captures every posted word. The tests unpack the payloads with
// the same header helpers the sequencer handler uses, verifying the WIRE
// FORMAT itself (param1 = field<<4 | slot, param2 = value).

namespace commands {

struct SentWord {
    Command cmd;
    uint8_t param1;
    uint8_t param2;
};
std::vector<SentWord> g_sentWords;

void sendCommand(Command cmd, uint8_t param1, uint8_t param2) {
    g_sentWords.push_back({cmd, param1, param2});
}

void resetPostedCommands() { g_sentWords.clear(); }

// How many MOD_TARGET_ADD words were posted, and (when `index` is in range)
// the pattern index the index-th claim carried.
size_t postedAddCount() {
    size_t count = 0;
    for (const SentWord& word : g_sentWords) {
        if (word.cmd == Command::MOD_TARGET_ADD) ++count;
    }
    return count;
}
uint8_t postedAddPattern(size_t index) {
    size_t seen = 0;
    for (const SentWord& word : g_sentWords) {
        if (word.cmd == Command::MOD_TARGET_ADD) {
            if (seen == index) return word.param1;
            ++seen;
        }
    }
    return 0xFF;
}

// How many MOD_TARGET_SET_PARAM words carry the given field for the given
// slot, and the value byte of the index-th such word.
size_t postedSetCount(uint8_t slot, commands::ModTargetField field) {
    size_t count = 0;
    for (const SentWord& word : g_sentWords) {
        if (word.cmd == Command::MOD_TARGET_SET_PARAM &&
            modTargetSlotOf(word.param1) == slot &&
            modTargetFieldOf(word.param1) == static_cast<uint8_t>(field)) {
            ++count;
        }
    }
    return count;
}
uint8_t postedSetValue(uint8_t slot, commands::ModTargetField field,
                       size_t index) {
    size_t seen = 0;
    for (const SentWord& word : g_sentWords) {
        if (word.cmd == Command::MOD_TARGET_SET_PARAM &&
            modTargetSlotOf(word.param1) == slot &&
            modTargetFieldOf(word.param1) == static_cast<uint8_t>(field)) {
            if (seen == index) return word.param2;
            ++seen;
        }
    }
    return 0xFF;
}

}  // namespace commands

// --- the pattern list page (FR7/FR11/FR16) -------------------------------------

void test_pattern_list_page() {
    hardware::LedMatrix led{0};
    ModulationPatternView view{led};

    // FR16: a fresh view enters on the pattern list, listing every pattern
    // of the sequencer's fixed pool.
    check(view.mode() == ModulationPatternView::Mode::PatternList,
          "a fresh view enters on the pattern list");
    check(ModulationPatternView::patternCount() == 8,
          "the pool holds eight global modulation patterns");
    check(view.selectedPatternIndex() == 0, "the cursor starts at pattern 0");

    // The cursor moves freely and wraps, in both directions.
    view.cyclePatternIndex(-1);
    check(view.selectedPatternIndex() == 7, "cycling backwards wraps to 7");
    view.cyclePatternIndex(3);
    check(view.selectedPatternIndex() == 2, "cycling forwards walks the cursor");
    check(view.selectPatternIndex(6), "selectPatternIndex(6) is accepted");
    check(view.selectedPatternIndex() == 6, "the cursor moved to pattern 6");
    check(!view.selectPatternIndex(8), "selectPatternIndex(8) is refused");
    check(!view.selectPatternIndex(255), "selectPatternIndex(255) is refused");
    check(view.selectedPatternIndex() == 6, "a refused selection changes nothing");

    // onEnter always re-lands on the pattern list (FR16), even after a
    // drill-in.
    view.addTarget(0);
    view.enterTargetConfig(0);
    check(view.mode() == ModulationPatternView::Mode::TargetConfig,
          "the drill entered the config page");
    view.onEnter();
    check(view.mode() == ModulationPatternView::Mode::PatternList,
          "onEnter re-lands on the pattern list");

    // The key front end: UP/LEFT previous, DOWN/RIGHT next, all wrapping.
    ui::state::UIState state;
    view.selectPatternIndex(0);
    view.handleEvent(state, Event::keyPressed(KeyId::LEFT, NONE, 0));
    check(view.selectedPatternIndex() == 7, "LEFT wraps backwards to pattern 7");
    view.handleEvent(state, Event::keyPressed(KeyId::UP, NONE, 0));
    check(view.selectedPatternIndex() == 6, "UP steps to the previous pattern");
    view.handleEvent(state, Event::keyPressed(KeyId::DOWN, NONE, 0));
    check(view.selectedPatternIndex() == 7, "DOWN steps to the next pattern");
    view.handleEvent(state, Event::keyPressed(KeyId::RIGHT, NONE, 0));
    check(view.selectedPatternIndex() == 0, "RIGHT wraps forwards to pattern 0");
}

// --- the drill-in (FR8/FR16) ----------------------------------------------------

void test_enter_drills_into_target_config() {
    hardware::LedMatrix led{0};
    ModulationPatternView view{led};

    // A pattern with no bound targets drills into "Non" (no selection).
    check(view.enterTargetConfig(3), "enterTargetConfig(3) is accepted");
    check(view.mode() == ModulationPatternView::Mode::TargetConfig,
          "the drill entered the config page");
    check(view.configuredPattern() == 3, "the drilled pattern is configured");
    check(!view.hasSelectedTarget(), "an unbound pattern selects no target");
    check(view.selectedTargetSlot() == ModulationPatternView::kNoTarget,
          "the selection is the no-target sentinel");
    check(!view.enterTargetConfig(8), "an out-of-pool drill is refused");

    // Claim two targets for pattern 5 and one for pattern 1: drilling into
    // 5 selects the FIRST bound target (FR8), never pattern 1's.
    view.exitTargetConfig();
    const uint8_t first = view.addTarget(5);
    const uint8_t second = view.addTarget(5);
    const uint8_t foreign = view.addTarget(1);
    check(first == 0 && second == 1 && foreign == 2,
          "claims fill the slot pool in order");
    check(view.enterTargetConfig(5), "the second drill is accepted");
    check(view.selectedTargetSlot() == first,
          "the drill selects the pattern's first bound target");
    check(view.selectedParam() == 0,
          "the picker cursor follows the factory binding (BPM)");
    view.cycleTarget(1);
    check(view.selectedTargetSlot() == second, "cycling reaches the second");
    check(view.selectedParam() == 0, "the picker cursor re-pointed too");

    // Re-drilling pattern 1 selects ITS first bound target.
    view.exitTargetConfig();
    check(view.enterTargetConfig(1), "the pattern-1 drill is accepted");
    check(view.selectedTargetSlot() == foreign,
          "the drill selects pattern 1's own target");

    // The ENTER key front end drills the cursor pattern.
    view.exitTargetConfig();
    view.selectPatternIndex(5);
    view.handleEvent(ui::state::UIState{},
                     Event::keyPressed(KeyId::ENTER, NONE, 0));
    check(view.mode() == ModulationPatternView::Mode::TargetConfig,
          "ENTER drills into the config page");
    check(view.configuredPattern() == 5, "ENTER drilled the cursor pattern");
    check(view.selectedTargetSlot() == first,
          "ENTER selected the pattern's first bound target");
    check(view.selectedField() == ModulationPatternView::Field::Param,
          "the drill restarts the field cursor at Param");
}

// --- target claims (FR13) post MOD_TARGET_ADD -----------------------------------

void test_target_claim_posts_command() {
    hardware::LedMatrix led{0};
    ModulationPatternView view{led};
    commands::resetPostedCommands();

    // A claim posts ONE MOD_TARGET_ADD carrying the driving pattern index.
    check(view.addTarget(2) == 0, "the first claim takes slot 0");
    check(commands::postedAddCount() == 1, "the claim posted one ADD word");
    check(commands::postedAddPattern(0) == 2, "the ADD carries pattern 2");

    // The local mirror arrives in the registry's FACTORY configuration:
    // deliberately invalid address (per-pattern scope holding the
    // global-only BPM id), scale 64, balance 0, beat/1, forward.
    check(view.targetScope() == ParameterScope::Pattern,
          "a claimed target starts per-pattern (FR13 default)");
    check(view.selectedParam() == 0, "the factory binding is the BPM id");
    check(view.targetScaling() == 64, "the factory scaling is 64");
    check(view.targetBalance() == 0, "the factory balance is 0");
    check(view.targetStepDuration().unit == StepUnit::Beat &&
              view.targetStepDuration().length == 1,
          "the factory duration is one beat");
    check(view.targetOrder() == ModulationOrder::Forward,
          "the factory order is forward");
    check(sequencer::validAddress(
              sequencer::ParameterAddress{view.targetScope(), 0,
                                          static_cast<ParamId>(view.selectedParam())}) ==
              false,
          "the factory address is deliberately invalid");
    check(view.targetSlotCount() == 16, "the slot pool holds sixteen slots");
    check(view.activeTargetCount() == 1, "one slot is active");

    // Claims bind to their pattern: boundTargetCount counts per pattern.
    check(view.boundTargetCount(2) == 1, "pattern 2 owns one target");
    check(view.boundTargetCount(3) == 0, "pattern 3 owns none");
    check(view.boundTargetCount(8) == 0, "an out-of-pool pattern owns none");

    // An out-of-pool pattern is refused WITHOUT a post.
    commands::resetPostedCommands();
    check(view.addTarget(8) == ModulationPatternView::kNoTarget,
          "addTarget(8) is refused");
    check(commands::g_sentWords.empty(), "the refused claim posted nothing");

    // The pool caps at sixteen claims; the seventeenth is a SURFACED
    // rejection (never a block, never a growth).
    for (uint8_t i = 1; i < 16; ++i) {
        check(view.addTarget(0) == i, "the pool still has free slots");
    }
    check(view.activeTargetCount() == 16, "the pool is full at sixteen");
    check(!view.lastActionRejected(), "claims up to now were accepted");
    commands::resetPostedCommands();
    check(view.addTarget(0) == ModulationPatternView::kNoTarget,
          "the seventeenth claim returns the sentinel");
    check(view.lastActionRejected(), "the full pool is surfaced");
    check(commands::postedAddCount() == 0,
          "the refused claim posts no ADD word");
    // A later success clears the latch.
    check(view.selectTargetSlot(3), "an existing slot stays selectable");
    check(!view.lastActionRejected(), "a success cleared the latch");
}

// --- TAB / LEFT / RIGHT cycle the drilled pattern's targets --------------------

void test_target_cycling() {
    hardware::LedMatrix led{0};
    ModulationPatternView view{led};

    // Pattern 4 owns slots 0, 1 and 3 (2 belongs to pattern 6): cycling
    // stays inside pattern 4's targets and wraps.
    check(view.addTarget(4) == 0, "pattern 4 takes slot 0");
    check(view.addTarget(4) == 1, "pattern 4 takes slot 1");
    check(view.addTarget(6) == 2, "pattern 6 takes slot 2");
    view.addTarget(4);  // slot 3
    check(view.boundTargetCount(4) == 3, "pattern 4 owns three targets");

    view.enterTargetConfig(4);
    check(view.selectedTargetSlot() == 0, "the drill selects slot 0");
    view.cycleTarget(1);
    check(view.selectedTargetSlot() == 1, "cycling forward walks the slots");
    view.cycleTarget(1);
    check(view.selectedTargetSlot() == 3,
          "cycling skips pattern 6's target (slot 2)");
    view.cycleTarget(1);
    check(view.selectedTargetSlot() == 0, "cycling wraps back to slot 0");
    view.cycleTarget(-1);
    check(view.selectedTargetSlot() == 3, "cycling backwards wraps to slot 3");
    view.cycleTarget(-2);
    check(view.selectedTargetSlot() == 1, "two backwards hops reach slot 1");

    // TAB / LEFT / RIGHT on the key front end do the same.
    view.selectTargetSlot(0);
    view.handleEvent(ui::state::UIState{}, Event::keyPressed(KeyId::TAB, NONE, 0));
    check(view.selectedTargetSlot() == 1, "TAB walks forwards");
    view.handleEvent(ui::state::UIState{}, Event::keyPressed(KeyId::RIGHT, NONE, 0));
    check(view.selectedTargetSlot() == 3, "RIGHT walks forwards");
    view.handleEvent(ui::state::UIState{}, Event::keyPressed(KeyId::LEFT, NONE, 0));
    check(view.selectedTargetSlot() == 1, "LEFT walks backwards");

    // selectTargetSlot refuses unclaimed slots and foreign actives.
    check(!view.selectTargetSlot(15), "an unclaimed slot is refused");
    check(view.selectedTargetSlot() == 1, "the refused selection changed nothing");
    check(view.selectTargetSlot(2), "a foreign ACTIVE slot is selectable");
    check(view.selectedTargetSlot() == 2, "the explicit selection stands");
    // Cycling re-homes into the drilled pattern from a foreign selection:
    // a forward hop lands on the pattern's FIRST bound slot.
    view.cycleTarget(1);
    check(view.selectedTargetSlot() == 0,
          "cycling from a foreign selection re-homes to pattern 4");

    // A pattern with no bound targets: cycling is a no-op, ENTER selects
    // nothing.
    view.enterTargetConfig(7);
    check(!view.hasSelectedTarget(), "pattern 7 has no targets");
    view.cycleTarget(1);
    check(!view.hasSelectedTarget(), "cycling an unbound pattern does nothing");
    view.cycleTarget(-1);
    check(!view.hasSelectedTarget(), "backwards cycling does nothing either");
}

// --- the parameter picker mirrors the REAL task-16 table (FR12) -----------------

void test_param_picker_mirrors_table() {
    hardware::LedMatrix led{0};
    ModulationPatternView view{led};

    // The picker lists EVERY task-16 parameter (including the global BPM),
    // metadata verbatim, in the SAME id order the sequencer's handler
    // resolves the ids by — drift breaks the cross-core contract.
    check(view.paramCount() == sequencer::kParamIdCount,
          "the picker lists the whole table");
    for (uint8_t i = 0; i < view.paramCount(); ++i) {
        ModulationPatternView::ParamOption option;
        check(view.paramAt(i, option), "paramAt(i) resolves");
        const sequencer::ParamInfo& info = sequencer::kParamInfos[i];
        if (std::strcmp(option.name, info.name) != 0 ||
            option.minValue != info.minValue ||
            option.maxValue != info.maxValue || option.scope != info.scope) {
            std::printf("FAIL: picker entry %u differs from the table (%s)\n", i,
                        info.name);
            ++failures;
        }
    }
    // The known anchors: id 0 is the global BPM, id 1 the per-pattern Gate
    // Steps, and the global scale pair sits at the end.
    ModulationPatternView::ParamOption bpm;
    view.paramAt(0, bpm);
    check(std::strcmp(bpm.name, "BPM") == 0 && bpm.scope == ParameterScope::Global,
          "id 0 is the global BPM");
    ModulationPatternView::ParamOption steps;
    view.paramAt(1, steps);
    check(std::strcmp(steps.name, "Gate Steps") == 0 &&
              steps.scope == ParameterScope::Pattern,
          "id 1 is the per-pattern Gate Steps");
    ModulationPatternView::ParamOption globalRoot;
    check(view.paramAt(sequencer::kParamIdCount - 2, globalRoot) &&
              std::strcmp(globalRoot.name, "Global Scale Root") == 0 &&
              globalRoot.scope == ParameterScope::Global,
          "the global scale root is in the table");

    // Total accessors past the count.
    ModulationPatternView::ParamOption none;
    check(!view.paramAt(sequencer::kParamIdCount, none),
          "paramAt past the count is refused");
    check(!view.paramAt(255, none), "paramAt(255) is refused");

    // The cursor cycles with wrap (no commit, nothing posted).
    commands::resetPostedCommands();
    view.cycleParam(-1);
    check(view.selectedParam() == view.paramCount() - 1,
          "the picker cursor wraps backwards over the table");
    view.cycleParam(1);
    check(view.selectedParam() == 0, "the picker cursor wraps forwards");
    view.cycleParam(static_cast<int>(view.paramCount()));
    check(view.selectedParam() == 0, "a full-cycle hop lands where it started");
    check(commands::g_sentWords.empty(),
          "moving the cursor alone posts no command");
}

// --- parameter binding (FR12) posts the address ---------------------------------

void test_param_binding_posts_commands() {
    hardware::LedMatrix led{0};
    ModulationPatternView view{led};
    view.addTarget(2);  // slot 0, factory binding
    commands::resetPostedCommands();

    // Binding a PER-PATTERN parameter posts the ParamId AND the mirrored
    // AddressPatternSlot (the sequencer's ParamId handler adopts the
    // table's scope by construction, so an explicit selection is always
    // VALIDLY addressed).
    check(view.selectAddressPatternSlot(7), "the address slot mirror is set");
    check(view.selectParam(1), "selectParam(Gate Steps) is accepted");
    check(commands::postedSetCount(0, commands::ModTargetField::ParamId) == 1,
          "the binding posted one ParamId word");
    check(commands::postedSetValue(0, commands::ModTargetField::ParamId, 0) == 1,
          "the ParamId word carries id 1");
    check(commands::postedSetCount(0, commands::ModTargetField::AddressPatternSlot) == 1,
          "a per-pattern binding also posts the address slot");
    check(commands::postedSetValue(0, commands::ModTargetField::AddressPatternSlot, 0) == 7,
          "the address slot word carries the mirror");

    // The local mirror took the FULL address.
    check(view.targetScope() == ParameterScope::Pattern,
          "Gate Steps bound per-pattern");
    check(view.selectedParam() == 1, "the picker cursor followed the binding");
    check(view.addressPatternSlot() == 7, "the mirror reads back");

    // Binding the GLOBAL BPM posts only the ParamId (no pattern slot in a
    // global address).
    commands::resetPostedCommands();
    check(view.selectParam(0), "selectParam(BPM) is accepted");
    check(commands::postedSetCount(0, commands::ModTargetField::ParamId) == 1,
          "the BPM binding posted one ParamId word");
    check(commands::postedSetValue(0, commands::ModTargetField::ParamId, 0) == 0,
          "the ParamId word carries id 0");
    check(commands::postedSetCount(0, commands::ModTargetField::AddressPatternSlot) == 0,
          "a global binding posts no address slot");
    check(view.targetScope() == ParameterScope::Global,
          "BPM bound globally (the table's scope)");

    // Refusals post nothing: an out-of-table id, a foreign id, a claimed
    // but unselected target (drill into an unbound pattern).
    commands::resetPostedCommands();
    check(!view.selectParam(200), "selectParam(200) is refused");
    check(!view.selectParam(sequencer::kParamIdCount),
          "selectParam past the count is refused");
    check(commands::g_sentWords.empty(), "the refusals posted nothing");
    view.enterTargetConfig(5);  // unbound: no selected target
    commands::resetPostedCommands();
    check(!view.selectParam(1), "a binding without a selected target is refused");
    check(commands::g_sentWords.empty(), "the unselected binding posted nothing");

    // UP/DOWN on the Param field COMMITS the new binding (the cursor move
    // rides the same selectParam path).
    view.exitTargetConfig();
    view.enterTargetConfig(2);  // back on slot 0
    commands::resetPostedCommands();
    view.handleEvent(ui::state::UIState{}, Event::keyPressed(KeyId::UP, NONE, 0));
    check(commands::postedSetCount(0, commands::ModTargetField::ParamId) == 1,
          "UP on the Param field commits a binding");
    check(commands::postedSetValue(0, commands::ModTargetField::ParamId, 0) == 1,
          "the committed id walked to 1");
    check(view.selectedParam() == 1, "the cursor followed the commit");
    // SHIFT+UP is the coarse step: ten table entries forward.
    view.handleEvent(ui::state::UIState{}, Event::keyPressed(KeyId::UP, SHIFT, 0));
    check(view.selectedParam() == 11,
          "SHIFT+UP walks the picker cursor ten entries");
}

// --- the scope binding rule (FR13) ------------------------------------------------

void test_scope_binding_rule() {
    hardware::LedMatrix led{0};
    ModulationPatternView view{led};
    view.addTarget(0);  // slot 0, deliberately invalid factory address
    commands::resetPostedCommands();

    // The factory address is invalid: per-pattern scope holding the
    // global-only BPM id.
    check(view.targetScope() == ParameterScope::Pattern &&
              view.selectedParam() == 0,
          "the factory target holds the invalid pattern/BPM address");

    // Toggle to GLOBAL: the BPM is global-legal, so the address becomes
    // valid WITHOUT a re-point. The Scope word is posted.
    check(view.setTargetScope(ParameterScope::Global), "the global scope is set");
    check(view.targetScope() == ParameterScope::Global, "the scope reads back");
    check(view.selectedParam() == 0, "the BPM binding survives the toggle");
    check(commands::postedSetCount(0, commands::ModTargetField::Scope) == 1,
          "the toggle posted one Scope word");
    check(commands::postedSetValue(0, commands::ModTargetField::Scope, 0) == 1,
          "the Scope word carries the global ordinal");
    check(commands::postedSetCount(0, commands::ModTargetField::AddressPatternSlot) == 0,
          "a global address posts no pattern slot");
    check(sequencer::validAddress(
              sequencer::ParameterAddress{ParameterScope::Global, 0, ParamId::GlobalBpm}),
          "the global BPM address is valid");

    // Toggle back to PER-PATTERN: the BPM is global-only, so the binding
    // RE-POINTS to the first per-pattern parameter (Gate Steps) instead of
    // leaving a dead address — the re-pointed ParamId posts BEFORE the
    // Scope, plus the mirrored AddressPatternSlot.
    commands::resetPostedCommands();
    check(view.setTargetScope(ParameterScope::Pattern),
          "the per-pattern scope is set");
    check(view.selectedParam() == 1,
          "the scope switch re-pointed to Gate Steps");
    check(view.targetScope() == ParameterScope::Pattern, "the scope flipped back");
    check(commands::postedSetCount(0, commands::ModTargetField::ParamId) == 1,
          "the re-point posted one ParamId word");
    check(commands::postedSetValue(0, commands::ModTargetField::ParamId, 0) == 1,
          "the re-pointed id is Gate Steps");
    check(commands::postedSetCount(0, commands::ModTargetField::AddressPatternSlot) == 1,
          "the per-pattern switch posted the address slot");
    check(commands::postedSetCount(0, commands::ModTargetField::Scope) == 1,
          "the per-pattern switch posted the Scope word");
    check(commands::postedSetValue(0, commands::ModTargetField::Scope, 0) == 0,
          "the Scope word carries the pattern ordinal");
    check(sequencer::validAddress(
              sequencer::ParameterAddress{ParameterScope::Pattern,
                                          view.addressPatternSlot(),
                                          static_cast<ParamId>(view.selectedParam())}),
          "the re-pointed address is valid");

    // The toggle shortcut flips the two-option field; a foreign ordinal is
    // refused.
    check(!view.setTargetScope(static_cast<ParameterScope>(9)),
          "a foreign scope ordinal is refused");
    view.toggleTargetScope();
    check(view.targetScope() == ParameterScope::Global, "toggleTargetScope flips");
    view.toggleTargetScope();
    check(view.targetScope() == ParameterScope::Pattern, "toggleTargetScope flips back");

    // The address mirror follows the UI's selected pattern through
    // handleEvent (a target bound while pattern 7 is selected addresses
    // pattern 7).
    ui::state::UIState state;
    state.selectedPattern = 7;
    view.handleEvent(state, Event::keyPressed(KeyId::ENTER, NONE, 0));  // field flip
    check(view.addressPatternSlot() == 7, "the mirror follows selectedPattern");
    commands::resetPostedCommands();
    view.selectParam(2);  // Gate Pulses, per-pattern
    check(commands::postedSetValue(0, commands::ModTargetField::AddressPatternSlot, 0) == 7,
          "the binding posted the mirrored pattern 7");
    state.selectedPattern = 200;  // past the capacity: the mirror holds
    view.handleEvent(state, Event::keyPressed(KeyId::ENTER, NONE, 0));
    check(view.addressPatternSlot() == 7,
          "an out-of-capacity selected pattern does not poison the mirror");
    check(!view.selectAddressPatternSlot(16), "an out-of-capacity slot is refused");
    check(view.addressPatternSlot() == 7, "the refused mirror changed nothing");
}

// --- the FR14 config fields post their values -------------------------------------

void test_fr14_config_posts_commands() {
    hardware::LedMatrix led{0};
    ModulationPatternView view{led};
    view.addTarget(0);  // slot 0
    commands::resetPostedCommands();

    // Scaling: clamped 0-127, posted as the ScaleFactor field.
    check(view.setTargetScaling(200), "setTargetScaling(200) is accepted (clamped)");
    check(view.targetScaling() == 127, "the scaling clamps at 127");
    check(commands::postedSetValue(0, commands::ModTargetField::ScaleFactor, 0) == 127,
          "the clamped ScaleFactor word carries 127");
    view.setTargetScaling(-40);
    check(view.targetScaling() == 0, "the scaling clamps at 0");
    check(commands::postedSetValue(0, commands::ModTargetField::ScaleFactor, 1) == 0,
          "the floor ScaleFactor word carries 0");
    view.adjustTargetScaling(50);
    check(view.targetScaling() == 50, "adjustTargetScaling walks the scaling");
    check(commands::postedSetValue(0, commands::ModTargetField::ScaleFactor, 2) == 50,
          "the walked ScaleFactor word carries 50");

    // Balance: clamped -127..127, posted as a SIGNED two's-complement byte.
    view.setTargetBalance(300);
    check(view.targetBalance() == 127, "the balance clamps at 127");
    check(commands::postedSetValue(0, commands::ModTargetField::Balance, 0) == 127,
          "the clamped Balance word carries 127");
    view.setTargetBalance(-300);
    check(view.targetBalance() == -127, "the balance clamps at -127");
    check(commands::postedSetValue(0, commands::ModTargetField::Balance, 1) ==
              static_cast<uint8_t>(-127),
          "the floored Balance word carries the signed byte");
    view.adjustTargetBalance(-28);
    check(view.targetBalance() == -127,
          "adjusting below -127 stays clamped");
    view.adjustTargetBalance(128);
    check(view.targetBalance() == 1, "the balance walked to 1");
    check(commands::postedSetValue(0, commands::ModTargetField::Balance, 3) == 1,
          "the walked Balance word carries 1");

    // Step duration: length clamped 1-16 (the unit kept), unit toggled
    // beat/bar; a foreign unit ordinal is refused.
    check(view.setStepDurationLength(99), "setStepDurationLength(99) clamps");
    check(view.targetStepDuration().length == 16, "the length clamps at 16");
    check(commands::postedSetValue(0, commands::ModTargetField::DurationLength, 0) == 16,
          "the clamped DurationLength word carries 16");
    view.setStepDurationLength(0);
    check(view.targetStepDuration().length == 1, "the length clamps at 1");
    check(commands::postedSetValue(0, commands::ModTargetField::DurationLength, 1) == 1,
          "the floored DurationLength word carries 1");
    check(view.targetStepDuration().unit == StepUnit::Beat,
          "the unit survived the length edits");
    check(!view.setStepDurationUnit(static_cast<StepUnit>(9)),
          "a foreign unit ordinal is refused");
    check(view.setStepDurationUnit(StepUnit::Bar), "the bar unit is set");
    check(view.targetStepDuration().unit == StepUnit::Bar, "the unit reads back");
    check(commands::postedSetValue(0, commands::ModTargetField::DurationUnit, 0) == 1,
          "the DurationUnit word carries the bar ordinal");
    view.toggleStepDurationUnit();
    check(view.targetStepDuration().unit == StepUnit::Beat, "the unit toggled back");

    // Order: the four ordinals cycle with wrap; a foreign ordinal refused.
    check(view.setTargetOrder(ModulationOrder::PingPong), "PingPong is set");
    check(view.targetOrder() == ModulationOrder::PingPong, "the order reads back");
    check(commands::postedSetValue(0, commands::ModTargetField::Order, 0) == 2,
          "the Order word carries the ping-pong ordinal");
    check(!view.setTargetOrder(static_cast<ModulationOrder>(9)),
          "a foreign order ordinal is refused");
    view.cycleTargetOrder(1);
    check(view.targetOrder() == ModulationOrder::Random, "the order cycled to random");
    view.cycleTargetOrder(1);
    check(view.targetOrder() == ModulationOrder::Forward, "the order wrapped");
    view.cycleTargetOrder(-1);
    check(view.targetOrder() == ModulationOrder::Random, "the order cycled backwards");

    // Everything above refused WITHOUT a selected target posts nothing.
    view.enterTargetConfig(5);  // unbound pattern: no selected target
    commands::resetPostedCommands();
    check(!view.setTargetScaling(10), "a scaling edit without a target is refused");
    check(!view.setTargetBalance(10), "a balance edit without a target is refused");
    check(!view.setStepDurationLength(10), "a duration edit without a target is refused");
    check(!view.setStepDurationUnit(StepUnit::Bar), "a unit edit without a target is refused");
    check(!view.setTargetOrder(ModulationOrder::Reverse), "an order edit without a target is refused");
    check(!view.setTargetScope(ParameterScope::Global), "a scope edit without a target is refused");
    check(commands::g_sentWords.empty(),
          "the unselected edits posted no words");

    // A second claimed target posts against ITS slot: the payload addresses
    // the slot, so multiple targets never alias.
    view.exitTargetConfig();
    const uint8_t second = view.addTarget(0);  // slot 1
    check(second == 1, "the second claim took slot 1");
    commands::resetPostedCommands();
    view.selectTargetSlot(second);
    view.setTargetScaling(33);
    check(commands::postedSetCount(1, commands::ModTargetField::ScaleFactor) == 1,
          "the edit posted against slot 1");
    check(commands::postedSetValue(1, commands::ModTargetField::ScaleFactor, 0) == 33,
          "the second slot's ScaleFactor word carries 33");
    check(commands::postedSetCount(0, commands::ModTargetField::ScaleFactor) == 0,
          "the first slot was not touched");
}

// --- ESCAPE backs out two levels (FR16) ---------------------------------------------

void test_two_level_escape() {
    hardware::LedMatrix led{0};
    ModulationPatternView view{led};
    view.addTarget(1);

    ui::state::UIState state;
    state.currentView = ui::state::ViewId::MODULATION;
    state.selectedPattern = 4;

    // Level one: from the target configuration, ESCAPE returns to the
    // pattern list with the UIState UNCHANGED (view-local navigation).
    view.enterTargetConfig(1);
    commands::resetPostedCommands();
    const ui::state::UIState back = view.handleEvent(
        state, Event::keyPressed(KeyId::ESCAPE, NONE, 0));
    check(view.mode() == ModulationPatternView::Mode::PatternList,
          "ESCAPE backs out of the config page to the pattern list");
    check(back.currentView == ui::state::ViewId::MODULATION,
          "the level-one ESCAPE keeps the current view");
    check(back.selectedPattern == 4, "the level-one ESCAPE keeps the pattern");
    check(commands::g_sentWords.empty(), "ESCAPE posts no command");

    // A HELD ESCAPE from the config page is ignored (only the press
    // navigates, matching the sibling views).
    view.enterTargetConfig(1);
    view.handleEvent(state, Event::keyHeld(KeyId::ESCAPE, NONE, 0));
    check(view.mode() == ModulationPatternView::Mode::TargetConfig,
          "a held ESCAPE does not leave the config page");
    view.handleEvent(state, Event::keyReleased(KeyId::ESCAPE, NONE, 0));
    check(view.mode() == ModulationPatternView::Mode::TargetConfig,
          "a released ESCAPE does not leave the config page");
    view.exitTargetConfig();

    // Level two: from the pattern list, ESCAPE hands control back to the
    // previous global view — PATTERNS by default (the sibling convention).
    ui::state::UIState next = view.handleEvent(
        state, Event::keyPressed(KeyId::ESCAPE, NONE, 0));
    check(next.currentView == ui::state::ViewId::PATTERNS,
          "ESCAPE from the list returns to the PATTERNS parent");
    check(next.selectedPattern == 4, "the return preserves the selected pattern");

    // The wiring layer (T9) overrides the return target: whatever global
    // view entered the modulation page is restored.
    view.setReturnView(ui::state::ViewId::SETTINGS);
    next = view.handleEvent(state, Event::keyPressed(KeyId::ESCAPE, NONE, 0));
    check(next.currentView == ui::state::ViewId::SETTINGS,
          "the overridable return view is honored");
    check(view.returnView() == ui::state::ViewId::SETTINGS, "the return view reads back");

    // Held repeats and releases are ignored at level two as well.
    const ui::state::UIState held = view.handleEvent(
        state, Event::keyHeld(KeyId::ESCAPE, NONE, 0));
    check(held.currentView == ui::state::ViewId::MODULATION,
          "a held ESCAPE repeat is ignored on the list page");
    const ui::state::UIState released = view.handleEvent(
        state, Event::keyReleased(KeyId::ESCAPE, NONE, 0));
    check(released.currentView == ui::state::ViewId::MODULATION,
          "a released ESCAPE is ignored on the list page");

    // A non-key event leaves everything alone.
    const ui::state::UIState other = view.handleEvent(state, Event{});
    check(other.currentView == ui::state::ViewId::MODULATION,
          "an unrelated event leaves the view");
}

// --- the key front end and the render totals ---------------------------------------

void test_key_frontend_and_render() {
    hardware::LedMatrix led{0};
    ModulationPatternView view{led};
    ui::state::UIState state;
    commands::resetPostedCommands();

    // ENTER on the config page advances the edited field, wrapping through
    // all seven fields; a foreign field byte is refused.
    view.enterTargetConfig(0);
    check(view.selectedField() == ModulationPatternView::Field::Param,
          "the field cursor starts at Param");
    check(!view.selectField(static_cast<ModulationPatternView::Field>(9)),
          "a foreign field byte is refused");
    view.handleEvent(state, Event::keyPressed(KeyId::ENTER, NONE, 0));
    check(view.selectedField() == ModulationPatternView::Field::Scope,
          "ENTER walks to Scope");
    view.cycleField(1);
    check(view.selectedField() == ModulationPatternView::Field::Scale,
          "cycleField walks to Scale");
    view.cycleField(1);
    check(view.selectedField() == ModulationPatternView::Field::Balance,
          "cycleField walks to Balance");
    view.cycleField(1);
    check(view.selectedField() == ModulationPatternView::Field::Duration,
          "cycleField walks to Duration");
    view.cycleField(1);
    check(view.selectedField() == ModulationPatternView::Field::Unit,
          "cycleField walks to Unit");
    view.cycleField(1);
    check(view.selectedField() == ModulationPatternView::Field::Order,
          "cycleField walks to Order");
    view.cycleField(1);
    check(view.selectedField() == ModulationPatternView::Field::Param,
          "the field cursor wraps back to Param");
    view.cycleField(-1);
    check(view.selectedField() == ModulationPatternView::Field::Order,
          "the field cursor wraps backwards");
    check(view.selectField(ModulationPatternView::Field::Param),
          "selectField(Param) is accepted");

    // A claims a target bound to the drilled pattern through the key front
    // end (the only way a fresh pattern gains targets).
    view.exitTargetConfig();
    view.selectPatternIndex(6);
    view.handleEvent(state, Event::keyPressed(KeyId::ENTER, NONE, 0));  // drill
    commands::resetPostedCommands();
    view.handleEvent(state, Event::keyPressed(KeyId::A, NONE, 0));
    check(view.hasSelectedTarget(), "A claimed a target");
    check(commands::postedAddCount() == 1, "A posted the ADD word");
    check(commands::postedAddPattern(0) == 6, "the claim is bound to pattern 6");
    check(view.boundTargetCount(6) == 1, "the drill pattern owns the claim");

    // UP/DOWN adjust the selected field through the front end: Scope
    // toggles, Scale steps (SHIFT coarse), Duration clamps, Unit toggles,
    // Order cycles.
    view.selectField(ModulationPatternView::Field::Scope);
    view.handleEvent(state, Event::keyPressed(KeyId::UP, NONE, 0));
    check(view.targetScope() == ParameterScope::Global, "UP on Scope toggled it");
    commands::resetPostedCommands();
    view.selectField(ModulationPatternView::Field::Scale);
    view.handleEvent(state, Event::keyPressed(KeyId::UP, SHIFT, 0));
    check(view.targetScaling() == 74,
          "SHIFT+UP stepped the factory 64 up by ten");
    check(commands::postedSetValue(0, commands::ModTargetField::ScaleFactor, 0) == 74,
          "the key-driven edit posted the walked value");
    view.selectField(ModulationPatternView::Field::Balance);
    view.handleEvent(state, Event::keyPressed(KeyId::DOWN, NONE, 0));
    check(view.targetBalance() == -1, "DOWN on Balance stepped it to -1");
    check(commands::postedSetValue(0, commands::ModTargetField::Balance, 0) ==
              static_cast<uint8_t>(-1),
          "the key-driven balance carries the signed byte");
    view.selectField(ModulationPatternView::Field::Duration);
    view.handleEvent(state, Event::keyPressed(KeyId::UP, NONE, 0));
    check(view.targetStepDuration().length == 2, "UP on Duration walked to 2");
    view.selectField(ModulationPatternView::Field::Unit);
    view.handleEvent(state, Event::keyPressed(KeyId::UP, NONE, 0));
    check(view.targetStepDuration().unit == StepUnit::Bar, "UP on Unit toggled to bar");
    view.selectField(ModulationPatternView::Field::Order);
    view.handleEvent(state, Event::keyPressed(KeyId::UP, NONE, 0));
    check(view.targetOrder() == ModulationOrder::Reverse, "UP on Order walked it");

    // render() totals: both pages, every field, the no-target page and the
    // rejection latch.
    view.onEnter();
    view.render(state);  // the pattern list
    view.enterTargetConfig(0);
    view.render(state);  // Param
    view.selectField(ModulationPatternView::Field::Scope);
    view.render(state);
    view.selectField(ModulationPatternView::Field::Scale);
    view.render(state);
    view.selectField(ModulationPatternView::Field::Balance);
    view.render(state);
    view.selectField(ModulationPatternView::Field::Duration);
    view.render(state);
    view.selectField(ModulationPatternView::Field::Unit);
    view.render(state);
    view.selectField(ModulationPatternView::Field::Order);
    view.render(state);
    view.enterTargetConfig(7);  // unbound pattern: the "Non" page
    view.render(state);
    view.exitTargetConfig();
    view.render(state);
}

// --- the sequencer core's FIFO modulation handlers (ui-integration T7) ------------

namespace {

commands::CommandMessage modTargetAddMessage(uint8_t patternIndex) {
    commands::CommandMessage msg;
    msg.cmd = commands::Command::MOD_TARGET_ADD;
    msg.param1 = patternIndex;
    return msg;
}

commands::CommandMessage modTargetSetParamMessage(uint8_t slot,
                                                  commands::ModTargetField field,
                                                  uint8_t value) {
    commands::CommandMessage msg;
    msg.cmd = commands::Command::MOD_TARGET_SET_PARAM;
    // The exact wire packing sendModTargetSetParamCommand produces.
    msg.param1 = static_cast<uint8_t>((static_cast<uint8_t>(field) << 4) |
                                       (slot & 0x0Fu));
    msg.param2 = value;
    return msg;
}

// A clean registry for every dispatch run (the pool is process-wide).
void resetRegistry() {
    for (size_t slot = 0; slot < sequencer::ModulationRegistry::MAX_TARGETS; ++slot) {
        sequencer::g_modulationRegistry.releaseTarget(slot);
    }
}

}  // namespace

void test_fifo_mod_target_dispatch() {
    resetRegistry();
    RecordingMidiOutput midi;
    FakeMonotonicClock clock;
    sequencer::Sequencer seq(midi, clock);
    sequencer::ModulationRegistry& reg = sequencer::g_modulationRegistry;

    // MOD_TARGET_ADD claims the first free slot of the SEQUENCER's OWN
    // registry, bound to the command's pattern, in factory configuration.
    seq.processCommand(modTargetAddMessage(5));
    check(reg.targetActive(0), "the first ADD claimed slot 0");
    check(reg.target(0).patternIndex == 5, "the claim bound pattern 5");
    check(reg.activeTargetCount() == 1, "one slot is active");
    check(reg.target(0).address.scope == ParameterScope::Pattern &&
              reg.target(0).address.paramId == ParamId::GlobalBpm,
          "the claimed slot holds the invalid factory address");
    check(reg.target(0).scaleFactor == 64 && reg.target(0).balance == 0 &&
              reg.target(0).stepDuration.unit == StepUnit::Beat &&
              reg.target(0).stepDuration.length == 1 &&
              reg.target(0).order == ModulationOrder::Forward,
          "the claimed slot holds the factory config");

    seq.processCommand(modTargetAddMessage(7));
    check(reg.targetActive(1) && reg.target(1).patternIndex == 7,
          "the second ADD claimed slot 1 bound to pattern 7");

    // A foreign pattern index is refused by canApplyCommand: nothing is
    // claimed.
    seq.processCommand(modTargetAddMessage(8));
    check(!reg.targetActive(2), "an ADD for pattern 8 claimed nothing");
    check(reg.activeTargetCount() == 2, "the refused ADD changed no count");

    // Fill the pool: the seventeenth ADD is a graceful refusal.
    for (uint8_t i = 0; i < 14; ++i) {
        seq.processCommand(modTargetAddMessage(0));
    }
    check(reg.activeTargetCount() == 16, "the pool filled to sixteen");
    seq.processCommand(modTargetAddMessage(1));
    check(reg.activeTargetCount() == 16,
          "a full pool refuses gracefully (no growth)");

    // MOD_TARGET_SET_PARAM — ParamId: the table's scope rides along, so an
    // explicitly selected parameter is always VALIDLY addressed.
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::ParamId, 1));  // Gate Steps
    check(reg.target(0).address.paramId == static_cast<ParamId>(1),
          "the ParamId write took id 1");
    check(reg.target(0).address.scope == ParameterScope::Pattern,
          "the ParamId write adopted the table's per-pattern scope");
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::ParamId, 0));  // BPM
    check(reg.target(0).address.scope == ParameterScope::Global,
          "the BPM write adopted the global scope");
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::ParamId, 200));
    check(reg.target(0).address.paramId == ParamId::GlobalBpm,
          "an unknown table id is refused");

    // Scope: the global scope keeps the BPM valid; switching a mismatched
    // parameter RE-POINTS it (the same rule the UI enforces).
    seq.processCommand(modTargetSetParamMessage(
        1, commands::ModTargetField::Scope, 1));  // global, on the factory address
    check(reg.target(1).address.scope == ParameterScope::Global &&
              reg.target(1).address.paramId == ParamId::GlobalBpm,
          "the global scope kept the BPM binding");
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::ParamId, 3));  // Gate Probability (pattern)
    check(reg.target(0).address.scope == ParameterScope::Pattern,
          "id 3 bound per-pattern");
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::Scope, 1));  // global: re-point to BPM
    check(reg.target(0).address.paramId == ParamId::GlobalBpm,
          "the global switch re-pointed the mismatched id to BPM");
    check(reg.target(0).address.scope == ParameterScope::Global,
          "the global switch took the scope");
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::Scope, 0));  // back to per-pattern
    check(reg.target(0).address.paramId == static_cast<ParamId>(1),
          "the pattern switch re-pointed to the first per-pattern id");
    check(reg.target(0).address.patternIndex == 0,
          "the per-pattern switch clamped the address slot");
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::Scope, 9));
    check(reg.target(0).address.scope == ParameterScope::Pattern,
          "a foreign scope ordinal is refused");

    // AddressPatternSlot: the per-pattern slot, refused past the capacity.
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::AddressPatternSlot, 7));
    check(reg.target(0).address.patternIndex == 7,
          "the address slot write took pattern 7");
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::AddressPatternSlot, 200));
    check(reg.target(0).address.patternIndex == 7,
          "a foreign address slot is refused");

    // PatternIndex: re-binding a target to another global pattern.
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::PatternIndex, 4));
    check(reg.target(0).patternIndex == 4, "the re-bind took pattern 4");
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::PatternIndex, 9));
    check(reg.target(0).patternIndex == 4, "a foreign re-bind is refused");

    // ScaleFactor: clamped 0-127.
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::ScaleFactor, 200));
    check(reg.target(0).scaleFactor == 127, "the scaling clamps at 127");

    // Balance: a SIGNED byte, clamped -127..127.
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::Balance, static_cast<uint8_t>(-126)));
    check(reg.target(0).balance == -126, "the signed balance byte decoded");
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::Balance, 30));
    check(reg.target(0).balance == 30, "the positive balance decoded");
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::Balance, 0x81));  // -127 signed
    check(reg.target(0).balance == -127, "the balance floor clamps at -127");

    // DurationLength clamps 1-16; DurationUnit accepts only beat/bar.
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::DurationLength, 99));
    check(reg.target(0).stepDuration.length == 16, "the length clamps at 16");
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::DurationLength, 0));
    check(reg.target(0).stepDuration.length == 1, "the length clamps at 1");
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::DurationUnit, 1));
    check(reg.target(0).stepDuration.unit == StepUnit::Bar, "the bar unit took");
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::DurationUnit, 9));
    check(reg.target(0).stepDuration.unit == StepUnit::Bar,
          "a foreign unit ordinal is refused");

    // Order accepts the four ordinals only.
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::Order, 3));
    check(reg.target(0).order == ModulationOrder::Random, "random took");
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::Order, 9));
    check(reg.target(0).order == ModulationOrder::Random,
          "a foreign order ordinal is refused");

    // An INACTIVE slot is dropped (canApplyCommand), and an unknown field
    // ordinal never reaches the target.
    resetRegistry();
    // After the reset, slot 0 is inactive: SET_PARAM words are dropped
    // without touching anything, and no factory default is resurrected.
    seq.processCommand(modTargetSetParamMessage(
        0, commands::ModTargetField::ScaleFactor, 77));
    check(!reg.targetActive(0), "the write to an inactive slot resurrected nothing");
    check(reg.activeTargetCount() == 0, "nothing was claimed by a field write");
    seq.processCommand(modTargetAddMessage(2));
    const sequencer::ModulationTarget snapshot = reg.target(0);
    seq.processCommand(modTargetSetParamMessage(
        0, static_cast<commands::ModTargetField>(12), 77));  // unknown field
    check(reg.target(0).scaleFactor == snapshot.scaleFactor &&
              reg.target(0).address.paramId == snapshot.address.paramId &&
              reg.target(0).order == snapshot.order,
          "an unknown field ordinal changed nothing");
}

int main() {
    test_pattern_list_page();
    test_enter_drills_into_target_config();
    test_target_claim_posts_command();
    test_target_cycling();
    test_param_picker_mirrors_table();
    test_param_binding_posts_commands();
    test_scope_binding_rule();
    test_fr14_config_posts_commands();
    test_two_level_escape();
    test_key_frontend_and_render();
    test_fifo_mod_target_dispatch();
    if (failures == 0) {
        std::printf("test_modulation_pattern_view: all checks passed\n");
        return 0;
    }
    std::printf("test_modulation_pattern_view: %d check(s) failed\n", failures);
    return 1;
}
