// separate-velocity-set-gate-set-and-note-set task 4.5 -- reducer fan-out and library actions.
#include "framework.h"
#include "helpers.h"
#include "stubs/command_stub.h"
#include "ui/state/Reducer.h"
#include "ui/state/SetLibrary.h"
#include "ui/state/UIState.h"

using namespace ui;
using commands::Command;

namespace {

// Three patterns: 0 and 1 share every slot-0 set, pattern 2 owns gate/note/velocity slot 1.
state::UIState sharedState() {
    state::UIState s;
    s.patternCount = 3;
    uint8_t slot = 0;
    state::createSet(s, state::SetType::GATE, slot);
    state::createSet(s, state::SetType::NOTE, slot);
    state::createSet(s, state::SetType::VELOCITY, slot);
    s.patternRefs[2] = state::PatternRefs{1, 1, 1};
    return s;
}

// Index of the first command of `cmd` at or after `from`, or -1.
int indexOf(Command cmd, size_t from = 0) {
    const auto& v = commands::testing::sentCommands();
    for (size_t i = from; i < v.size(); ++i) if (v[i].cmd == cmd) return static_cast<int>(i);
    return -1;
}

}  // namespace

TEST(edit_of_shared_gate_set_reaches_all_users_in_one_batch) {
    state::UIState s = sharedState();
    s.selectedGateSlot = 0;
    state::beginGateSetEdit(s);
    s.selectedGateSetProperty = state::GateSetProperty::PULSES;
    commands::testing::reset();

    state::adjustGateSetValue(s, 1);

    const auto& v = commands::testing::sentCommands();
    CHECK_EQ(commands::testing::countOf(Command::PATTERN_GATE_SET), 2);
    CHECK_EQ(commands::testing::countOf(Command::BATCH_BEGIN), 1);
    CHECK_EQ(commands::testing::countOf(Command::BATCH_END), 1);
    const int begin = indexOf(Command::BATCH_BEGIN);
    const int end = indexOf(Command::BATCH_END);
    CHECK(begin >= 0 && end > begin);
    if (begin >= 0) CHECK_EQ(v[begin].param1, 2);
    const int g0 = indexOf(Command::PATTERN_GATE_SET);
    const int g1 = indexOf(Command::PATTERN_GATE_SET, g0 + 1);
    CHECK(g0 > begin && g1 > g0 && g1 < end);
    if (g1 > 0) {
        CHECK_EQ(v[g0].param1, 0);
        CHECK_EQ(v[g1].param1, 1);
    }
}

TEST(edit_of_unshared_set_is_not_framed) {
    state::UIState s = sharedState();
    s.selectedGateSlot = 1;  // used by pattern 2 only
    state::beginGateSetEdit(s);
    s.selectedGateSetProperty = state::GateSetProperty::PULSES;
    commands::testing::reset();

    state::adjustGateSetValue(s, 1);

    CHECK_EQ(commands::testing::countOf(Command::PATTERN_GATE_SET), 1);
    CHECK_EQ(commands::testing::countOf(Command::BATCH_BEGIN), 0);
    CHECK_EQ(commands::testing::countOf(Command::BATCH_END), 0);
    CHECK_EQ(commands::testing::sentCommands()[0].param1, 2);
}

TEST(edit_of_unused_set_sends_nothing) {
    state::UIState s = sharedState();
    uint8_t slot = 0;
    state::createSet(s, state::SetType::NOTE, slot);  // slot 2, no users
    s.selectedNoteSlot = slot;
    state::beginPitchSetEdit(s);
    s.selectedPitchSetField = state::PITCH_SET_FIELD_PITCH_BASE;
    commands::testing::reset();

    state::adjustPitchSetValue(s, 1);

    CHECK(s.pitchSetDirty);
    CHECK_EQ(commands::testing::sentCommands().size(), 0);
}

TEST(commit_stores_into_the_slot_and_sends_gen_params_to_every_user) {
    state::UIState s = sharedState();
    s.selectedVelocitySlot = 0;
    state::beginVelocitySetEdit(s);
    s.selectedVelocitySetField = state::VELOCITY_SET_FIELD_VELOCITY_BASE;
    state::adjustVelocitySetValue(s, 5);
    commands::testing::reset();

    state::commitVelocitySetEdit(s);

    CHECK(!s.velocitySetDirty);
    CHECK_EQ(s.velocityLib.slots[0].velocities[0], 105);
    CHECK_EQ(s.velocitySetOf(1).velocities[0], 105);  // pattern 1 shares the slot
    CHECK_EQ(s.velocitySetOf(2).velocities[0], 100);  // pattern 2 owns another slot
    CHECK_EQ(commands::testing::countOf(Command::GEN_PARAMS_SET), 2);
    CHECK_EQ(commands::testing::countOf(Command::BATCH_BEGIN), 1);
}

