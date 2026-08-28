#include "framework.h"
#include "stubs/command_stub.h"
#include "stubs/hardware_stub.h"
#include "ui/views/GateSetView.h"
#include "ui/views/PitchSetView.h"
#include "ui/views/VelocitySetView.h"
#include "ui/state/UIState.h"
#include "common/gate_set.h"
#include "common/pitch_set.h"
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

TEST(pitchsetview_renders_count_field_by_default_and_escape_returns_to_patterns) {
    hardware::LedMatrix matrix{0};
    PitchSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::PITCH_SET;
    hardware::testing::resetMatrix();
    view.render(s);
    CHECK_STREQ(hardware::testing::lastLabel(), "Cnt");
    CHECK_EQ(hardware::testing::lastNumber(), 4);
    checkEscapeReturnsToPatterns(view, state::ViewId::PITCH_SET);
}

TEST(pitchsetview_navigates_through_count_order_and_pitches) {
    hardware::LedMatrix matrix{0};
    PitchSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::PITCH_SET;
    s.selectedPitchSetField = state::PITCH_SET_FIELD_COUNT;

    s = view.handleEvent(s, press(KeyId::RIGHT));
    CHECK_EQ(s.selectedPitchSetField, state::PITCH_SET_FIELD_ORDER);
    s = view.handleEvent(s, press(KeyId::RIGHT));
    CHECK_EQ(s.selectedPitchSetField, state::PITCH_SET_FIELD_PITCH_BASE + 0);
    s = view.handleEvent(s, press(KeyId::RIGHT));
    CHECK_EQ(s.selectedPitchSetField, state::PITCH_SET_FIELD_PITCH_BASE + 1);
    s = view.handleEvent(s, press(KeyId::RIGHT));
    CHECK_EQ(s.selectedPitchSetField, state::PITCH_SET_FIELD_PITCH_BASE + 2);
    s = view.handleEvent(s, press(KeyId::RIGHT));
    CHECK_EQ(s.selectedPitchSetField, state::PITCH_SET_FIELD_PITCH_BASE + 3);
    // clamp at last pitch
    CHECK_EQ(view.handleEvent(s, press(KeyId::RIGHT)).selectedPitchSetField,
             state::PITCH_SET_FIELD_PITCH_BASE + 3);
    // go back
    s = view.handleEvent(s, press(KeyId::LEFT));
    CHECK_EQ(s.selectedPitchSetField, state::PITCH_SET_FIELD_PITCH_BASE + 2);
    s = view.handleEvent(s, press(KeyId::LEFT));
    s = view.handleEvent(s, press(KeyId::LEFT));
    s = view.handleEvent(s, press(KeyId::LEFT));
    CHECK_EQ(s.selectedPitchSetField, state::PITCH_SET_FIELD_ORDER);
    s = view.handleEvent(s, press(KeyId::LEFT));
    CHECK_EQ(s.selectedPitchSetField, state::PITCH_SET_FIELD_COUNT);
    // clamp at count
    CHECK_EQ(view.handleEvent(s, press(KeyId::LEFT)).selectedPitchSetField,
             state::PITCH_SET_FIELD_COUNT);
}

TEST(pitchsetview_edits_count_and_duplicates_last_pitch) {
    hardware::LedMatrix matrix{0};
    PitchSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::PITCH_SET;
    s.selectedPitchSetField = state::PITCH_SET_FIELD_COUNT;
    commands::testing::reset();

    s = view.handleEvent(s, press(KeyId::UP));
    CHECK_EQ(s.pitchSetDraft.count, 5);
    CHECK_EQ(s.pitchSetDraft.pitches[4], 72);  // duplicated last pitch (72)
    CHECK(s.pitchSetDirty);
    CHECK_EQ(commands::testing::sentCommands().size(), 1);
    if (!commands::testing::sentCommands().empty()) {
        const auto& cmd = commands::testing::sentCommands()[0];
        CHECK(cmd.cmd == commands::Command::PATTERN_PITCH_SET);
        CHECK_EQ(cmd.param1, 0);
        CHECK_EQ(cmd.pitchCount, 5);
        CHECK_EQ(cmd.pitches[4], 72);
    }

    // shrink back
    s = view.handleEvent(s, press(KeyId::DOWN));
    CHECK_EQ(s.pitchSetDraft.count, 4);
    CHECK(!s.pitchSetDirty);
}

TEST(pitchsetview_clamps_count_to_1_and_16) {
    hardware::LedMatrix matrix{0};
    PitchSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::PITCH_SET;
    s.selectedPitchSetField = state::PITCH_SET_FIELD_COUNT;
    s.pitchSetDraft.count = 1;

    CHECK_EQ(view.handleEvent(s, press(KeyId::DOWN)).pitchSetDraft.count, 1);

    s.pitchSetDraft.count = 16;
    CHECK_EQ(view.handleEvent(s, press(KeyId::UP)).pitchSetDraft.count, 16);
}

