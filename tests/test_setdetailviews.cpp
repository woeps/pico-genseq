#include "framework.h"
#include "stubs/command_stub.h"
#include "stubs/hardware_stub.h"
#include "ui/views/GateSetView.h"
#include "ui/views/PitchSetView.h"
#include "ui/views/VelocitySetView.h"
#include "ui/state/UIState.h"
#include "common/gate_set.h"
#include "ui/Event.h"

using namespace ui;

namespace {

events::Event press(KeyId id) {
    return events::Event::keyPressed(id, mod::NONE, 0);
}

template <typename View>
void checkEscapeReturnsToPatterns(View& view, state::ViewId detailView) {
    state::UIState s;
    s.currentView = detailView;
    CHECK(view.handleEvent(s, press(KeyId::ESCAPE)).currentView == state::ViewId::PATTERNS);
    CHECK(view.handleEvent(s, press(KeyId::ENTER)).currentView == detailView);
}

}

TEST(gatesetview_starts_on_euclidean_algorithm_and_navigates_properties) {
    hardware::LedMatrix matrix{0};
    GateSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::GATE_SET;

    hardware::testing::resetMatrix();
    view.render(s);
    CHECK_STREQ(hardware::testing::lastLabel(), "EUC");

    s = view.handleEvent(s, press(KeyId::RIGHT));
    CHECK(s.selectedGateSetProperty == state::GateSetProperty::STEPS);
    s = view.handleEvent(s, press(KeyId::RIGHT));
    CHECK(s.selectedGateSetProperty == state::GateSetProperty::PULSES);
    s = view.handleEvent(s, press(KeyId::RIGHT));
    CHECK(s.selectedGateSetProperty == state::GateSetProperty::ROTATION);
    s = view.handleEvent(s, press(KeyId::RIGHT));
    CHECK(s.selectedGateSetProperty == state::GateSetProperty::NOTE_LENGTH);
    s = view.handleEvent(s, press(KeyId::RIGHT));
    CHECK(s.selectedGateSetProperty == state::GateSetProperty::LENGTH);
    CHECK(view.handleEvent(s, press(KeyId::RIGHT)).selectedGateSetProperty == state::GateSetProperty::LENGTH);
    CHECK(view.handleEvent(s, press(KeyId::LEFT)).selectedGateSetProperty == state::GateSetProperty::NOTE_LENGTH);
}

TEST(gatesetview_edits_and_renders_musical_note_length) {
    hardware::LedMatrix matrix{0};
    GateSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::GATE_SET;
    s.selectedGateSetProperty = state::GateSetProperty::NOTE_LENGTH;
    commands::testing::reset();

    s = view.handleEvent(s, press(KeyId::UP));
    CHECK(s.gateSetDraft.noteLength == state::NoteLength::EIGHTH);
    CHECK(s.gateSetDirty);
    CHECK_EQ(commands::testing::sentCommands().size(), 1);
    if (!commands::testing::sentCommands().empty()) {
        CHECK_EQ(commands::testing::sentCommands()[0].gateCount, 192);
    }
    hardware::testing::resetMatrix();
    view.render(s);
    CHECK_STREQ(hardware::testing::lastLabel(), "nLn");
    CHECK_EQ(hardware::testing::lastNumber(), 8);
}

TEST(gatesetview_edits_send_live_preview_and_render_dirty_value) {
    hardware::LedMatrix matrix{0};
    GateSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::GATE_SET;
    s.selectedGateSetProperty = state::GateSetProperty::STEPS;
    commands::testing::reset();

    const auto next = view.handleEvent(s, events::Event::keyHeld(KeyId::UP, mod::NONE, 0));
    CHECK_EQ(next.gateSetDraft.steps, 17);
    CHECK(next.gateSetDirty);
    CHECK_EQ(commands::testing::sentCommands().size(), 1);
    if (!commands::testing::sentCommands().empty()) {
        const auto& command = commands::testing::sentCommands()[0];
        CHECK(command.cmd == commands::Command::PATTERN_GATE_SET);
        CHECK_EQ(command.param1, 0);
        CHECK_EQ(command.gateCount, 102);
    }

    hardware::testing::resetMatrix();
    view.render(next);
    CHECK_STREQ(hardware::testing::lastLabel(), "StP");
    CHECK_EQ(hardware::testing::lastNumber(), 17);
    CHECK_EQ(hardware::testing::pixelAt(15, 0), 0x0000FF00);
}