TEST(selecting_another_slot_discards_the_draft_and_resends_the_committed_set) {
    state::UIState s = sharedState();
    s.selectedNoteSlot = 0;
    state::beginPitchSetEdit(s);
    s.selectedPitchSetField = state::PITCH_SET_FIELD_PITCH_BASE;
    state::adjustPitchSetValue(s, 3);
    CHECK(s.pitchSetDirty);
    commands::testing::reset();

    state::selectSetSlot(s, state::SetType::NOTE, 1);

    CHECK(!s.pitchSetDirty);
    CHECK_EQ(s.selectedNoteSlot, 1);
    CHECK_EQ(s.pitchSetDraft.pitches[0], 60);
    CHECK_EQ(s.noteLib.slots[0].pitches[0], 60);  // committed set untouched by the draft
    // the previewed value was undone for the users of the old slot (patterns 0 and 1)
    CHECK_EQ(commands::testing::countOf(Command::PATTERN_PITCH_SET), 2);
}

TEST(selecting_a_free_slot_is_ignored) {
    state::UIState s = sharedState();
    s.selectedGateSlot = 0;
    state::selectSetSlot(s, state::SetType::GATE, 9);
    CHECK_EQ(s.selectedGateSlot, 0);
}

TEST(step_slot_wraps_over_occupied_slots) {
    state::UIState s = sharedState();  // gate slots 0 and 1 occupied
    s.selectedGateSlot = 0;
    state::stepSetSlot(s, state::SetType::GATE, +1);
    CHECK_EQ(s.selectedGateSlot, 1);
    state::stepSetSlot(s, state::SetType::GATE, +1);
    CHECK_EQ(s.selectedGateSlot, 0);
    state::stepSetSlot(s, state::SetType::GATE, -1);
    CHECK_EQ(s.selectedGateSlot, 1);
}

TEST(create_selects_the_new_set_and_refuses_when_full) {
    state::UIState s;
    state::createSetInView(s, state::SetType::GATE);
    CHECK_EQ(s.selectedGateSlot, 1);
    uint8_t slot = 0;
    while (state::createSet(s, state::SetType::GATE, slot)) {}
    const uint16_t occ = s.gateLib.occupied;
    state::createSetInView(s, state::SetType::GATE);
    CHECK(s.setNotice == state::SetNotice::LIBRARY_FULL);
    CHECK_EQ(s.gateLib.occupied, occ);
    state::copySetInView(s, state::SetType::GATE);
    CHECK(s.setNotice == state::SetNotice::LIBRARY_FULL);
}

TEST(copy_selects_a_new_set_with_the_source_contents) {
    state::UIState s = sharedState();
    s.noteLib.slots[0].pitches[0] = 55;
    s.selectedNoteSlot = 0;
    state::copySetInView(s, state::SetType::NOTE);
    CHECK_EQ(s.selectedNoteSlot, 2);
    CHECK_EQ(s.noteLib.slots[2].pitches[0], 55);
    CHECK_EQ(s.pitchSetDraft.pitches[0], 55);
    CHECK_EQ(s.patternRefs[0].note, 0);  // no reference changed
}

TEST(delete_refuses_in_use_and_last_then_succeeds_for_an_unused_set) {
    state::UIState s = sharedState();
    s.selectedGateSlot = 0;  // used by patterns 0 and 1
    state::deleteSetInView(s, state::SetType::GATE);
    CHECK(s.setNotice == state::SetNotice::IN_USE);
    CHECK_EQ(s.gateLib.occupied, 0x0003);

    s.setNotice = state::SetNotice::NONE;
    uint8_t slot = 0;
    state::createSet(s, state::SetType::GATE, slot);  // slot 2, unused
    s.selectedGateSlot = slot;
    state::deleteSetInView(s, state::SetType::GATE);
    CHECK(s.setNotice == state::SetNotice::NONE);
    CHECK_EQ(s.gateLib.occupied, 0x0003);
    CHECK(s.selectedGateSlot == 0 || s.selectedGateSlot == 1);

    state::UIState single;
    single.patternCount = 0;  // no users, so only the last-set rule applies
    state::deleteSetInView(single, state::SetType::VELOCITY);
    CHECK(single.setNotice == state::SetNotice::LAST_SET);
    CHECK_EQ(single.velocityLib.occupied, 0x0001);
}

