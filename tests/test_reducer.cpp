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

TEST(remove_pattern_compacts_active_bits_and_sends_index) {
    commands::testing::reset();
    state::UIState s;
    s.patternCount = 4;
    s.activePatterns = 0b1101;
    s.selectedPattern = 1;

    state::removePattern(s, 1);

    CHECK_EQ(s.patternCount, 3);
    CHECK_EQ(s.activePatterns, 0b111);
    CHECK_EQ(s.selectedPattern, 1);
    auto& cmds = commands::testing::sentCommands();
    CHECK_EQ(cmds.size(), 1);
    if (!cmds.empty()) {
        CHECK(cmds[0].cmd == commands::Command::PATTERN_REMOVE);
        CHECK_EQ(cmds[0].param1, 1);
    }
}

TEST(remove_pattern_clamps_selection_after_removing_last_pattern) {
    state::UIState s;
    s.patternCount = 4;
    s.activePatterns = 0b1111;
    s.selectedPattern = 3;

    state::removePattern(s, 3);

    CHECK_EQ(s.patternCount, 3);
    CHECK_EQ(s.selectedPattern, 2);
}

TEST(remove_pattern_keeps_at_least_one_pattern) {
    commands::testing::reset();
    state::UIState s;

    state::removePattern(s, 0);

    CHECK_EQ(s.patternCount, 1);
    CHECK_EQ(s.activePatterns, 1);
    CHECK_EQ(commands::testing::sentCommands().size(), 0);
}

TEST(remove_pattern_shifts_a_later_selection_with_compacted_indices) {
    state::UIState s;
    s.patternCount = 5;
    s.selectedPattern = 4;

    state::removePattern(s, 1);

    CHECK_EQ(s.patternCount, 4);
    CHECK_EQ(s.selectedPattern, 3);
}

TEST(add_pattern_initializes_and_sends_default_gate_set) {
    commands::testing::reset();
    state::UIState s;
    s.patternCount = 2;
    s.gateSetConfigs[2].steps = 7;

    state::addPattern(s);

    CHECK_EQ(s.gateSetConfigs[2].steps, 16);
    CHECK_EQ(commands::testing::sentCommands().size(), 3);
    if (commands::testing::sentCommands().size() >= 3) {
        CHECK(commands::testing::sentCommands()[0].cmd == commands::Command::PATTERN_ADD);
        CHECK(commands::testing::sentCommands()[1].cmd == commands::Command::PATTERN_GATE_SET);
        CHECK_EQ(commands::testing::sentCommands()[1].param1, 2);
        CHECK_EQ(commands::testing::sentCommands()[1].gateCount, 96);
        CHECK(commands::testing::sentCommands()[2].cmd == commands::Command::PATTERN_PITCH_SET);
        CHECK_EQ(commands::testing::sentCommands()[2].param1, 2);
        CHECK_EQ(commands::testing::sentCommands()[2].pitchCount, 4);
    }
}

TEST(remove_pattern_compacts_gate_set_configs) {
    state::UIState s;
    s.patternCount = 3;
    s.gateSetConfigs[0].steps = 8;
    s.gateSetConfigs[1].steps = 12;
    s.gateSetConfigs[2].steps = 24;

    state::removePattern(s, 1);

    CHECK_EQ(s.gateSetConfigs[0].steps, 8);
    CHECK_EQ(s.gateSetConfigs[1].steps, 24);
    CHECK_EQ(s.gateSetConfigs[2].steps, 16);
}

TEST(sync_gate_set_supports_long_musical_cycles) {
    commands::testing::reset();
    state::UIState s;
    s.gateSetConfigs[0].steps = 64;
    s.gateSetConfigs[0].noteLength = state::NoteLength::WHOLE;

    state::syncGateSet(s, 0);

    CHECK_EQ(commands::testing::sentCommands().size(), 1);
    if (!commands::testing::sentCommands().empty()) {
        CHECK_EQ(commands::testing::sentCommands()[0].gateCount, 6144);
        CHECK_EQ(commands::testing::sentCommands()[0].gates.size(), 6144);
    }
}

