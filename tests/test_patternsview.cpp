#include "framework.h"
#include "stubs/command_stub.h"
#include "stubs/hardware_stub.h"
#include "ui/views/PatternsView.h"
#include "ui/state/UIState.h"
#include "ui/Event.h"

using namespace ui;

namespace {

struct Fixture {
    hardware::LedMatrix matrix{0};
    PatternsView view{matrix};
};

events::Event press(KeyId id, uint8_t mods = mod::NONE) {
    return events::Event::keyPressed(id, mods, 0);
}

}

TEST(patternsview_plain_arrows_select_sets_spatially) {
    Fixture f;
    state::UIState s;

    auto pitch = f.view.handleEvent(s, press(KeyId::RIGHT));
    CHECK(pitch.selectedPatternSet == state::PatternSet::PITCH);
    CHECK(f.view.handleEvent(pitch, press(KeyId::DOWN)).selectedPatternSet == state::PatternSet::PITCH);

    auto gate = f.view.handleEvent(pitch, press(KeyId::LEFT));
    CHECK(gate.selectedPatternSet == state::PatternSet::GATE);

    auto velocity = f.view.handleEvent(gate, press(KeyId::DOWN));
    CHECK(velocity.selectedPatternSet == state::PatternSet::VELOCITY);
    CHECK(f.view.handleEvent(velocity, press(KeyId::RIGHT)).selectedPatternSet == state::PatternSet::VELOCITY);
    CHECK(f.view.handleEvent(velocity, press(KeyId::UP)).selectedPatternSet == state::PatternSet::GATE);
}

TEST(patternsview_ctrl_arrows_navigate_pattern_grid) {
    Fixture f;
    state::UIState s;
    s.patternCount = 8;
    s.selectedPattern = 6;

    CHECK_EQ(f.view.handleEvent(s, press(KeyId::UP, mod::CTRL)).selectedPattern, 1);
    CHECK_EQ(f.view.handleEvent(s, press(KeyId::DOWN, mod::CTRL)).selectedPattern, 8);
    CHECK_EQ(f.view.handleEvent(s, press(KeyId::LEFT, mod::CTRL)).selectedPattern, 5);
    CHECK_EQ(f.view.handleEvent(s, press(KeyId::RIGHT, mod::CTRL)).selectedPattern, 7);
}

TEST(patternsview_ctrl_navigation_obeys_bounds_and_full_capacity) {
    Fixture f;
    state::UIState s;
    s.patternCount = 15;
    s.selectedPattern = 14;

    CHECK_EQ(f.view.handleEvent(s, press(KeyId::RIGHT, mod::CTRL)).selectedPattern, 14);
    CHECK_EQ(f.view.handleEvent(s, press(KeyId::DOWN, mod::CTRL)).selectedPattern, 14);

    s.selectedPattern = 0;
    CHECK_EQ(f.view.handleEvent(s, press(KeyId::LEFT, mod::CTRL)).selectedPattern, 0);
    CHECK_EQ(f.view.handleEvent(s, press(KeyId::UP, mod::CTRL)).selectedPattern, 0);
}

TEST(patternsview_enter_routes_selected_set_to_its_detail_view) {
    Fixture f;
    state::UIState s;

    s.selectedPatternSet = state::PatternSet::GATE;
    CHECK(f.view.handleEvent(s, press(KeyId::ENTER)).currentView == state::ViewId::GATE_SET);
    s.selectedPatternSet = state::PatternSet::PITCH;
    CHECK(f.view.handleEvent(s, press(KeyId::ENTER)).currentView == state::ViewId::PITCH_SET);
    s.selectedPatternSet = state::PatternSet::VELOCITY;
    CHECK(f.view.handleEvent(s, press(KeyId::ENTER)).currentView == state::ViewId::VELOCITY_SET);
}

TEST(patternsview_enter_adds_when_add_slot_is_selected) {
    Fixture f;
    state::UIState s;
    s.patternCount = 3;
    s.selectedPattern = 3;

    const auto next = f.view.handleEvent(s, press(KeyId::ENTER));

    CHECK_EQ(next.patternCount, 4);
    CHECK_EQ(next.selectedPattern, 3);
    CHECK(next.currentView == state::ViewId::INIT);
}

TEST(patternsview_delete_removes_selected_real_pattern) {
    Fixture f;
    commands::testing::reset();
    state::UIState s;
    s.patternCount = 3;
    s.activePatterns = 0b111;
    s.selectedPattern = 1;

    const auto next = f.view.handleEvent(s, press(KeyId::DELETE_KEY));

    CHECK_EQ(next.patternCount, 2);
    CHECK_EQ(next.selectedPattern, 1);
    CHECK_EQ(commands::testing::sentCommands().size(), 1);
}

TEST(patternsview_delete_ignores_add_slot_and_key_repeat) {
    Fixture f;
    commands::testing::reset();
    state::UIState s;
    s.patternCount = 3;
    s.selectedPattern = 3;

    CHECK_EQ(f.view.handleEvent(s, press(KeyId::DELETE_KEY)).patternCount, 3);

    s.selectedPattern = 1;
    const auto held = events::Event::keyHeld(KeyId::DELETE_KEY, mod::NONE, 0);
    CHECK_EQ(f.view.handleEvent(s, held).patternCount, 3);
    CHECK_EQ(commands::testing::sentCommands().size(), 0);
}

TEST(patternsview_plain_arrows_do_not_select_sets_on_add_slot) {
    Fixture f;
    state::UIState s;
    s.patternCount = 2;
    s.selectedPattern = 2;

    CHECK(f.view.handleEvent(s, press(KeyId::RIGHT)).selectedPatternSet == state::PatternSet::GATE);
    CHECK(f.view.handleEvent(s, press(KeyId::DOWN)).selectedPatternSet == state::PatternSet::GATE);
}

TEST(patternsview_renders_all_four_pixels_but_highlights_only_selected_sets) {
    Fixture f;
    hardware::testing::resetMatrix();
    state::UIState s;
    s.patternCount = 1;
    s.activePatterns = 1;
    s.selectedPattern = 0;
    s.selectedPatternSet = state::PatternSet::VELOCITY;

    f.view.render(s);

    CHECK_EQ(hardware::testing::pixelAt(0, 5), 0x0000FF00);
    CHECK_EQ(hardware::testing::pixelAt(1, 5), 0x0000FF00);
    CHECK_EQ(hardware::testing::pixelAt(0, 6), 0x00FFFFFF);
    CHECK_EQ(hardware::testing::pixelAt(1, 6), 0x0000FF00);
}