TEST(assign_ref_sends_the_new_set_and_gen_params_as_one_batch) {
    state::UIState s = sharedState();
    commands::testing::reset();

    state::assignSetRef(s, 1, state::SetType::NOTE, 1);

    CHECK_EQ(s.patternRefs[1].note, 1);
    CHECK_EQ(s.patternRefs[0].note, 0);
    const auto& v = commands::testing::sentCommands();
    CHECK_EQ(v.size(), 4);  // BEGIN, PITCH_SET, GEN_PARAMS_SET, END
    if (v.size() == 4) {
        CHECK(v[0].cmd == Command::BATCH_BEGIN);
        CHECK_EQ(v[0].param1, 2);
        CHECK(v[1].cmd == Command::PATTERN_PITCH_SET);
        CHECK_EQ(v[1].param1, 1);
        CHECK(v[2].cmd == Command::GEN_PARAMS_SET);
        CHECK_EQ(v[2].param1, 1);
        CHECK(v[3].cmd == Command::BATCH_END);
    }
}

TEST(assign_ref_rejects_a_free_slot_and_sends_nothing) {
    state::UIState s = sharedState();
    commands::testing::reset();
    state::assignSetRef(s, 1, state::SetType::GATE, 7);
    CHECK_EQ(s.patternRefs[1].gate, 0);
    CHECK_EQ(commands::testing::sentCommands().size(), 0);
}

TEST(step_ref_moves_to_next_occupied_slot_with_wrap) {
    state::UIState s = sharedState();
    state::stepSetRef(s, 0, state::SetType::VELOCITY, +1);
    CHECK_EQ(s.patternRefs[0].velocity, 1);
    state::stepSetRef(s, 0, state::SetType::VELOCITY, +1);
    CHECK_EQ(s.patternRefs[0].velocity, 0);
}

TEST(select_slots_of_selected_pattern_follows_its_references) {
    state::UIState s = sharedState();
    s.selectedPattern = 2;
    state::selectSlotsOfSelectedPattern(s);
    CHECK_EQ(s.selectedGateSlot, 1);
    CHECK_EQ(s.selectedNoteSlot, 1);
    CHECK_EQ(s.selectedVelocitySlot, 1);
}

TEST(add_pattern_sends_gen_params_for_its_referenced_sets) {
    commands::testing::reset();
    state::UIState s;
    state::addPattern(s);
    CHECK_EQ(commands::testing::countOf(Command::GEN_PARAMS_SET), 1);
}

// --- Go to pattern (task 5) ---------------------------------------------------

namespace {

ui::events::Event key(ui::KeyId id, uint8_t mods = ui::mod::NONE) {
    return ui::events::Event::keyPressed(id, mods, 0);
}

// Patterns 0..3; 0, 1 and 3 share gate slot 0, pattern 2 uses gate slot 1.
state::UIState gotoState() {
    state::UIState s;
    s.patternCount = 4;
    uint8_t slot = 0;
    state::createSet(s, state::SetType::GATE, slot);
    s.patternRefs[2].gate = 1;
    s.currentView = state::ViewId::GATE_SET;
    s.selectedGateSlot = 0;
    state::beginGateSetEdit(s);
    return s;
}

}  // namespace

TEST(goto_with_one_user_jumps_straight_to_that_pattern) {
    state::UIState s = gotoState();
    s.selectedGateSlot = 1;  // only pattern 2
    state::beginGateSetEdit(s);
    s = test::reduced(s, key(KeyId::G, mod::CTRL), nullptr);
    CHECK(s.currentView == state::ViewId::PATTERNS);
    CHECK_EQ(s.selectedPattern, 2);
    CHECK(!s.gotoPicker.active);
}

TEST(goto_with_no_users_shows_unused_and_stays) {
    state::UIState s = gotoState();
    uint8_t slot = 0;
    state::createSet(s, state::SetType::GATE, slot);  // slot 2, unused
    s.selectedGateSlot = slot;
    state::beginGateSetEdit(s);
    s = test::reduced(s, key(KeyId::G, mod::CTRL), nullptr);
    CHECK(s.currentView == state::ViewId::GATE_SET);
    CHECK(s.setNotice == state::SetNotice::UNUSED);
    CHECK(!s.gotoPicker.active);
}

TEST(goto_with_several_users_opens_picker_on_the_current_pattern_if_it_is_a_user) {
    state::UIState s = gotoState();
    s.selectedPattern = 3;
    s = test::reduced(s, key(KeyId::G, mod::CTRL), nullptr);
    CHECK(s.gotoPicker.active);
    CHECK_EQ(s.gotoPicker.candidates, 0b1011);
    CHECK_EQ(s.gotoPicker.cursor, 3);
    CHECK(s.currentView == state::ViewId::GATE_SET);
}

TEST(goto_picker_starts_on_lowest_user_when_current_pattern_is_not_a_user) {
    state::UIState s = gotoState();
    s.selectedPattern = 2;  // uses slot 1, not slot 0
    s = test::reduced(s, key(KeyId::G, mod::CTRL), nullptr);
    CHECK(s.gotoPicker.active);
    CHECK_EQ(s.gotoPicker.cursor, 0);
}

