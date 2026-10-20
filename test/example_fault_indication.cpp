// performance-safe-foundation tasks 11.2 / 11.3 — persistent fault indication
// and explicit-Play acknowledgement.
//
// Validates Requirements 10.4, 10.5, 10.6 against ui::state fault-indication
// policy helpers:
//   - 11.2 (10.4): the fault matrix color is DISTINCT from every save/load
//     banner color and takes precedence while latched;
//   - 11.2 (10.5): the fault indication does not auto-dismiss — it shows for as
//     long as the latch holds;
//   - 11.3 (10.6): an explicit Play while latched clears the fault (and only
//     then); an ordinary Play when not latched is a no-op.
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "ui/state/FaultIndication.h"
#include "ui/state/UIState.h"

#include <cstdint>
#include <cstdio>

using ui::state::UIState;
using ui::state::SaveBanner;
namespace us = ui::state;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// 11.2 / 10.4: fault color is distinct and takes precedence over a save banner.
void test_fault_color_precedence() {
    UIState s;
    check(us::matrixFeedbackColor(s) == 0, "no overlay when nothing is active");

    s.saveBanner = SaveBanner::SUCCESS;
    check(us::matrixFeedbackColor(s) == us::kSaveSuccessColor, "save-success color when only banner");

    s.timingFaultLatched = true;
    check(us::shouldShowFault(s), "fault shown while latched");
    check(us::matrixFeedbackColor(s) == us::kFaultColor,
          "fault color takes precedence over the save banner (Req 10.4)");
    check(us::kFaultColor != us::kSaveSuccessColor && us::kFaultColor != us::kSaveFailureColor &&
              us::kFaultColor != us::kLoadFailureColor,
          "fault color distinct from every save/load color");
}

// 11.2 / 10.5: no auto-dismiss — the indication persists across many frames
// while latched.
void test_no_auto_dismiss() {
    UIState s;
    s.timingFaultLatched = true;
    for (int frame = 0; frame < 1000; ++frame) {
        check(us::shouldShowFault(s), "fault still shown (no auto-dismiss)");
        check(us::matrixFeedbackColor(s) == us::kFaultColor, "fault color persists each frame");
    }
}

// 11.3 / 10.6: explicit Play clears the latch; only then.
void test_explicit_play_acknowledges() {
    UIState s;
    // Not latched: Play is a no-op acknowledgement.
    check(!us::acknowledgeFaultOnPlay(s), "Play with no fault is not an acknowledgement");

    // Latched: Play acknowledges and clears.
    s.timingFaultLatched = true;
    s.timingFaultCause = 1;
    check(us::acknowledgeFaultOnPlay(s), "Play while latched acknowledges the fault");
    check(!s.timingFaultLatched && s.timingFaultCause == 0, "fault + cause cleared on ack");
    check(!us::shouldShowFault(s), "fault no longer shown after acknowledgement");

    // A second Play is again a no-op.
    check(!us::acknowledgeFaultOnPlay(s), "second Play is a no-op (already cleared)");
}

// Working State is preserved across the fault + acknowledgement (Req 10.3).
void test_working_state_preserved_through_ack() {
    UIState s;
    s.patternCount = 4;
    s.bpm = 150;
    s.timingFaultLatched = true;
    us::acknowledgeFaultOnPlay(s);
    check(s.patternCount == 4 && s.bpm == 150, "Working State intact across fault + ack");
}

}  // namespace

int main() {
    test_fault_color_precedence();
    test_no_auto_dismiss();
    test_explicit_play_acknowledges();
    test_working_state_preserved_through_ack();

    if (failures == 0) {
        std::printf("example_fault_indication: all checks passed\n");
        return 0;
    }
    std::printf("example_fault_indication: %d check(s) failed\n", failures);
    return 1;
}
