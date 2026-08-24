#include "framework.h"
#include "stubs/hardware_stub.h"
#include "ui/views/GateSetView.h"
#include "ui/views/PitchSetView.h"
#include "ui/views/VelocitySetView.h"
#include "ui/state/UIState.h"
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

TEST(gatesetview_renders_title_and_escape_returns_to_patterns) {
    hardware::LedMatrix matrix{0};
    GateSetView view{matrix};
    hardware::testing::resetMatrix();
    view.render(state::UIState{});
    CHECK_STREQ(hardware::testing::lastLabel(), "GAt");
    checkEscapeReturnsToPatterns(view, state::ViewId::GATE_SET);
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
