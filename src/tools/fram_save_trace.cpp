// =============================================================================
// FRAM save-path transparency trace harness (task 9.3, Route A)
// =============================================================================
//
// Purpose
// -------
// Prove on real silicon that an external-FRAM save does NOT freeze sequencing
// the way the internal-flash save does (task 9.2 measured a ~37 ms both-core
// freeze: zero ticks, zero MIDI for the whole write). FRAM is plain SPI with no
// XIP-disable and no core-park, so a FRAM write should be transparent: ticks and
// MIDI must keep flowing straight through it.
//
// This is a FOCUSED on-target experiment, not the full 10.3 backend-selector
// integration. It drives the REAL write path (`Mb85rsFram::eraseAndProgram` over
// the real `Mb85rsFramSpi` SPI1 transport) with the REAL serialized SaveRecord
// (worst case: 15 patterns, full sets), while a second core emits the exact
// 9.2-style Tick+MIDI waveform on the same trace pins the BitScope already
// watches. The verdict is read off the scope: do Tick (GP2 / L0) and MIDI
// (GP3) keep toggling across the Save-pin-high (GP5 / L3) window?
//
// Trace pins (identical to the 9.2 build-trace rig):
//   GP2 = Tick       (L0)   core0 pulses once per sequencer tick
//   GP3 = MIDI              core0 pulses per emitted MIDI byte-burst
//   GP5 = Save       (L3)   core1 holds HIGH for the whole FRAM write  <-- trigger
//   (GP4 Operation / GP6 Fault unused here)
//
// FRAM wiring (verified, TODO.md 9.3): SPI1 GP10=SCK GP11=MOSI GP12=MISO GP13=CS.
//
// stdio: UART0 (GP0/GP1 -> picoprobe bridge) AND USB-CDC. The harness also
// prints the measured FRAM write duration each cycle, so the latency number is
// captured even without the scope.
//
// Build (gated; never affects the default genseq build):
//   cmake -B build-fram -DGENSEQ_BUILD_FRAM_SAVE_TRACE=ON
//   cmake --build build-fram --target fram_save_trace
// Flash over SWD: program build-fram/fram_save_trace.elf verify reset exit
// Reversible: reflash build/genseq.uf2 (then power-cycle per the OpenOCD quirk).
//
// IMPORTANT: this harness drives SPI1/GP10-13 and the trace pins only. It does
// NOT touch internal flash or the live save path, so it cannot corrupt a stored
// config. The core0 "sequencer" here is a fixed-cadence PROXY emitter, not the
// real Sequencer object — enough to reproduce the 9.2 waveform for the
// transparency question without pulling the whole app into this binary.
// =============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"

#include "persistence/Mb85rsFram.h"
#include "persistence/Mb85rsFramSpi.h"
#include "persistence/PersistableConfig.h"
#include "persistence/SaveRecord.h"