TEST(pitchsetview_edits_order_and_renders_label) {
    hardware::LedMatrix matrix{0};
    PitchSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::PITCH_SET;
    s.selectedPitchSetField = state::PITCH_SET_FIELD_ORDER;
    commands::testing::reset();

    s = view.handleEvent(s, press(KeyId::UP));
    CHECK(s.pitchSetDraft.order == common::PlayingOrder::BACKWARDS);
    CHECK(s.pitchSetDirty);
    CHECK_EQ(commands::testing::sentCommands().size(), 1);

    s = view.handleEvent(s, press(KeyId::UP));
    CHECK(s.pitchSetDraft.order == common::PlayingOrder::PENDULUM);

    s = view.handleEvent(s, press(KeyId::UP));
    CHECK(s.pitchSetDraft.order == common::PlayingOrder::RANDOM);

    // clamp at RANDOM
    CHECK(view.handleEvent(s, press(KeyId::UP)).pitchSetDraft.order == common::PlayingOrder::RANDOM);

    s = view.handleEvent(s, press(KeyId::DOWN));
    CHECK(s.pitchSetDraft.order == common::PlayingOrder::PENDULUM);

    hardware::testing::resetMatrix();
    view.render(s);
    CHECK_STREQ(hardware::testing::lastLabel(), "Pnd");
}

TEST(pitchsetview_edits_pitch_and_renders_note_name) {
    hardware::LedMatrix matrix{0};
    PitchSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::PITCH_SET;
    s.selectedPitchSetField = state::PITCH_SET_FIELD_PITCH_BASE;
    commands::testing::reset();

    s = view.handleEvent(s, press(KeyId::UP));
    CHECK_EQ(s.pitchSetDraft.pitches[0], 61);
    CHECK(s.pitchSetDirty);
    CHECK_EQ(commands::testing::sentCommands().size(), 1);

    hardware::testing::resetMatrix();
    view.render(s);
    const char* note = hardware::testing::lastNote();
    CHECK_EQ(note[0], 'c');
    CHECK_EQ(note[1], '4');
    CHECK_EQ(note[2], '#');
    CHECK_STREQ(hardware::testing::lastLabel(), "p0");
}

TEST(pitchsetview_renders_natural_note_and_sharp_boundary) {
    hardware::LedMatrix matrix{0};
    PitchSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::PITCH_SET;
    s.selectedPitchSetField = state::PITCH_SET_FIELD_PITCH_BASE;

    // MIDI 60 = C4 natural
    s.pitchSetDraft.pitches[0] = 60;
    hardware::testing::resetMatrix();
    view.render(s);
    const char* noteC4 = hardware::testing::lastNote();
    CHECK_EQ(noteC4[0], 'c');
    CHECK_EQ(noteC4[1], '4');
    CHECK_EQ(noteC4[2], ' ');
    CHECK_EQ(noteC4[3], ' ');

    // MIDI 61 = C#4
    s.pitchSetDraft.pitches[0] = 61;
    hardware::testing::resetMatrix();
    view.render(s);
    const char* noteCs4 = hardware::testing::lastNote();
    CHECK_EQ(noteCs4[0], 'c');
    CHECK_EQ(noteCs4[2], '#');

    // MIDI 0 = C-1 (negative octave)
    s.pitchSetDraft.pitches[0] = 0;
    hardware::testing::resetMatrix();
    view.render(s);
    const char* noteCm1 = hardware::testing::lastNote();
    CHECK_EQ(noteCm1[0], 'c');
    CHECK_EQ(noteCm1[1], '1');
    CHECK_EQ(noteCm1[3], '-');

    // MIDI 127 = G9 (natural)
    s.pitchSetDraft.pitches[0] = 127;
    hardware::testing::resetMatrix();
    view.render(s);
    const char* noteG9 = hardware::testing::lastNote();
    CHECK_EQ(noteG9[0], 'g');
    CHECK_EQ(noteG9[1], '9');
    CHECK_EQ(noteG9[2], ' ');
}

TEST(pitchsetview_clamps_pitch_to_0_and_127) {
    hardware::LedMatrix matrix{0};
    PitchSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::PITCH_SET;
    s.selectedPitchSetField = state::PITCH_SET_FIELD_PITCH_BASE;

    s.pitchSetDraft.pitches[0] = 0;
    CHECK_EQ(view.handleEvent(s, press(KeyId::DOWN)).pitchSetDraft.pitches[0], 0);

    s.pitchSetDraft.pitches[0] = 127;
    CHECK_EQ(view.handleEvent(s, press(KeyId::UP)).pitchSetDraft.pitches[0], 127);
}