TEST(gatesetview_enter_commits_and_escape_undoes_then_exits) {
    hardware::LedMatrix matrix{0};
    GateSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::GATE_SET;
    s.selectedGateSetProperty = state::GateSetProperty::PULSES;
    s = view.handleEvent(s, press(KeyId::UP));
    commands::testing::reset();

    auto undone = view.handleEvent(s, press(KeyId::ESCAPE));
    CHECK(undone.currentView == state::ViewId::GATE_SET);
    CHECK(!undone.gateSetDirty);
    CHECK_EQ(undone.gateSetDraft.pulses, 4);
    CHECK_EQ(commands::testing::sentCommands().size(), 1);
    CHECK(view.handleEvent(undone, press(KeyId::ESCAPE)).currentView == state::ViewId::PATTERNS);

    s = view.handleEvent(s, press(KeyId::ENTER));
    CHECK(!s.gateSetDirty);
    CHECK_EQ(s.gateSetConfigs[0].pulses, 5);
    CHECK(view.handleEvent(s, press(KeyId::ESCAPE)).currentView == state::ViewId::PATTERNS);
}

TEST(gatesetview_clamps_dependent_euclidean_values) {
    hardware::LedMatrix matrix{0};
    GateSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::GATE_SET;
    s.gateSetDraft = {state::GateAlgorithm::EUCLIDEAN, 16, 16, 15,
                      state::NoteLength::SIXTEENTH, 50};
    s.selectedGateSetProperty = state::GateSetProperty::STEPS;

    const auto next = view.handleEvent(s, press(KeyId::DOWN));
    CHECK_EQ(next.gateSetDraft.steps, 15);
    CHECK_EQ(next.gateSetDraft.pulses, 15);
    CHECK_EQ(next.gateSetDraft.rotation, 14);
}

TEST(gatesetview_obeys_euclidean_field_bounds) {
    hardware::LedMatrix matrix{0};
    GateSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::GATE_SET;
    s.gateSetDraft = {state::GateAlgorithm::EUCLIDEAN, 64, 0, 0,
                      state::NoteLength::WHOLE, 100};
    commands::testing::reset();

    s.selectedGateSetProperty = state::GateSetProperty::STEPS;
    CHECK_EQ(view.handleEvent(s, press(KeyId::UP)).gateSetDraft.steps, 64);
    s.selectedGateSetProperty = state::GateSetProperty::PULSES;
    CHECK_EQ(view.handleEvent(s, press(KeyId::DOWN)).gateSetDraft.pulses, 0);
    s.selectedGateSetProperty = state::GateSetProperty::ROTATION;
    CHECK_EQ(view.handleEvent(s, press(KeyId::DOWN)).gateSetDraft.rotation, 0);
    s.selectedGateSetProperty = state::GateSetProperty::NOTE_LENGTH;
    CHECK(view.handleEvent(s, press(KeyId::UP)).gateSetDraft.noteLength == state::NoteLength::WHOLE);
    s.selectedGateSetProperty = state::GateSetProperty::LENGTH;
    CHECK_EQ(view.handleEvent(s, press(KeyId::UP)).gateSetDraft.gateLength, 100);
    s.gateSetDraft.noteLength = state::NoteLength::THIRTY_SECOND;
    s.selectedGateSetProperty = state::GateSetProperty::NOTE_LENGTH;
    CHECK(view.handleEvent(s, press(KeyId::DOWN)).gateSetDraft.noteLength == state::NoteLength::THIRTY_SECOND);
    s.gateSetDraft.gateLength = 0;
    s.selectedGateSetProperty = state::GateSetProperty::LENGTH;
    CHECK_EQ(view.handleEvent(s, press(KeyId::DOWN)).gateSetDraft.gateLength, 0);
    s.selectedGateSetProperty = state::GateSetProperty::ALGORITHM;
    CHECK(view.handleEvent(s, press(KeyId::UP)).gateSetDraft.algorithm == state::GateAlgorithm::EUCLIDEAN);
    CHECK_EQ(commands::testing::sentCommands().size(), 0);
}

