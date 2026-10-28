// Task T10 (ui-integration): HostControl view-name mapping (FR10, AC9).
//
// H:dump renders "D:view=<name>" through viewName(), a file-local helper in
// the GENSEQ_HOST_CONTROL-gated src/ui/hardware/HostControl.cpp. Before T10
// the four Milestone-3 views (GENERATOR_CONFIG, MUTATION, MODULATION,
// SCALE_SELECTION) fell through to the "?" fallback, so a bench script could
// not identify a dump taken in one of those views.
//
// viewName() lives in an anonymous namespace, so this TU includes the real
// HostControl.cpp translation unit DIRECTLY (unity-style) instead of linking
// it: the helper becomes reachable here, compiled against the pico_stubs SDK
// doubles (pico/stdlib.h, pico/time.h, hardware/watchdog.h). This TU supplies
// host-safe definitions for the symbols that TU links against but that
// firmware owns in SDK-only translation units: the core0 UI telemetry globals
// (UiTelemetry.h externs, owned by UIController.cpp) and the multicore
// commands:: FIFO seam (owned by command.cpp). StateManager.cpp + Reducer.cpp
// are compiled alongside as the real, SDK-free dispatch path. No Pico SDK
// libs and no RapidCheck; own main(), so it lives in test/unit/ outside the
// property-test glob (test/*.cpp).

#include <cstdio>
#include <cstring>
#include <strings.h>  // POSIX strcasecmp/strtok_r used by the included TU

#include "commands/command.h"

namespace ui {
// Host doubles for the UiTelemetry.h externs; firmware definitions live in
// UIController.cpp, which is never compiled host-side.
uint32_t g_lastSaveMicros = 0;
uint32_t g_maxUiGapMicros = 0;
}  // namespace ui

namespace commands {
// Host-safe no-op FIFO seams; signatures MUST match command.h exactly. The
// view-name mapping never observes any inter-core effect, so command.cpp is
// not compiled (the same arrangement as genseq_reducer_property_tests).
void sendCommand(Command, uint8_t, uint8_t) {}
void sendGateSet(uint8_t, const std::vector<bool>&) {}
void sendPitchSet(uint8_t, uint8_t, common::PlayingOrder,
                  const std::vector<uint8_t>&) {}
void sendVelocitySet(uint8_t, uint8_t, common::PlayingOrder,
                     const std::vector<uint8_t>&) {}
void sendScaleCommand(uint8_t, uint8_t) {}
}  // namespace commands

// The real TU under test. GENSEQ_HOST_CONTROL must be 1 BEFORE the .cpp pulls
// in HostControl.h, whose #ifndef default is 0 and would compile the whole TU
// away.
#define GENSEQ_HOST_CONTROL 1
#include "ui/hardware/HostControl.cpp"

namespace {

int failures = 0;

void expectName(ui::state::ViewId v, const char* expect) {
    // viewName() is in an unnamed namespace inside namespace hardware; its
    // members are reachable through the enclosing namespace in this TU.
    const char* got = hardware::viewName(v);
    if (std::strcmp(got, expect) != 0) {
        std::printf("FAIL viewName(ViewId %d): expected \"%s\", got \"%s\"\n",
                    static_cast<int>(v), expect, got);
        ++failures;
    }
}

}  // namespace

int main() {
    // The pre-existing names keep their exact strings: the bench scripts in
    // test/bench diff dumps field-by-field, so these are a wire contract.
    expectName(ui::state::ViewId::INIT,             "init");
    expectName(ui::state::ViewId::SETTINGS,         "settings");
    expectName(ui::state::ViewId::PATTERNS,         "patterns");
    expectName(ui::state::ViewId::GATE_SET,         "gate");
    expectName(ui::state::ViewId::PITCH_SET,        "pitch");
    expectName(ui::state::ViewId::VELOCITY_SET,     "velocity");

    // T10: the four Milestone-3 views used to render as the "?" fallback.
    expectName(ui::state::ViewId::GENERATOR_CONFIG, "generator");
    expectName(ui::state::ViewId::MUTATION,         "mutation");
    expectName(ui::state::ViewId::MODULATION,       "modulation");
    expectName(ui::state::ViewId::SCALE_SELECTION,  "scale");

    // Whole-enum guard: no named view may hit the "?" fallback, so a dump
    // taken from ANY view stays identifiable even for ViewIds added after
    // this test. COUNT is the sentinel, not a view.
    for (int i = 0; i < static_cast<int>(ui::state::ViewId::COUNT); ++i) {
        const char* got = hardware::viewName(static_cast<ui::state::ViewId>(i));
        if (got == nullptr || got[0] == '\0' || std::strcmp(got, "?") == 0) {
            std::printf("FAIL viewName(ViewId %d) has no mapping (\"%s\")\n",
                        i, got != nullptr ? got : "(null)");
            ++failures;
        }
    }

    if (failures == 0) {
        std::printf("test_host_control_view_names: PASS (10 view names mapped)\n");
        return 0;
    }
    std::printf("test_host_control_view_names: FAIL (%d check(s) failed)\n", failures);
    return 1;
}
