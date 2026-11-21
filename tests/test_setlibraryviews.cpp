// separate-velocity-set-gate-set-and-note-set task 6.3 -- set view slot navigation, overlays and
// the PatternsView set-reference rows.
#include <cstring>
#include "framework.h"
#include "helpers.h"
#include "stubs/command_stub.h"
#include "stubs/hardware_stub.h"
#include "ui/views/GateSetView.h"
#include "ui/views/PatternsView.h"
#include "ui/views/PitchSetView.h"
#include "ui/views/VelocitySetView.h"
#include "ui/state/Reducer.h"
#include "ui/state/SetLibrary.h"
#include "ui/state/UIState.h"
#include "ui/Event.h"

using namespace ui;

namespace {

constexpr uint32_t kOccupied = 0x00200000;
constexpr uint32_t kSelected = 0x00FFFFFF;
constexpr uint32_t kUser = 0x0000FF00;

events::Event press(KeyId id, uint8_t mods = mod::NONE) {
    return events::Event::keyPressed(id, mods, 0);
}

// Patterns 0 and 1 share every slot-0 set; pattern 2 owns slot 1 of each library.
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

}  // namespace

TEST(setview_ctrl_arrows_step_the_selected_slot) {
    hardware::LedMatrix m{0};
    GateSetView view{m};
    state::UIState s = sharedState();
    s.currentView = state::ViewId::GATE_SET;
    state::beginGateSetEdit(s);

    s = test::handle(view, s, press(KeyId::RIGHT, mod::CTRL));
    CHECK_EQ(s.selectedGateSlot, 1);
    s = test::handle(view, s, press(KeyId::RIGHT, mod::CTRL));
    CHECK_EQ(s.selectedGateSlot, 0);
    s = test::handle(view, s, press(KeyId::LEFT, mod::CTRL));
    CHECK_EQ(s.selectedGateSlot, 1);
    // plain arrows still navigate properties
    s = test::handle(view, s, press(KeyId::RIGHT));
    CHECK(s.selectedGateSetProperty == state::GateSetProperty::STEPS);
}

TEST(setview_library_keys_create_copy_and_delete) {
    hardware::LedMatrix m{0};
    PitchSetView view{m};
    state::UIState s = sharedState();
    s.currentView = state::ViewId::PITCH_SET;
    state::beginPitchSetEdit(s);

    s = test::handle(view, s, press(KeyId::N, mod::CTRL));
    CHECK_EQ(s.selectedNoteSlot, 2);
    CHECK_EQ(s.noteLib.occupied, 0x0007);

    s = test::handle(view, s, press(KeyId::D, mod::CTRL));
    CHECK_EQ(s.selectedNoteSlot, 3);

    s = test::handle(view, s, press(KeyId::DELETE_KEY, mod::CTRL));  // slot 3: unused -> deleted
    CHECK_EQ(s.noteLib.occupied, 0x0007);

    s.selectedNoteSlot = 0;  // used by patterns 0 and 1
    state::beginPitchSetEdit(s);
    s = test::handle(view, s, press(KeyId::DELETE_KEY, mod::CTRL));
    CHECK(s.setNotice == state::SetNotice::IN_USE);
    CHECK_EQ(s.noteLib.occupied, 0x0007);
}

TEST(setview_notice_renders_and_clears_on_next_key) {
    hardware::LedMatrix m{0};
    VelocitySetView view{m};
    state::UIState s = sharedState();
    s.currentView = state::ViewId::VELOCITY_SET;
    s.selectedVelocitySlot = 0;
    state::beginVelocitySetEdit(s);

    s = test::handle(view, s, press(KeyId::DELETE_KEY, mod::CTRL));
    hardware::testing::resetMatrix();
    view.render(s);
    CHECK_STREQ(hardware::testing::lastValueText(), "use");

    s = test::handle(view, s, press(KeyId::RIGHT));
    CHECK(s.setNotice == state::SetNotice::NONE);
    hardware::testing::resetMatrix();
    view.render(s);
    CHECK(std::strcmp(hardware::testing::lastValueText(), "use") != 0);
}