TEST(pitchsetview_enter_commits_and_escape_undoes_then_exits) {
    hardware::LedMatrix matrix{0};
    PitchSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::PITCH_SET;
    s.selectedPitchSetField = state::PITCH_SET_FIELD_PITCH_BASE;
    s = view.handleEvent(s, press(KeyId::UP));
    CHECK(s.pitchSetDirty);
    commands::testing::reset();

    auto undone = view.handleEvent(s, press(KeyId::ESCAPE));
    CHECK(undone.currentView == state::ViewId::PITCH_SET);
    CHECK(!undone.pitchSetDirty);
    CHECK_EQ(undone.pitchSetDraft.pitches[0], 60);
    CHECK_EQ(commands::testing::sentCommands().size(), 1);
    CHECK(view.handleEvent(undone, press(KeyId::ESCAPE)).currentView == state::ViewId::PATTERNS);

    // commit path
    s = view.handleEvent(s, press(KeyId::ENTER));
    CHECK(!s.pitchSetDirty);
    CHECK_EQ(s.pitchSetConfigs[0].pitches[0], 61);
    CHECK(view.handleEvent(s, press(KeyId::ESCAPE)).currentView == state::ViewId::PATTERNS);
}

TEST(pitchsetview_clears_dirty_when_edited_back_to_committed_value) {
    hardware::LedMatrix matrix{0};
    PitchSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::PITCH_SET;
    s.selectedPitchSetField = state::PITCH_SET_FIELD_PITCH_BASE;

    s = view.handleEvent(s, press(KeyId::UP));
    CHECK(s.pitchSetDirty);
    s = view.handleEvent(s, press(KeyId::DOWN));
    CHECK(!s.pitchSetDirty);
    CHECK_EQ(s.pitchSetDraft.pitches[0], 60);
}

TEST(pitchsetview_renders_dirty_indicator) {
    hardware::LedMatrix matrix{0};
    PitchSetView view{matrix};
    state::UIState s;
    s.currentView = state::ViewId::PITCH_SET;
    s.selectedPitchSetField = state::PITCH_SET_FIELD_PITCH_BASE;
    s.pitchSetDirty = true;

    hardware::testing::resetMatrix();
    view.render(s);
    CHECK_EQ(hardware::testing::pixelAt(15, 0), 0x0000FF00);
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

TEST(pitchset_forwards_advances_wrapping) {
    common::PitchSet ps({10, 20, 30}, common::PlayingOrder::FORWARDS);
    CHECK_EQ(ps.getPitch(), 10);
    ps.advance();
    CHECK_EQ(ps.getPitch(), 20);
    ps.advance();
    CHECK_EQ(ps.getPitch(), 30);
    ps.advance();
    CHECK_EQ(ps.getPitch(), 10);
}

TEST(pitchset_backwards_advances_wrapping) {
    common::PitchSet ps({10, 20, 30}, common::PlayingOrder::BACKWARDS);
    CHECK_EQ(ps.getPitch(), 10);
    ps.advance();
    CHECK_EQ(ps.getPitch(), 30);
    ps.advance();
    CHECK_EQ(ps.getPitch(), 20);
    ps.advance();
    CHECK_EQ(ps.getPitch(), 10);
}

TEST(pitchset_pendulum_bounces_without_repeating_endpoints) {
    common::PitchSet ps({10, 20, 30}, common::PlayingOrder::PENDULUM);
    CHECK_EQ(ps.getPitch(), 10);
    ps.advance(); CHECK_EQ(ps.getPitch(), 20);
    ps.advance(); CHECK_EQ(ps.getPitch(), 30);
    ps.advance(); CHECK_EQ(ps.getPitch(), 20);
    ps.advance(); CHECK_EQ(ps.getPitch(), 10);
    ps.advance(); CHECK_EQ(ps.getPitch(), 20);
    ps.advance(); CHECK_EQ(ps.getPitch(), 30);
}

TEST(pitchset_random_advances_to_a_valid_index) {
    common::PitchSet ps({10, 20, 30, 40}, common::PlayingOrder::RANDOM);
    const uint8_t startPos = ps.getPosition();
    ps.advance();
    const uint8_t endPos = ps.getPosition();
    CHECK(endPos < 4);
    CHECK(endPos != startPos);
}

TEST(pitchset_single_pitch_advance_is_stable) {
    common::PitchSet ps({42}, common::PlayingOrder::FORWARDS);
    CHECK_EQ(ps.getPitch(), 42);
    ps.advance();
    CHECK_EQ(ps.getPitch(), 42);
    ps.setOrder(common::PlayingOrder::BACKWARDS);
    ps.advance();
    CHECK_EQ(ps.getPitch(), 42);
    ps.setOrder(common::PlayingOrder::PENDULUM);
    ps.advance();
    CHECK_EQ(ps.getPitch(), 42);
}

TEST(pitchset_reset_restores_position_zero) {
    common::PitchSet ps({10, 20, 30}, common::PlayingOrder::FORWARDS);
    ps.advance();
    ps.advance();
    CHECK_EQ(ps.getPosition(), 2);
    ps.reset();
    CHECK_EQ(ps.getPosition(), 0);
    CHECK_EQ(ps.getPitch(), 10);
}