namespace {

// --- Trace pins (match build-trace / 9.2 rig) -------------------------------
constexpr uint TICK_PIN = 2;   // L0
constexpr uint MIDI_PIN = 3;
constexpr uint SAVE_PIN = 5;   // L3  <-- scope trigger

// --- 255 BPM worst-case tick cadence ----------------------------------------
// 96 PPQN internal resolution. tick period = 60 / (BPM * PPQN) seconds.
//   255 BPM: 60 / (255 * 96) = 2451 us/tick.
constexpr uint32_t TICK_PERIOD_US = 2451;
// Worst-case load the 9.2 rig used: 10 active patterns, each emitting a
// note-on/off pair on a tick boundary. Model the MIDI wire cost as a short
// burst of byte-width pulses so the scope sees comparable wire density. A MIDI
// byte at 31250 baud is ~320 us (10 bits). 10 patterns * ~2 msgs * 3 bytes is a
// lot; to keep core0 free-running we emit a representative burst per tick.
constexpr int MIDI_BYTES_PER_TICK = 6;   // representative worst-case burst
constexpr uint32_t MIDI_BYTE_US = 320;   // 1 MIDI byte @31250 baud (10 bits)
// Tick strobe HIGH width: wide enough to be sampled at coarse capture
// resolution, yet comfortably under one tick period (2451 us) so the loop
// never overruns the next-boundary schedule. The MIDI burst fits inside it.
constexpr uint32_t TICK_HIGH_US = 1200;

volatile bool g_core0_running = false;

// ---------------------------------------------------------------------------
// core0: fixed-cadence Tick + MIDI emitter reproducing the 9.2 waveform.
// Runs forever, independent of core1's FRAM writes. If a FRAM write were to
// stall this core (it must NOT), the Tick pulses on GP2 would stop during the
// GP5-high window — exactly the ~37 ms gap internal flash produced.
// ---------------------------------------------------------------------------
void core0_sequencer_proxy() {
    g_core0_running = true;
    absolute_time_t next = get_absolute_time();
    for (;;) {
        next = delayed_by_us(next, TICK_PERIOD_US);

        // Tick (GP2 / L0): raise at the tick boundary and hold HIGH for a wide,
        // fixed window so the logic analyzer reliably samples it. The MIDI burst
        // is emitted DURING this window (separate pin), so one iteration is
        // bounded well under the tick period and the cadence never drifts.
        gpio_put(TICK_PIN, 1);

        // MIDI burst (GP3): byte-width pulses standing in for the note-on/off
        // traffic of the active patterns on this tick, emitted inside the
        // tick-high window.
        for (int i = 0; i < MIDI_BYTES_PER_TICK; ++i) {
            gpio_put(MIDI_PIN, 1);
            busy_wait_us(MIDI_BYTE_US / 2);
            gpio_put(MIDI_PIN, 0);
            busy_wait_us(MIDI_BYTE_US / 2);
        }
        // Pad the tick-high window out to TICK_HIGH_US total (>= the MIDI burst).
        const uint32_t midi_us = MIDI_BYTES_PER_TICK * MIDI_BYTE_US;
        if (TICK_HIGH_US > midi_us) busy_wait_us(TICK_HIGH_US - midi_us);
        gpio_put(TICK_PIN, 0);

        // Sleep until the next tick boundary. If the FRAM write on core1 had any
        // cross-core stall effect, it would show as jitter/gaps here.
        sleep_until(next);
    }
}

// Build a worst-case PersistableConfig (all 15 patterns active, full sets) so
// the serialized SaveRecord is the largest the codec produces.
persistence::PersistableConfig make_worst_case_config() {
    persistence::PersistableConfig cfg{};
    cfg.patternCount = persistence::MAX_PATTERNS;  // 15
    cfg.bpm = 255;
    cfg.midiClockEnabled = 1;
    for (uint8_t p = 0; p < persistence::MAX_PATTERNS; ++p) {
        auto& pc = cfg.patterns[p];
        pc.gateAlgorithm = 1;
        pc.gateSteps = 64;
        pc.gatePulses = 32;
        pc.gateRotation = 3;
        pc.gateNoteLength = 2;
        pc.gateLength = 75;
        pc.pitchCount = persistence::MAX_SET_LEN;     // 16
        pc.pitchOrder = 0;
        pc.velocityCount = persistence::MAX_SET_LEN;  // 16
        pc.velocityOrder = 0;
        for (uint8_t i = 0; i < persistence::MAX_SET_LEN; ++i) {
            pc.pitches[i] = static_cast<uint8_t>(36 + i);
            pc.velocities[i] = static_cast<uint8_t>(64 + i);
        }
        pc.midiChannel = static_cast<uint8_t>((p % 16) + 1);
        pc.active = 1;
    }
    return cfg;
}

}  // namespace