TEST(gatesetview_clears_dirty_when_edited_back_to_committed_value) {
    hardware::LedMatrix matrix{0};
    GateSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::GATE_SET;
    s.selectedGateSetProperty = state::GateSetProperty::PULSES;

    s = view.handleEvent(s, press(KeyId::UP));
    CHECK(s.gateSetDirty);
    s = view.handleEvent(s, press(KeyId::DOWN));
    CHECK(!s.gateSetDirty);
    CHECK_EQ(s.gateSetDraft.pulses, 4);
}

TEST(pitchsetview_renders_title_and_escape_returns_to_patterns) {
    hardware::LedMatrix matrix{0};
    PitchSetView view{matrix};
    hardware::testing::resetMatrix();
    view.render(state::UIState{});
    CHECK_STREQ(hardware::testing::lastLabel(), "PIt");
    checkEscapeReturnsToPatterns(view, state::ViewId::PITCH_SET);
}

TEST(velocitysetview_renders_title_and_escape_returns_to_patterns) {
    hardware::LedMatrix matrix{0};
    VelocitySetView view{matrix};
    hardware::testing::resetMatrix();
    view.render(state::UIState{});
    CHECK_STREQ(hardware::testing::lastLabel(), "VEL");
    checkEscapeReturnsToPatterns(view, state::ViewId::VELOCITY_SET);
}

TEST(euclidean_gate_set_uses_step_note_length_for_cycle_size) {
    CHECK_EQ(common::GateSet::createEuclidean(7, 3, 0, 12, 50).getLength(), 84);
    CHECK_EQ(common::GateSet::createEuclidean(16, 4, 0, 6, 50).getLength(), 96);
}

TEST(euclidean_gate_set_scales_gate_until_the_next_pulse) {
    const auto trigger = common::GateSet::createEuclidean(4, 1, 0, 6, 0).getGates();
    const auto half = common::GateSet::createEuclidean(4, 1, 0, 6, 50).getGates();
    const auto full = common::GateSet::createEuclidean(4, 1, 0, 6, 100).getGates();

    int triggerTicks = 0;
    int halfTicks = 0;
    int fullTicks = 0;
    for (bool gate : trigger) triggerTicks += gate ? 1 : 0;
    for (bool gate : half) halfTicks += gate ? 1 : 0;
    for (bool gate : full) fullTicks += gate ? 1 : 0;
    CHECK_EQ(triggerTicks, 1);
    CHECK(trigger[18]);
    CHECK(!trigger[19]);
    CHECK_EQ(halfTicks, 12);
    CHECK_EQ(fullTicks, 23);
    CHECK(full[16]);
    CHECK(!full[17]);
    CHECK(full[18]);
}

TEST(gate_set_positions_support_long_musical_cycles) {
    common::GateSet gateSet(std::vector<bool>(6144, false));
    gateSet.setPosition(4095);
    CHECK_EQ(gateSet.getPosition(), 4095);
}

TEST(euclidean_gate_set_handles_zero_dimensions) {
    CHECK_EQ(common::GateSet::createEuclidean(0, 0, 0, 24, 50).getLength(), 0);
    CHECK_EQ(common::GateSet::createEuclidean(8, 3, 0, 0, 50).getLength(), 0);

    const auto silent = common::GateSet::createEuclidean(8, 0, 0, 24, 50);
    for (bool gate : silent.getGates()) CHECK(!gate);
}
