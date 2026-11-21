// Instruments task 16.3 -- MIDI load indicator policy and the timing fault mirror.
#include "framework.h"
#include "ui/state/LoadIndicator.h"
#include "ui/state/UIState.h"

using namespace ui::state;

TEST(load_level_is_off_when_nothing_was_late) {
    CHECK(loadLevel(false, 0, 5000) == LoadLevel::OFF);
}

TEST(load_level_is_amber_for_two_seconds_after_a_late_byte) {
    CHECK(loadLevel(false, 1000, 1000) == LoadLevel::AMBER);
    CHECK(loadLevel(false, 1000, 2999) == LoadLevel::AMBER);
    CHECK(loadLevel(false, 1000, 3000) == LoadLevel::OFF);
}

TEST(load_level_red_wins_while_a_fault_is_latched) {
    CHECK(loadLevel(true, 0, 5000) == LoadLevel::RED);
    CHECK(loadLevel(true, 4999, 5000) == LoadLevel::RED);
}

TEST(load_colors_are_off_amber_red) {
    CHECK_EQ(loadColor(LoadLevel::OFF), 0u);
    CHECK_EQ(loadColor(LoadLevel::AMBER), 0x0080FF00u);
    CHECK_EQ(loadColor(LoadLevel::RED), 0x0000FF00u);
    CHECK_EQ(kLoadPixelX, 15);
    CHECK_EQ(kLoadPixelY, 4);
}

TEST(mirror_latches_the_fault_and_stops_the_transport) {
    UIState s;
    s.playing = true;
    CHECK(mirrorTimingFault(s, true, 2));
    CHECK(s.timingFaultLatched);
    CHECK_EQ(s.timingFaultCause, 2);
    CHECK(!s.playing);
    CHECK(!mirrorTimingFault(s, true, 2));   // unchanged: no notify
}

TEST(mirror_clears_after_core1_acknowledged_play) {
    UIState s;
    mirrorTimingFault(s, true, 2);
    s.playing = true;                        // the user pressed Space (Play)
    CHECK(mirrorTimingFault(s, false, 0));
    CHECK(!s.timingFaultLatched);
    CHECK_EQ(s.timingFaultCause, 0);
    CHECK(s.playing);                        // clearing never touches the transport
}

TEST(mirror_without_a_fault_changes_nothing) {
    UIState s;
    s.playing = true;
    CHECK(!mirrorTimingFault(s, false, 0));
    CHECK(s.playing);
}