int main() {
    stdio_init_all();
    // Give a USB-CDC host a moment to attach (UART path needs no wait).
    for (int i = 0; i < 30 && !stdio_usb_connected(); ++i) sleep_ms(100);

    // Trace pins as GPIO outputs, idle low.
    for (uint pin : {TICK_PIN, MIDI_PIN, SAVE_PIN}) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
    }

    std::printf("\n=== FRAM save-path transparency trace (task 9.3, Route A) ===\n");
    std::printf("Tick=GP2(L0) MIDI=GP3 Save=GP5(L3); FRAM SPI1 GP10-13\n");
    std::printf("core0: %u us/tick (255 BPM), %d MIDI bursts/tick\n",
                TICK_PERIOD_US, MIDI_BYTES_PER_TICK);

    // Bring up the FRAM backend over the real SPI1 transport. Region sized to
    // hold the stress record (several KB) as well as the real 676-byte save.
    persistence::Mb85rsFramSpi fram_spi;  // default Pins{} = SPI1 GP10-13
    fram_spi.init();
    persistence::Mb85rsFram fram(fram_spi, /*regionSize=*/16384);

    // Confirm the part is really there before trusting the write path.
    uint8_t id[4] = {0};
    fram.readDeviceId(id, sizeof(id));
    std::printf("RDID: 0x%02X 0x%02X 0x%02X 0x%02X -> %s\n",
                id[0], id[1], id[2], id[3],
                id[0] == 0x04 ? "Fujitsu OK" : "WIRING FAULT");
    if (id[0] != 0x04) {
        std::printf("Aborting: FRAM not detected. Check wiring.\n");
        for (;;) sleep_ms(1000);
    }

    // Serialize the worst-case record with the REAL codec (the true save size).
    const persistence::PersistableConfig cfg = make_worst_case_config();
    const std::vector<uint8_t> record = persistence::serialize(cfg);
    std::printf("SaveRecord: %u bytes (15 patterns, full sets)\n",
                static_cast<unsigned>(record.size()));

    // A larger STRESS record (real record tiled to ~12 KB). The real 676-byte
    // save is shorter than one 2.451 ms tick period, so at most one tick edge
    // can fall inside it — too tight to SHOW continuity. The stress write takes
    // long enough (~32 ms) to straddle many tick periods, making "ticks keep
    // flowing through the write" visually unambiguous on the scope. It measures
    // the SAME write path; only the length differs.
    std::vector<uint8_t> stress(12288);
    for (std::size_t i = 0; i < stress.size(); ++i) stress[i] = record[i % record.size()];

    // Launch the free-running sequencer waveform on core1 so core0 is free to
    // drive FRAM writes. (Either core works; the transparency claim is that
    // NEITHER core stalls. We put the emitter on core1 and the write on core0,
    // which is the harder case: core0 historically parks core1 during a flash
    // write, so if any residual cross-core coupling existed it would gap the
    // core1 emitter here.)
    multicore_launch_core1(core0_sequencer_proxy);
    while (!g_core0_running) tight_loop_contents();
    sleep_ms(200);  // let a few ticks establish a clean baseline on the scope

    std::printf("Emitter live. Alternating real (676B) and stress (12KB) FRAM\n");
    std::printf("writes once/sec; trigger scope on GP5 rising.\n");

    uint32_t cycle = 0;
    for (;;) {
        sleep_ms(1000);

        // Even cycles: the STRESS write (long, spans many tick periods -> best
        // for the scope). Odd cycles: the REAL 676-byte save (true latency).
        const bool stress_cycle = (cycle % 2) == 0;
        const uint8_t* data = stress_cycle ? stress.data() : record.data();
        const std::size_t len = stress_cycle ? stress.size() : record.size();

        // --- The measured event: a real FRAM write bracketed by the Save pin ---
        const absolute_time_t t0 = get_absolute_time();
        gpio_put(SAVE_PIN, 1);                                    // GP5 high (L3 trigger)
        const auto result = fram.eraseAndProgram(data, len);
        gpio_put(SAVE_PIN, 0);                                    // GP5 low
        const absolute_time_t t1 = get_absolute_time();

        const int64_t us = absolute_time_diff_us(t0, t1);
        std::printf("cycle %lu: %s FRAM write %u B = %lld us (%.3f ms) result=%s\n",
                    static_cast<unsigned long>(cycle++),
                    stress_cycle ? "STRESS" : "real  ",
                    static_cast<unsigned>(len),
                    static_cast<long long>(us), us / 1000.0,
                    result == persistence::FlashResult::OK ? "OK"
                    : result == persistence::FlashResult::VERIFY_MISMATCH ? "VERIFY_MISMATCH"
                    : "PROGRAM_FAILED");
    }
}
