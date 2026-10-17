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
