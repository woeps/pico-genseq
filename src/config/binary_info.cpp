// Firmware metadata embedded in the binary (Pico "binary info"), readable without running the
// device:   picotool info -a build/genseq.uf2      (or the .elf / the device in BOOTSEL mode)
//
// Three kinds of data live here or are fed from CMake:
//   - program name / version / description / URL: pico_set_program_* in CMakeLists.txt. The
//     version string carries the git revision ("0.1+<hash>[-dirty]").
//   - the pinout: bi_decl() entries below, built from the macros in config/pins.h so the
//     table can never drift from the pins the firmware actually drives.
//   - build attributes and features: the git revision and what this firmware supports.
//
// This translation unit has no code. Its only effect is the .binary_info section.

#include "pico/binary_info.h"

#include "config/pins.h"
#include "persistence/SaveRecord.h"

// ---- Pinout ---------------------------------------------------------------------------------
// MIDI DIN: UART1 at 31250 baud (8, 9).
bi_decl(bi_2pins_with_names(MIDI_UART_PIN_TX, "MIDI OUT (DIN, UART1 TX, 31250 baud)",
                            MIDI_UART_PIN_RX, "MIDI IN (UART1 RX)"))

// External FRAM config store, MB85RS2MTA on SPI1 (10..13).
bi_decl(bi_4pins_with_names(FRAM_SPI_PIN_SCK, "FRAM SCK (SPI1)",
                            FRAM_SPI_PIN_MOSI, "FRAM SI / MOSI (SPI1 TX)",
                            FRAM_SPI_PIN_MISO, "FRAM SO / MISO (SPI1 RX)",
                            FRAM_SPI_PIN_CS, "FRAM CS (software, active low)"))

// 16x16 WS2812 LED matrix, driven by PIO + DMA.
bi_decl(bi_1pin_with_name(LED_MATRIX_PIN, "WS2812 LED matrix data (16x16, PIO)"))

// Onboard status LED.
bi_decl(bi_1pin_with_name(LED_PIN, "Status LED (onboard)"))

// Optional logic-analyzer trace pins; present only when the build enabled them.
#if GENSEQ_TRACE_TICK_PIN >= 0
bi_decl(bi_1pin_with_name(GENSEQ_TRACE_TICK_PIN, "Trace: logical tick active"))
#endif
#if GENSEQ_TRACE_MIDI_PIN >= 0
bi_decl(bi_1pin_with_name(GENSEQ_TRACE_MIDI_PIN, "Trace: MIDI byte dispatch"))
#endif
#if GENSEQ_TRACE_OPERATION_PIN >= 0
bi_decl(bi_1pin_with_name(GENSEQ_TRACE_OPERATION_PIN, "Trace: sequencer operation applied"))
#endif
#if GENSEQ_TRACE_SAVE_PIN >= 0
bi_decl(bi_1pin_with_name(GENSEQ_TRACE_SAVE_PIN, "Trace: save in progress"))
#endif
#if GENSEQ_TRACE_FAULT_PIN >= 0
bi_decl(bi_1pin_with_name(GENSEQ_TRACE_FAULT_PIN, "Trace: performance fault latched"))
#endif

// ---- Build attributes and features ------------------------------------------------------------
// GENSEQ_GIT_REVISION comes from CMake (git describe --always --dirty).
#ifndef GENSEQ_GIT_REVISION
#define GENSEQ_GIT_REVISION "unknown"
#endif
bi_decl(bi_program_build_attribute("git " GENSEQ_GIT_REVISION))

// Feature text states constants; the asserts keep it honest when the constants change.
static_assert(persistence::FORMAT_VERSION == 4, "update the save-format feature text below");
static_assert(persistence::SET_LIBRARY_SIZE == 16, "update the set-library feature text below");
static_assert(persistence::MAX_PATTERNS == 15, "update the pattern-count feature text below");
static_assert(persistence::MAX_INSTRUMENTS == 16, "update the instrument feature text below");

bi_decl(bi_program_feature("MIDI clock + notes out, 15 patterns"))
bi_decl(bi_program_feature("Shared sets: 16 gate + 16 note + 16 velocity sets, 1 of each per pattern"))
bi_decl(bi_program_feature("Instruments: 16 MIDI channels, patterns shared between them"))
bi_decl(bi_program_feature("Config in external FRAM, save format v4 (loads v1-v3)"))
bi_decl(bi_program_feature("USB keyboard host input"))
