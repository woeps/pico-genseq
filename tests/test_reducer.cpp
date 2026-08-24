#include "framework.h"
#include "stubs/command_stub.h"
#include "ui/state/Reducer.h"
#include "ui/state/UIState.h"
#include "ui/views/IView.h"
#include "ui/Event.h"

using namespace ui;

namespace {

// Records what reached the view and marks the state so delegation is visible.
class FakeView : public IView {
public:
    int eventsSeen = 0;
    KeyId lastKey = KeyId::NONE;

    state::UIState handleEvent(const state::UIState& state, const events::Event& event) override {
        ++eventsSeen;
        lastKey = event.data.key.id;
        state::UIState next = state;
        next.value = 42;            // sentinel: the view ran
        return next;
    }

    void render(const state::UIState&) override {}
};

events::Event press(KeyId id, uint8_t mods = mod::NONE) {
    return events::Event::keyPressed(id, mods, 0);
}

} // namespace

TEST(reduce_f1_switches_to_init_view) {
    FakeView view;
    state::UIState s;
    s.currentView = state::ViewId::SETTINGS;
    const auto next = state::reduce(s, press(KeyId::F1), &view);
    CHECK(next.currentView == state::ViewId::INIT);
    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_f2_switches_to_settings_view) {
    FakeView view;
    const auto next = state::reduce(state::UIState{}, press(KeyId::F2), &view);
    CHECK(next.currentView == state::ViewId::SETTINGS);
    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_f3_switches_to_patterns_view) {
    FakeView view;
    const auto next = state::reduce(state::UIState{}, press(KeyId::F3), &view);
    CHECK(next.currentView == state::ViewId::PATTERNS);
    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_consumes_unbound_function_keys_without_effect) {
    FakeView view;
    state::UIState s;
    s.currentView = state::ViewId::SETTINGS;
    for (uint8_t usage = static_cast<uint8_t>(KeyId::F4);
         usage <= static_cast<uint8_t>(KeyId::F12); ++usage) {
        const auto next = state::reduce(s, press(static_cast<KeyId>(usage)), &view);
        CHECK(next.currentView == state::ViewId::SETTINGS);
    }
    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_reserves_function_keys_regardless_of_modifiers) {
    FakeView view;
    state::UIState s;
    s.currentView = state::ViewId::SETTINGS;
    const auto next = state::reduce(s, press(KeyId::F1, mod::SHIFT), &view);
    CHECK(next.currentView == state::ViewId::SETTINGS);   // modified: consumed, no effect
    CHECK_EQ(view.eventsSeen, 0);                          // and never reaches the view
}

TEST(reduce_space_toggles_playing_and_sends_commands) {
    FakeView view;
    commands::testing::reset();

    state::UIState stopped;
    stopped.playing = false;
    const auto started = state::reduce(stopped, press(KeyId::SPACE), &view);
    CHECK(started.playing);

    const auto stoppedAgain = state::reduce(started, press(KeyId::SPACE), &view);
    CHECK(!stoppedAgain.playing);

    auto& cmds = commands::testing::sentCommands();
    CHECK_EQ(cmds.size(), 2);
    if (cmds.size() >= 2) {
        CHECK(cmds[0].cmd == commands::Command::PLAY);
        CHECK(cmds[1].cmd == commands::Command::STOP);
    }
    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_absorbs_release_and_repeat_of_reserved_keys) {
    FakeView view;
    state::UIState s;
    s.playing = false;

    const auto afterRelease = state::reduce(s, events::Event::keyReleased(KeyId::SPACE, mod::NONE, 0), &view);
    CHECK(!afterRelease.playing);

    const auto afterHeld = state::reduce(s, events::Event::keyHeld(KeyId::SPACE, mod::NONE, 0), &view);
    CHECK(!afterHeld.playing);

    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_delegates_unreserved_keys_to_the_active_view) {
    FakeView view;
    const auto next = state::reduce(state::UIState{}, press(KeyId::UP), &view);
    CHECK_EQ(next.value, 42);
    CHECK_EQ(view.eventsSeen, 1);
    CHECK(view.lastKey == KeyId::UP);
}

TEST(reduce_returns_state_unchanged_without_an_active_view) {
    state::UIState s;
    s.value = 7;
    const auto next = state::reduce(s, press(KeyId::UP), nullptr);
    CHECK_EQ(next.value, 7);
}
