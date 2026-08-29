#pragma once
// Minimal host stub for Pico SDK UART types used only in declarations
// (uart_inst_t* / uint in createSequencerTask's signature). No hardware
// functions are needed host-side because UartMidiOutput.cpp and
// sequencer_task.cpp are NOT compiled in the host test build.
//
// In the real SDK, <hardware/uart.h> transitively exposes the time API /
// absolute_time_t. We mirror that here so sequencer.h's use of
// absolute_time_t (member lastTickTime) resolves without editing the source.
#include <cstdint>
#include "pico/time.h"

typedef struct uart_inst uart_inst_t;
typedef unsigned int uint;
