#include "framework.h"
#include "helpers.h"
#include "stubs/command_stub.h"
#include "ui/state/Reducer.h"
#include "ui/state/SetLibrary.h"
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

    void handleEvent(state::UIState& state, const events::Event& event) override {
        ++eventsSeen;
        lastKey = event.data.key.id;
        state.bpm = 77;            // sentinel: the view ran
    }

    void render(const state::UIState&) override {}
};

events::Event press(KeyId id, uint8_t mods = mod::NONE) {
    return events::Event::keyPressed(id, mods, 0);
}

} // namespace

TEST(reduce_f1_switches_to_settings_view) {
    FakeView view;
    const auto next = test::reduced(state::UIState{}, press(KeyId::F1), &view);
    CHECK(next.currentView == state::ViewId::SETTINGS);
    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_f2_switches_to_instruments_view) {
    FakeView view;
    state::UIState s;
    s.currentView = state::ViewId::PATTERNS;
    const auto next = test::reduced(s, press(KeyId::F2), &view);
    CHECK(next.currentView == state::ViewId::INSTRUMENTS);
    CHECK_EQ(view.eventsSeen, 0);
}

TEST(boot_view_is_instruments) {
    state::UIState s;
    CHECK(s.currentView == state::ViewId::INSTRUMENTS);
}

TEST(reduce_f4_f5_f6_open_the_set_views_of_the_selected_pattern) {
    state::UIState s;
    s.patternCount = 2;
    s.selectedPattern = 1;
    s.patternRefs[1] = state::PatternRefs{0, 0, 0};
    const struct { KeyId key; state::ViewId view; } cases[] = {
        {KeyId::F4, state::ViewId::GATE_SET},
        {KeyId::F5, state::ViewId::PITCH_SET},
        {KeyId::F6, state::ViewId::VELOCITY_SET},
    };
    for (const auto& c : cases) {
        state::UIState t = s;
        t.currentView = state::ViewId::PATTERNS;
        const auto next = test::reduced(t, press(c.key), nullptr);
        CHECK(next.currentView == c.view);
    }
}

TEST(set_view_function_keys_do_nothing_without_a_selected_pattern) {
    state::UIState s;
    s.currentView = state::ViewId::PATTERNS;
    s.patternCount = 1;
    s.selectedPattern = 1;  // the "add" slot
    const auto next = test::reduced(s, press(KeyId::F4), nullptr);
    CHECK(next.currentView == state::ViewId::PATTERNS);
}

TEST(set_view_function_key_matches_enter_in_the_patterns_view_and_does_not_restart_the_draft) {
    state::UIState s;
    s.currentView = state::ViewId::PATTERNS;
    s.patternCount = 2;
    s.selectedPattern = 1;
    state::createSetInView(s, state::SetType::GATE);   // slot 1
    s.patternRefs[1].gate = 1;
    const auto opened = test::reduced(s, press(KeyId::F4), nullptr);
    CHECK(opened.currentView == state::ViewId::GATE_SET);
    CHECK_EQ(opened.selectedGateSlot, 1);
    state::UIState edited = opened;
    edited.gateSetDraft.steps = 9;
    edited.gateSetDirty = true;
    const auto again = test::reduced(edited, press(KeyId::F4), nullptr);   // already open: no-op
    CHECK_EQ(again.gateSetDraft.steps, 9);
    CHECK(again.gateSetDirty);
}

TEST(reduce_f3_switches_to_patterns_view) {
    FakeView view;
    const auto next = test::reduced(state::UIState{}, press(KeyId::F3), &view);
    CHECK(next.currentView == state::ViewId::PATTERNS);
    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_consumes_unbound_function_keys_without_effect) {
    FakeView view;
    state::UIState s;
    s.currentView = state::ViewId::SETTINGS;
    for (uint8_t usage = static_cast<uint8_t>(KeyId::F7);
         usage <= static_cast<uint8_t>(KeyId::F12); ++usage) {
        const auto next = test::reduced(s, press(static_cast<KeyId>(usage)), &view);
        CHECK(next.currentView == state::ViewId::SETTINGS);
    }
    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_reserves_function_keys_regardless_of_modifiers) {
    FakeView view;
    state::UIState s;
    s.currentView = state::ViewId::SETTINGS;
    const auto next = test::reduced(s, press(KeyId::F1, mod::SHIFT), &view);
    CHECK(next.currentView == state::ViewId::SETTINGS);   // modified: consumed, no effect
    CHECK_EQ(view.eventsSeen, 0);                          // and never reaches the view
}

TEST(reduce_space_toggles_playing_and_sends_commands) {
    FakeView view;
    commands::testing::reset();

    state::UIState stopped;
    stopped.playing = false;
    const auto started = test::reduced(stopped, press(KeyId::SPACE), &view);
    CHECK(started.playing);

    const auto stoppedAgain = test::reduced(started, press(KeyId::SPACE), &view);
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

    const auto afterRelease = test::reduced(s, events::Event::keyReleased(KeyId::SPACE, mod::NONE, 0), &view);
    CHECK(!afterRelease.playing);

    const auto afterHeld = test::reduced(s, events::Event::keyHeld(KeyId::SPACE, mod::NONE, 0), &view);
    CHECK(!afterHeld.playing);

    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_delegates_unreserved_keys_to_the_active_view) {
    FakeView view;
    const auto next = test::reduced(state::UIState{}, press(KeyId::UP), &view);
    CHECK_EQ(next.bpm, 77);
    CHECK_EQ(view.eventsSeen, 1);
    CHECK(view.lastKey == KeyId::UP);
}