TEST(leaving_dirty_gate_set_view_restores_committed_preview) {
    commands::testing::reset();
    state::UIState s;
    s.currentView = state::ViewId::GATE_SET;
    s.gateSetDraft.steps = 20;
    s.gateSetDirty = true;

    const auto next = state::reduce(s, press(KeyId::F1), nullptr);

    CHECK(next.currentView == state::ViewId::INIT);
    CHECK(!next.gateSetDirty);
    CHECK_EQ(next.gateSetDraft.steps, 16);
    CHECK_EQ(commands::testing::sentCommands().size(), 1);
    if (!commands::testing::sentCommands().empty()) {
        CHECK(commands::testing::sentCommands()[0].cmd == commands::Command::PATTERN_GATE_SET);
    }
}

TEST(leaving_dirty_pitch_set_view_restores_committed_preview) {
    commands::testing::reset();
    state::UIState s;
    s.currentView = state::ViewId::PITCH_SET;
    s.pitchSetDraft.pitches[0] = 99;
    s.pitchSetDirty = true;

    const auto next = state::reduce(s, press(KeyId::F1), nullptr);

    CHECK(next.currentView == state::ViewId::INIT);
    CHECK(!next.pitchSetDirty);
    CHECK_EQ(next.pitchSetDraft.pitches[0], 60);
    CHECK_EQ(commands::testing::sentCommands().size(), 1);
    if (!commands::testing::sentCommands().empty()) {
        CHECK(commands::testing::sentCommands()[0].cmd == commands::Command::PATTERN_PITCH_SET);
    }
}

TEST(add_pattern_initializes_and_sends_default_pitch_set) {
    commands::testing::reset();
    state::UIState s;
    s.patternCount = 2;

    state::addPattern(s);

    CHECK_EQ(s.pitchSetConfigs[2].count, 4);
    CHECK_EQ(s.pitchSetConfigs[2].pitches[0], 60);
    auto& cmds = commands::testing::sentCommands();
    // PATTERN_ADD, PATTERN_GATE_SET, PATTERN_PITCH_SET
    bool hasPitchSet = false;
    for (const auto& c : cmds) {
        if (c.cmd == commands::Command::PATTERN_PITCH_SET) {
            hasPitchSet = true;
            CHECK_EQ(c.param1, 2);
            CHECK_EQ(c.pitchCount, 4);
            CHECK_EQ(c.pitches[0], 60);
        }
    }
    CHECK(hasPitchSet);
}

TEST(remove_pattern_compacts_pitch_set_configs) {
    state::UIState s;
    s.patternCount = 3;
    s.pitchSetConfigs[0].pitches[0] = 60;
    s.pitchSetConfigs[1].pitches[0] = 62;
    s.pitchSetConfigs[2].pitches[0] = 64;

    state::removePattern(s, 1);

    CHECK_EQ(s.pitchSetConfigs[0].pitches[0], 60);
    CHECK_EQ(s.pitchSetConfigs[1].pitches[0], 64);
    CHECK_EQ(s.pitchSetConfigs[2].pitches[0], 60);  // reset to default
}

TEST(sync_pitch_set_sends_configured_pitches) {
    commands::testing::reset();
    state::UIState s;
    s.pitchSetConfigs[0].count = 3;
    s.pitchSetConfigs[0].pitches[0] = 55;
    s.pitchSetConfigs[0].pitches[1] = 57;
    s.pitchSetConfigs[0].pitches[2] = 59;
    s.pitchSetConfigs[0].order = common::PlayingOrder::BACKWARDS;

    state::syncPitchSet(s, 0);

    CHECK_EQ(commands::testing::sentCommands().size(), 1);
    if (!commands::testing::sentCommands().empty()) {
        const auto& cmd = commands::testing::sentCommands()[0];
        CHECK(cmd.cmd == commands::Command::PATTERN_PITCH_SET);
        CHECK_EQ(cmd.pitchCount, 3);
        CHECK_EQ(cmd.pitches[0], 55);
        CHECK(cmd.pitchOrder == common::PlayingOrder::BACKWARDS);
    }
}