TEST(goto_picker_moves_over_candidates_with_wrap_and_confirms) {
    state::UIState s = gotoState();
    s.selectedPattern = 0;
    s = test::reduced(s, key(KeyId::G, mod::CTRL), nullptr);
    s = test::reduced(s, key(KeyId::RIGHT), nullptr);
    CHECK_EQ(s.gotoPicker.cursor, 1);
    s = test::reduced(s, key(KeyId::RIGHT), nullptr);
    CHECK_EQ(s.gotoPicker.cursor, 3);  // pattern 2 is not a user
    s = test::reduced(s, key(KeyId::RIGHT), nullptr);
    CHECK_EQ(s.gotoPicker.cursor, 0);  // wraps
    s = test::reduced(s, key(KeyId::LEFT), nullptr);
    CHECK_EQ(s.gotoPicker.cursor, 3);

    s = test::reduced(s, key(KeyId::ENTER), nullptr);
    CHECK(s.currentView == state::ViewId::PATTERNS);
    CHECK_EQ(s.selectedPattern, 3);
    CHECK(!s.gotoPicker.active);
}

TEST(goto_cancel_keeps_slot_draft_and_view) {
    state::UIState s = gotoState();
    s.selectedGateSetProperty = state::GateSetProperty::PULSES;
    state::adjustGateSetValue(s, 1);
    CHECK(s.gateSetDirty);
    const state::GateSetConfig draft = s.gateSetDraft;
    s = test::reduced(s, key(KeyId::G, mod::CTRL), nullptr);
    CHECK(s.gotoPicker.active);
    s = test::reduced(s, key(KeyId::ESCAPE), nullptr);
    CHECK(!s.gotoPicker.active);
    CHECK(s.currentView == state::ViewId::GATE_SET);
    CHECK_EQ(s.selectedGateSlot, 0);
    CHECK(s.gateSetDirty);
    CHECK(s.gateSetDraft == draft);
}

TEST(goto_confirm_discards_the_uncommitted_draft) {
    state::UIState s = gotoState();
    s.selectedGateSetProperty = state::GateSetProperty::PULSES;
    state::adjustGateSetValue(s, 1);
    const uint8_t committedPulses = s.gateLib.slots[0].pulses;
    s = test::reduced(s, key(KeyId::G, mod::CTRL), nullptr);
    s = test::reduced(s, key(KeyId::ENTER), nullptr);
    CHECK(s.currentView == state::ViewId::PATTERNS);
    CHECK(!s.gateSetDirty);
    CHECK_EQ(s.gateLib.slots[0].pulses, committedPulses);
    CHECK_EQ(s.gateSetDraft.pulses, committedPulses);
}

TEST(goto_picker_consumes_editing_keys) {
    state::UIState s = gotoState();
    s.selectedGateSetProperty = state::GateSetProperty::PULSES;
    s = test::reduced(s, key(KeyId::G, mod::CTRL), nullptr);
    const state::GateSetConfig before = s.gateSetDraft;
    s = test::reduced(s, key(KeyId::UP, mod::SHIFT), nullptr);
    s = test::reduced(s, key(KeyId::S), nullptr);
    CHECK(s.gateSetDraft == before);
    CHECK(s.gotoPicker.active);
}

TEST(goto_picker_closes_on_function_key_view_switch) {
    state::UIState s = gotoState();
    s = test::reduced(s, key(KeyId::G, mod::CTRL), nullptr);
    CHECK(s.gotoPicker.active);
    s = test::reduced(s, key(KeyId::F3), nullptr);
    CHECK(!s.gotoPicker.active);
    CHECK(s.currentView == state::ViewId::PATTERNS);
}

TEST(ctrl_g_outside_set_views_does_nothing) {
    state::UIState s = gotoState();
    s.currentView = state::ViewId::PATTERNS;
    s = test::reduced(s, key(KeyId::G, mod::CTRL), nullptr);
    CHECK(!s.gotoPicker.active);
    CHECK(s.currentView == state::ViewId::PATTERNS);
}

TEST(goto_works_from_note_and_velocity_views) {
    state::UIState s = gotoState();
    s.currentView = state::ViewId::PITCH_SET;
    s.selectedNoteSlot = 0;
    s = test::reduced(s, key(KeyId::G, mod::CTRL), nullptr);
    CHECK(s.gotoPicker.active);
    CHECK(s.gotoPicker.type == state::SetType::NOTE);
    s = test::reduced(s, key(KeyId::ESCAPE), nullptr);

    s.currentView = state::ViewId::VELOCITY_SET;
    s = test::reduced(s, key(KeyId::G, mod::CTRL), nullptr);
    CHECK(s.gotoPicker.type == state::SetType::VELOCITY);
}
