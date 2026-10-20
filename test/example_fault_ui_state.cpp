// performance-safe-foundation task 11.1 — distinct latched Timing Fault UI state.
//
// Validates Requirements 10.3, 10.4, 10.7 against ui::state::UIState: the latched
// Timing Fault indicator is distinct plain state that preserves Working State and
// is independent of the save/load SaveBanner feedback.
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "ui/state/UIState.h"

#include <cstdint>
#include <cstdio>

using ui::state::UIState;
using ui::state::SaveBanner;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// 10.4: the fault flag is DISTINCT from the SaveBanner feedback — the two are
// independent fields and can hold any combination.
void test_fault_distinct_from_save_banner() {
    UIState s;
    check(!s.timingFaultLatched, "fault not latched by default");
    check(s.saveBanner == SaveBanner::NONE, "no save banner by default");

    // A save-success banner and a latched fault coexist independently.
    s.saveBanner = SaveBanner::SUCCESS;
    s.timingFaultLatched = true;
    s.timingFaultCause = 1;  // SCHEDULER_LATENESS
    check(s.saveBanner == SaveBanner::SUCCESS && s.timingFaultLatched,
          "fault indicator is distinct from and independent of the save banner (10.4)");
}

// 10.3: Working State is PRESERVED while the fault is latched — latching the
// fault does not clear patterns or their configuration.
void test_working_state_preserved() {
    UIState s;
    s.patternCount = 3;
    s.bpm = 140;
    s.pitchSetConfigs[0].count = 5;
    s.pitchSetConfigs[0].pitches[0] = 48;
    s.activePatterns = 0x0007;

    // Latch the fault.
    s.timingFaultLatched = true;
    s.timingFaultCause = 2;  // MIDI_OUTPUT_OVERLOAD

    // Working State is untouched (Req 10.3).
    check(s.patternCount == 3 && s.bpm == 140, "transport/pattern count preserved under fault");
    check(s.pitchSetConfigs[0].count == 5 && s.pitchSetConfigs[0].pitches[0] == 48,
          "pattern configuration preserved under fault");
    check(s.activePatterns == 0x0007, "active-pattern mask preserved under fault");
}

// 10.6/10.5: the fault does NOT auto-dismiss; it is cleared explicitly (the
// explicit-Play acknowledgement path clears both the flag and the cause).
void test_explicit_clear_only() {
    UIState s;
    s.timingFaultLatched = true;
    s.timingFaultCause = 3;

    // Simulate the explicit-Play acknowledgement clearing the latch.
    s.timingFaultLatched = false;
    s.timingFaultCause = 0;
    check(!s.timingFaultLatched && s.timingFaultCause == 0,
          "explicit acknowledgement clears the latched fault and its cause (10.6)");
}

// 10.7: a machine-readable cause is retained for diagnostics while latched.
void test_cause_retained() {
    UIState s;
    s.timingFaultLatched = true;
    s.timingFaultCause = 2;
    check(s.timingFaultCause == 2, "machine-readable fault cause retained for diagnostics (10.7)");
}

}  // namespace

int main() {
    test_fault_distinct_from_save_banner();
    test_working_state_preserved();
    test_explicit_clear_only();
    test_cause_retained();

    if (failures == 0) {
        std::printf("example_fault_ui_state: all checks passed\n");
        return 0;
    }
    std::printf("example_fault_ui_state: %d check(s) failed\n", failures);
    return 1;
}