TEST(setview_overlay_shows_slot_map_and_user_pixels) {
    hardware::LedMatrix m{0};
    GateSetView view{m};
    state::UIState s = sharedState();
    s.currentView = state::ViewId::GATE_SET;
    s.selectedGateSlot = 0;
    state::beginGateSetEdit(s);

    hardware::testing::resetMatrix();
    view.render(s);
    // row 15: slot 0 selected, slot 1 occupied, slot 2 free
    CHECK_EQ(hardware::testing::pixelAt(0, 15), kSelected);
    CHECK_EQ(hardware::testing::pixelAt(1, 15), kOccupied);
    CHECK_EQ(hardware::testing::pixelAt(2, 15), 0);
    // row 14: slot 0 is used by patterns 0 and 1 (shared -> two lit pixels), not pattern 2
    CHECK_EQ(hardware::testing::pixelAt(0, 14), kUser);
    CHECK_EQ(hardware::testing::pixelAt(1, 14), kUser);
    CHECK_EQ(hardware::testing::pixelAt(2, 14), 0);

    s.selectedGateSlot = 1;
    state::beginGateSetEdit(s);
    hardware::testing::resetMatrix();
    view.render(s);
    CHECK_EQ(hardware::testing::pixelAt(1, 15), kSelected);
    CHECK_EQ(hardware::testing::pixelAt(0, 14), 0);
    CHECK_EQ(hardware::testing::pixelAt(2, 14), kUser);  // only pattern 2
}

TEST(setview_picker_replaces_the_display_with_the_candidate_list) {
    hardware::LedMatrix m{0};
    GateSetView view{m};
    state::UIState s = sharedState();
    s.currentView = state::ViewId::GATE_SET;
    s.selectedGateSlot = 0;
    state::beginGateSetEdit(s);
    s = test::reduced(s, press(KeyId::G, mod::CTRL), nullptr);
    CHECK(s.gotoPicker.active);

    hardware::testing::resetMatrix();
    view.render(s);
    CHECK_STREQ(hardware::testing::lastLabel(), "gto");
    CHECK_EQ(hardware::testing::lastNumber(), 0);
    CHECK_EQ(hardware::testing::pixelAt(0, 14), kSelected);  // cursor
    CHECK(hardware::testing::pixelAt(1, 14) != 0);           // other candidate
    CHECK_EQ(hardware::testing::pixelAt(2, 14), 0);          // not a candidate
    CHECK_EQ(hardware::testing::pixelAt(0, 15), 0);          // slot map hidden
}

TEST(patternsview_shift_arrows_step_the_selected_reference) {
    hardware::LedMatrix m{0};
    PatternsView view{m};
    commands::testing::reset();
    state::UIState s = sharedState();
    s.selectedPattern = 1;
    s.selectedPatternSet = state::PatternSet::PITCH;

    s = test::handle(view, s, press(KeyId::RIGHT, mod::SHIFT));
    CHECK_EQ(s.patternRefs[1].note, 1);
    CHECK_EQ(s.patternRefs[1].gate, 0);
    CHECK_EQ(s.patternRefs[0].note, 0);
    CHECK_EQ(commands::testing::countOf(commands::Command::PATTERN_PITCH_SET), 1);

    s = test::handle(view, s, press(KeyId::LEFT, mod::SHIFT));
    CHECK_EQ(s.patternRefs[1].note, 0);

    s.selectedPatternSet = state::PatternSet::VELOCITY;
    s = test::handle(view, s, press(KeyId::RIGHT, mod::SHIFT));
    CHECK_EQ(s.patternRefs[1].velocity, 1);
}

TEST(patternsview_shift_arrows_ignore_the_add_slot) {
    hardware::LedMatrix m{0};
    PatternsView view{m};
    state::UIState s = sharedState();
    s.selectedPattern = 3;  // the add slot
    s = test::handle(view, s, press(KeyId::RIGHT, mod::SHIFT));
    CHECK_EQ(s.patternRefs[0].gate, 0);
}

TEST(patternsview_renders_reference_maps_for_the_selected_pattern) {
    hardware::LedMatrix m{0};
    PatternsView view{m};
    state::UIState s = sharedState();
    s.selectedPattern = 2;  // references slot 1 of every library

    hardware::testing::resetMatrix();
    view.render(s);
    for (uint8_t row = 13; row <= 15; ++row) {
        CHECK_EQ(hardware::testing::pixelAt(0, row), kOccupied);
        CHECK_EQ(hardware::testing::pixelAt(1, row), kSelected);
        CHECK_EQ(hardware::testing::pixelAt(2, row), 0);
    }

    s.selectedPattern = 0;
    hardware::testing::resetMatrix();
    view.render(s);
    CHECK_EQ(hardware::testing::pixelAt(0, 13), kSelected);
    CHECK_EQ(hardware::testing::pixelAt(1, 13), kOccupied);
}

TEST(patternsview_enter_opens_the_set_the_pattern_references) {
    hardware::LedMatrix m{0};
    PatternsView view{m};
    state::UIState s = sharedState();
    s.selectedPattern = 2;
    s.selectedPatternSet = state::PatternSet::PITCH;
    s.noteLib.slots[1].pitches[0] = 77;

    s = test::handle(view, s, press(KeyId::ENTER));
    CHECK(s.currentView == state::ViewId::PITCH_SET);
    CHECK_EQ(s.selectedNoteSlot, 1);
    CHECK_EQ(s.pitchSetDraft.pitches[0], 77);
}