TEST(reduce_returns_state_unchanged_without_an_active_view) {
    state::UIState s;
    s.bpm = 7;
    const auto next = test::reduced(s, press(KeyId::UP), nullptr);
    CHECK_EQ(next.bpm, 7);
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

TEST(add_pattern_references_lowest_occupied_sets_and_sends_them) {
    commands::testing::reset();
    state::UIState s;
    s.patternCount = 2;
    s.gateSetOf(0).steps = 7;  // pattern 0 and the new pattern share gate slot 0

    state::addPattern(s);

    CHECK_EQ(s.patternCount, 3);
    CHECK_EQ(s.patternRefs[2].gate, 0);
    CHECK_EQ(s.gateSetOf(2).steps, 7);  // a new pattern shares the lowest occupied set
    CHECK_EQ(commands::testing::countOf(commands::Command::PATTERN_ADD), 1);
    auto& cmds = commands::testing::sentCommands();
    CHECK(!cmds.empty() && cmds[0].cmd == commands::Command::PATTERN_ADD);
    bool sawGate = false;
    for (const auto& c : cmds) {
        if (c.cmd == commands::Command::PATTERN_GATE_SET) {
            sawGate = true;
            CHECK_EQ(c.param1, 2);
        }
    }
    CHECK(sawGate);
}

TEST(remove_pattern_compacts_references_and_keeps_sets) {
    state::UIState s;
    s.patternCount = 3;
    uint8_t slot = 0;
    state::createSet(s, state::SetType::GATE, slot);
    state::createSet(s, state::SetType::GATE, slot);
    s.patternRefs[0].gate = 0;
    s.patternRefs[1].gate = 1;
    s.patternRefs[2].gate = 2;
    s.gateLib.slots[1].steps = 12;
    s.gateLib.slots[2].steps = 24;

    state::removePattern(s, 1);

    CHECK_EQ(s.patternRefs[0].gate, 0);
    CHECK_EQ(s.patternRefs[1].gate, 2);
    CHECK_EQ(s.gateSetOf(1).steps, 24);
    CHECK_EQ(s.gateLib.slots[1].steps, 12);  // orphaned set kept
    CHECK_EQ(s.gateLib.occupied, 0x0007);
}

TEST(sync_gate_set_supports_long_musical_cycles) {
    commands::testing::reset();
    state::UIState s;
    s.gateSetOf(0).steps = 64;
    s.gateSetOf(0).noteLength = state::NoteLength::WHOLE;

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

    const auto next = test::reduced(s, press(KeyId::F1), nullptr);

    CHECK(next.currentView == state::ViewId::SETTINGS);
    CHECK(!next.gateSetDirty);
    CHECK_EQ(next.gateSetDraft.steps, 16);
    CHECK_EQ(commands::testing::countOf(commands::Command::PATTERN_GATE_SET), 1);
}

TEST(leaving_dirty_pitch_set_view_restores_committed_preview) {
    commands::testing::reset();
    state::UIState s;
    s.currentView = state::ViewId::PITCH_SET;
    s.pitchSetDraft.pitches[0] = 99;
    s.pitchSetDirty = true;

    const auto next = test::reduced(s, press(KeyId::F1), nullptr);

    CHECK(next.currentView == state::ViewId::SETTINGS);
    CHECK(!next.pitchSetDirty);
    CHECK_EQ(next.pitchSetDraft.pitches[0], 60);
    CHECK_EQ(commands::testing::countOf(commands::Command::PATTERN_PITCH_SET), 1);
}

TEST(add_pattern_sends_referenced_pitch_set) {
    commands::testing::reset();
    state::UIState s;
    s.patternCount = 2;

    state::addPattern(s);

    CHECK_EQ(s.pitchSetOf(2).count, 4);
    CHECK_EQ(s.pitchSetOf(2).pitches[0], 60);
    bool hasPitchSet = false;
    for (const auto& c : commands::testing::sentCommands()) {
        if (c.cmd == commands::Command::PATTERN_PITCH_SET) {
            hasPitchSet = true;
            CHECK_EQ(c.param1, 2);
            CHECK_EQ(c.pitchCount, 4);
            CHECK_EQ(c.pitches[0], 60);
        }
    }
    CHECK(hasPitchSet);
}

TEST(sync_pitch_set_sends_configured_pitches) {
    commands::testing::reset();
    state::UIState s;
    s.pitchSetOf(0).count = 3;
    s.pitchSetOf(0).pitches[0] = 55;
    s.pitchSetOf(0).pitches[1] = 57;
    s.pitchSetOf(0).pitches[2] = 59;
    s.pitchSetOf(0).order = common::PlayingOrder::BACKWARDS;

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
