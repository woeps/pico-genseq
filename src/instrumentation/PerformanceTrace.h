#pragma once

#ifndef GENSEQ_PERFORMANCE_TRACE_ENABLED
#define GENSEQ_PERFORMANCE_TRACE_ENABLED 0
#endif

#ifndef GENSEQ_TRACE_TICK_PIN
#define GENSEQ_TRACE_TICK_PIN -1
#endif

#ifndef GENSEQ_TRACE_MIDI_PIN
#define GENSEQ_TRACE_MIDI_PIN -1
#endif

#ifndef GENSEQ_TRACE_OPERATION_PIN
#define GENSEQ_TRACE_OPERATION_PIN -1
#endif

#ifndef GENSEQ_TRACE_SAVE_PIN
#define GENSEQ_TRACE_SAVE_PIN -1
#endif

#ifndef GENSEQ_TRACE_FAULT_PIN
#define GENSEQ_TRACE_FAULT_PIN -1
#endif

#if GENSEQ_PERFORMANCE_TRACE_ENABLED
#include "hardware/gpio.h"
#include "pico/time.h"
#endif

// Optional: widen the Tick pulse (GP2) by holding it high this many extra
// microseconds in tickEnd(), so a logic analyzer sampling at tens of us/sample
// reliably latches the otherwise ~5.5 us serviceTick pulse. Trace-build only;
// defaults to 0 (no change). The RISING edge still marks the true tick
// boundary and the hold stays << the tick period, so cadence is unaffected —
// this is a measurement aid for the task-12.4 live-save capture, nothing more.
#ifndef GENSEQ_TRACE_TICK_WIDEN_US
#define GENSEQ_TRACE_TICK_WIDEN_US 0
#endif

namespace instrumentation {

/// Compile-time GPIO traces for logic-analyzer measurements.
///
/// With GENSEQ_PERFORMANCE_TRACE_ENABLED=0 every method compiles to a no-op and
/// the header has no Pico SDK dependency. Individual traces remain disabled
/// unless their pin is configured to a non-negative GPIO number.
class PerformanceTrace final {
public:
    static constexpr bool enabled() {
        return GENSEQ_PERFORMANCE_TRACE_ENABLED != 0;
    }

    static void initialize() {
#if GENSEQ_PERFORMANCE_TRACE_ENABLED
        // Initialization is idempotent so repeated composition-root calls can
        // never reset a trace level while core1 is using the pins.
        static bool initialized = false;
        if (initialized) return;
        initialized = true;
#if GENSEQ_TRACE_TICK_PIN >= 0
        initializePin(GENSEQ_TRACE_TICK_PIN);
#endif
#if GENSEQ_TRACE_MIDI_PIN >= 0
        initializePin(GENSEQ_TRACE_MIDI_PIN);
#endif
#if GENSEQ_TRACE_OPERATION_PIN >= 0
        initializePin(GENSEQ_TRACE_OPERATION_PIN);
#endif
#if GENSEQ_TRACE_SAVE_PIN >= 0
        initializePin(GENSEQ_TRACE_SAVE_PIN);
#endif
#if GENSEQ_TRACE_FAULT_PIN >= 0
        initializePin(GENSEQ_TRACE_FAULT_PIN);
#endif
#endif
    }

    static void tickBegin() {
#if GENSEQ_PERFORMANCE_TRACE_ENABLED && GENSEQ_TRACE_TICK_PIN >= 0
        gpio_put(GENSEQ_TRACE_TICK_PIN, 1);
#endif
    }

    static void tickEnd() {
#if GENSEQ_PERFORMANCE_TRACE_ENABLED && GENSEQ_TRACE_TICK_PIN >= 0
#if GENSEQ_TRACE_TICK_WIDEN_US > 0
        // Measurement aid: keep GP2 high long enough to be sampled, then drop.
        busy_wait_us(GENSEQ_TRACE_TICK_WIDEN_US);
#endif
        gpio_put(GENSEQ_TRACE_TICK_PIN, 0);
#endif
    }

    static void midiDispatchBegin() {
#if GENSEQ_PERFORMANCE_TRACE_ENABLED && GENSEQ_TRACE_MIDI_PIN >= 0
        gpio_put(GENSEQ_TRACE_MIDI_PIN, 1);
#endif
    }

    static void midiDispatchEnd() {
#if GENSEQ_PERFORMANCE_TRACE_ENABLED && GENSEQ_TRACE_MIDI_PIN >= 0
        gpio_put(GENSEQ_TRACE_MIDI_PIN, 0);
#endif
    }

    static void operationApplyBegin() {
#if GENSEQ_PERFORMANCE_TRACE_ENABLED && GENSEQ_TRACE_OPERATION_PIN >= 0
        gpio_put(GENSEQ_TRACE_OPERATION_PIN, 1);
#endif
    }

    static void operationApplyEnd() {
#if GENSEQ_PERFORMANCE_TRACE_ENABLED && GENSEQ_TRACE_OPERATION_PIN >= 0
        gpio_put(GENSEQ_TRACE_OPERATION_PIN, 0);
#endif
    }

    static void saveBegin() {
#if GENSEQ_PERFORMANCE_TRACE_ENABLED && GENSEQ_TRACE_SAVE_PIN >= 0
        gpio_put(GENSEQ_TRACE_SAVE_PIN, 1);
#endif
    }

    static void saveEnd() {
#if GENSEQ_PERFORMANCE_TRACE_ENABLED && GENSEQ_TRACE_SAVE_PIN >= 0
        gpio_put(GENSEQ_TRACE_SAVE_PIN, 0);
#endif
    }

    static void setFault(bool active) {
#if GENSEQ_PERFORMANCE_TRACE_ENABLED && GENSEQ_TRACE_FAULT_PIN >= 0
        gpio_put(GENSEQ_TRACE_FAULT_PIN, active ? 1 : 0);
#else
        (void)active;
#endif
    }

private:
#if GENSEQ_PERFORMANCE_TRACE_ENABLED
    static void initializePin(unsigned int pin) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
    }
#endif
};

}  // namespace instrumentation
