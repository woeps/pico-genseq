#include "framework.h"
#include "ui/views/InitView.h"
#include "ui/state/UIState.h"
#include "ui/Event.h"

using namespace ui;

namespace {

struct Fixture {
    hardware::Led led{0};
    hardware::LedMatrix matrix{0};
    InitView view{led, matrix};
};

state::UIState stateWithValue(int value) {
    state::UIState s;
    s.value = value;
    return s;
}

events::Event press(KeyId id, uint8_t mods = mod::NONE) {
    return events::Event::keyPressed(id, mods, 0);
}

} // namespace

TEST(initview_up_increments_by_one) {
    Fixture f;
    const auto next = f.view.handleEvent(stateWithValue(5), press(KeyId::UP));
    CHECK_EQ(next.value, 6);
}

TEST(initview_down_decrements_by_one) {
    Fixture f;
    const auto next = f.view.handleEvent(stateWithValue(5), press(KeyId::DOWN));
    CHECK_EQ(next.value, 4);
}

TEST(initview_shift_makes_steps_coarse) {
    Fixture f;
    CHECK_EQ(f.view.handleEvent(stateWithValue(50), press(KeyId::UP, mod::SHIFT)).value, 60);
    CHECK_EQ(f.view.handleEvent(stateWithValue(50), press(KeyId::DOWN, mod::SHIFT)).value, 40);
}

TEST(initview_treats_key_held_like_key_pressed) {
    Fixture f;
    const auto held = events::Event::keyHeld(KeyId::UP, mod::NONE, 0);
    CHECK_EQ(f.view.handleEvent(stateWithValue(5), held).value, 6);
}

TEST(initview_ignores_key_released) {
    Fixture f;
    const auto released = events::Event::keyReleased(KeyId::UP, mod::NONE, 0);
    CHECK_EQ(f.view.handleEvent(stateWithValue(5), released).value, 5);
}

TEST(initview_clamps_at_both_ends) {
    Fixture f;
    CHECK_EQ(f.view.handleEvent(stateWithValue(95), press(KeyId::UP, mod::SHIFT)).value, 99);
    CHECK_EQ(f.view.handleEvent(stateWithValue(99), press(KeyId::UP)).value, 99);
    CHECK_EQ(f.view.handleEvent(stateWithValue(5),  press(KeyId::DOWN, mod::SHIFT)).value, 0);
    CHECK_EQ(f.view.handleEvent(stateWithValue(0),  press(KeyId::DOWN)).value, 0);
}

TEST(initview_ignores_unbound_keys) {
    Fixture f;
    CHECK_EQ(f.view.handleEvent(stateWithValue(5), press(KeyId::LEFT)).value, 5);
    CHECK_EQ(f.view.handleEvent(stateWithValue(5), press(KeyId::A)).value, 5);
}
