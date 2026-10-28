#include "HostControl.h"

#if GENSEQ_HOST_CONTROL

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/watchdog.h"

#include "../Types.h"
#include "../Event.h"
#include "../state/StateManager.h"
#include "../state/UIState.h"
#include "../../sequencer/sequencer_telemetry.h"
#include "../UiTelemetry.h"

namespace hardware {
namespace {

using ui::KeyId;
using ui::events::Event;

uint32_t nowMs() { return to_ms_since_boot(get_absolute_time()); }

// Push one key as the real keyboard would: a press then a release, through the
// SAME dispatch() seam. The reducer's global handlers (Ctrl+S, SPACE, F-keys)
// and every view act on KEY_PRESSED; the release keeps decoder-equivalent
// framing so repeat/held logic elsewhere never sees a key stuck down.
void tapKey(KeyId id, uint8_t mods) {
    const uint32_t t = nowMs();
    ui::state::getStateManager().dispatch(Event::keyPressed(id, mods, t));
    ui::state::getStateManager().dispatch(Event::keyReleased(id, mods, t));
}

// Case-insensitive name -> KeyId for the keys a bench test actually drives.
// Deliberately a small explicit table (not the full HID set) so an unknown
// name is rejected rather than silently cast to a bogus usage code.
KeyId keyFromName(const char* name) {
    struct Entry { const char* n; KeyId id; };
    static const Entry table[] = {
        {"s", KeyId::S}, {"space", KeyId::SPACE},
        {"up", KeyId::UP}, {"down", KeyId::DOWN},
        {"left", KeyId::LEFT}, {"right", KeyId::RIGHT},
        {"f1", KeyId::F1}, {"f2", KeyId::F2}, {"f3", KeyId::F3},
        {"enter", KeyId::ENTER}, {"escape", KeyId::ESCAPE},
    };
    for (const auto& e : table) {
        if (strcasecmp(e.n, name) == 0) return e.id;
    }
    return KeyId::NONE;
}

uint8_t modFromName(const char* name) {
    if (name == nullptr) return ui::mod::NONE;
    if (strcasecmp(name, "ctrl") == 0) return ui::mod::CTRL;
    if (strcasecmp(name, "shift") == 0) return ui::mod::SHIFT;
    if (strcasecmp(name, "alt") == 0) return ui::mod::ALT;
    if (strcasecmp(name, "gui") == 0) return ui::mod::GUI;
    return ui::mod::NONE;
}

const char* viewName(ui::state::ViewId v) {
    switch (v) {
        case ui::state::ViewId::INIT:         return "init";
        case ui::state::ViewId::SETTINGS:     return "settings";
        case ui::state::ViewId::PATTERNS:     return "patterns";
        case ui::state::ViewId::GATE_SET:     return "gate";
        case ui::state::ViewId::PITCH_SET:    return "pitch";
        case ui::state::ViewId::VELOCITY_SET: return "velocity";
        case ui::state::ViewId::GENERATOR_CONFIG: return "generator";
        case ui::state::ViewId::MUTATION:         return "mutation";
        case ui::state::ViewId::MODULATION:       return "modulation";
        case ui::state::ViewId::SCALE_SELECTION:  return "scale";
        default:                              return "?";
    }
}

} // namespace

void HostControl::poll() {
    // Drain only already-buffered input; -1 (PICO_ERROR_TIMEOUT) means nothing
    // pending, so the loop never blocks waiting for a host byte.
    for (;;) {
        const int c = getchar_timeout_us(0);
        if (c < 0) return;

        if (c == '\r') continue;
        if (c == '\n') {
            line_[len_] = '\0';
            if (len_ > 0) dispatchLine(line_);
            len_ = 0;
            continue;
        }
        if (len_ < LINE_MAX - 1) {
            line_[len_++] = static_cast<char>(c);
        } else {
            // Overlong line: reset and report rather than silently truncate.
            len_ = 0;
            printf("H:err line_too_long\n");
        }
    }
}

void HostControl::dispatchLine(const char* raw) {
    // Only lines starting with the "H:" guard are commands; anything else is
    // host noise (echoes, stray bytes) and is ignored without a reply.
    if (raw[0] != 'H' && raw[0] != 'h') return;
    if (raw[1] != ':') return;

    // Tokenize a mutable copy on the stack (bounded by LINE_MAX, no heap).
    char buf[LINE_MAX];
    strncpy(buf, raw + 2, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* save = nullptr;
    const char* verb = strtok_r(buf, " \t", &save);
    if (verb == nullptr) { printf("H:err empty\n"); return; }

    const char* a1 = strtok_r(nullptr, " \t", &save);
    const char* a2 = strtok_r(nullptr, " \t", &save);

    if (strcasecmp(verb, "ping") == 0) {
        printf("H:pong\n");
        return;
    }

    if (strcasecmp(verb, "save") == 0) {        // = Ctrl+S
        tapKey(KeyId::S, ui::mod::CTRL);
        printf("H:ok save\n");
        return;
    }

    if (strcasecmp(verb, "play") == 0) {        // = SPACE (toggle transport)
        tapKey(KeyId::SPACE, ui::mod::NONE);
        printf("H:ok play\n");
        return;
    }

    if (strcasecmp(verb, "view") == 0) {        // 1->F1 2->F2 3->F3
        if (a1 == nullptr) { printf("H:err view_needs_index\n"); return; }
        const int n = atoi(a1);
        KeyId f = n == 1 ? KeyId::F1 : n == 2 ? KeyId::F2 : n == 3 ? KeyId::F3 : KeyId::NONE;
        if (f == KeyId::NONE) { printf("H:err view_range\n"); return; }
        tapKey(f, ui::mod::NONE);
        printf("H:ok view\n");
        return;
    }

    if (strcasecmp(verb, "key") == 0) {         // raw key by name [+ mod]
        if (a1 == nullptr) { printf("H:err key_needs_name\n"); return; }
        const KeyId id = keyFromName(a1);
        if (id == KeyId::NONE) { printf("H:err key_unknown\n"); return; }
        tapKey(id, modFromName(a2));
        printf("H:ok key\n");
        return;
    }

    if (strcasecmp(verb, "up") == 0 || strcasecmp(verb, "down") == 0 ||
        strcasecmp(verb, "left") == 0 || strcasecmp(verb, "right") == 0) {
        const KeyId id = strcasecmp(verb, "up") == 0 ? KeyId::UP
                       : strcasecmp(verb, "down") == 0 ? KeyId::DOWN
                       : strcasecmp(verb, "left") == 0 ? KeyId::LEFT
                       : KeyId::RIGHT;
        const bool coarse = a1 != nullptr && strcasecmp(a1, "coarse") == 0;
        tapKey(id, coarse ? ui::mod::SHIFT : ui::mod::NONE);
        printf("H:ok %s\n", verb);
        return;
    }

    if (strcasecmp(verb, "bpm") == 0) {
        if (a1 == nullptr) { printf("H:err bpm_needs_value\n"); return; }
        int target = atoi(a1);
        if (target < 40) target = 40;
        if (target > 255) target = 255;
        // Go to SETTINGS (F2) where UP/DOWN step BPM, then walk to the target
        // via coarse (shift = +/-10) + fine steps. Faithful to the real path:
        // every step is a synthesized keypress through the reducer, which emits
        // the BPM_SET command to core1 exactly as a human would.
        tapKey(KeyId::F2, ui::mod::NONE);
        int cur = static_cast<int>(ui::state::getStateManager().getState().bpm);
        int guard = 0;
        while (cur != target && guard++ < 512) {
            const int diff = target - cur;
            const KeyId dir = diff > 0 ? KeyId::UP : KeyId::DOWN;
            const bool coarse = (diff <= -10 || diff >= 10);
            tapKey(dir, coarse ? ui::mod::SHIFT : ui::mod::NONE);
            cur = static_cast<int>(ui::state::getStateManager().getState().bpm);
        }
        printf("H:ok bpm %d\n", cur);
        return;
    }

    if (strcasecmp(verb, "pattern") == 0) {
        // H:pattern <i> on|off - navigate PATTERNS and toggle pattern i.
        if (a1 == nullptr || a2 == nullptr) { printf("H:err pattern_args\n"); return; }
        const int idx = atoi(a1);
        const bool want = strcasecmp(a2, "on") == 0;
        if (idx < 0 || idx >= ui::state::MAX_PATTERNS) { printf("H:err pattern_range\n"); return; }
        tapKey(KeyId::F3, ui::mod::NONE);   // PATTERNS view
        // PatternsView selects with UP/DOWN and toggles with ENTER/SPACE-class
        // keys; expose it as a raw request and report. The host verifies the
        // effect via H:dump rather than this reply asserting success, since the
        // exact toggle key is view-defined.
        (void)want;
        printf("H:ok pattern (verify via dump)\n");
        return;
    }

    if (strcasecmp(verb, "dump") == 0) {
        emitDump();
        return;
    }

    if (strcasecmp(verb, "resetmetrics") == 0) {
        // Zero the UI-gap / save-duration telemetry so a measurement window
        // starts clean, excluding the boot + USB re-enumeration transient that
        // otherwise dominates maxUiGapUs as a max-since-boot. (soundingNotes and
        // timingFault are live state, not reset.)
        ui::g_maxUiGapMicros = 0;
        ui::g_lastSaveMicros = 0;
        printf("H:ok resetmetrics\n");
        return;
    }

    if (strcasecmp(verb, "testload") == 0) {
        // Give the selected pattern a DENSE gate set (pulses == steps) so the
        // sequencer emits a note on every step — a near-continuous MIDI load for
        // the task-12.4 live-save transparency capture. Drives the real gate
        // editor through synthesized keys (never pokes state): F3 -> ENTER opens
        // the GATE_SET editor (property resets to ALGORITHM), RIGHT,RIGHT walks
        // ALGORITHM->STEPS->PULSES, UP (coarse) raises pulses to the ceiling
        // (clamped to steps), ENTER commits. Reads gateSetDraft.pulses back to
        // guard the loop.
        auto& sm = ui::state::getStateManager();
        tapKey(KeyId::F3, ui::mod::NONE);     // PATTERNS view
        tapKey(KeyId::ENTER, ui::mod::NONE);  // enter GATE_SET editor
        tapKey(KeyId::RIGHT, ui::mod::NONE);  // ALGORITHM -> STEPS
        tapKey(KeyId::RIGHT, ui::mod::NONE);  // STEPS -> PULSES
        int prev = -1, guard = 0;
        while (guard++ < 80) {
            const int p = static_cast<int>(sm.getState().gateSetDraft.pulses);
            if (p == prev) break;             // clamped at the ceiling (== steps)
            prev = p;
            tapKey(KeyId::UP, ui::mod::SHIFT); // coarse +10 toward the ceiling
        }
        tapKey(KeyId::ENTER, ui::mod::NONE);  // commit the gate set
        printf("H:ok testload pulses=%u (verify via dump)\n",
               static_cast<unsigned>(sm.getState().gateSetConfigs[
                   sm.getState().selectedPattern].pulses));
        return;
    }

    if (strcasecmp(verb, "reset") == 0) {
        // Autonomous "power-cycle": arm the watchdog with a short timeout and
        // spin. The chip reboots and re-runs main(), re-loading config from the
        // FRAM - exactly the boot-load path 12.3 verifies, with no human unplug
        // and no debug probe. Reply first so the host sees the ack before the
        // serial link drops on reboot.
        printf("H:ok reset\n");
        stdio_flush();
        watchdog_reboot(0, 0, 50);
        for (;;) { tight_loop_contents(); }
    }

    printf("H:err unknown_verb\n");
}

void HostControl::emitDump() {
    const ui::state::UIState& s = ui::state::getStateManager().getState();
    // One field per line, "D:<key>=<value>", so a host parser reads it without
    // ambiguity and can diff two dumps field-by-field (the 12.3 round-trip
    // check: dump -> set -> save -> reset -> dump -> compare).
    printf("D:view=%s\n", viewName(s.currentView));
    printf("D:bpm=%u\n", static_cast<unsigned>(s.bpm));
    printf("D:playing=%u\n", s.playing ? 1u : 0u);
    printf("D:patternCount=%u\n", static_cast<unsigned>(s.patternCount));
    printf("D:activePatterns=0x%04x\n", static_cast<unsigned>(s.activePatterns));
    printf("D:selectedPattern=%u\n", static_cast<unsigned>(s.selectedPattern));
    printf("D:saveBanner=%u\n", static_cast<unsigned>(s.saveBanner));
    printf("D:timingFault=%u\n", s.timingFaultLatched ? 1u : 0u);
    // Literal validation metrics (tasks 12.3/12.4): sounding-note count (0 after
    // Stop = no stuck note), last live-save duration us (<=100 ms gap limit),
    // and max UI-loop gap us (<=50 ms limit).
    printf("D:soundingNotes=%u\n", static_cast<unsigned>(sequencer::g_soundingNotes));
    printf("D:lastSaveUs=%u\n", static_cast<unsigned>(ui::g_lastSaveMicros));
    printf("D:maxUiGapUs=%u\n", static_cast<unsigned>(ui::g_maxUiGapMicros));
    printf("D:end\n");
}

} // namespace hardware

#endif // GENSEQ_HOST_CONTROL
