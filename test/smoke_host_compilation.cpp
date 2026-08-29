// Feature: midi-output-interface, Smoke test: host compilation with zero Pico SDK libraries
//
// Validates: Requirements 1.3, 3.2, 5.1, 5.5.
//
// Requirement 1.3: THE IMidiOutput header SHALL include nothing from the Pico SDK, so it
//   compiles in any standard C++ toolchain.
// Requirement 3.2: THE refactored Sequencer's MIDI emission SHALL not reference any Pico SDK
//   UART symbol, so the Sequencer class header/TU is usable host-side.
// Requirement 5.1: THE IMidiOutput SHALL expose exactly one pure virtual write operation,
//   making it an abstract type.
// Requirement 5.5: THE emission logic SHALL compile host-side against IMidiOutput with zero
//   Pico SDK libraries linked.
//
// This is a SMOKE test (not property-based). Its VALUE is not in what main() does at runtime —
// running it is trivially green. The value is that this translation unit COMPILES and LINKS:
//   * It #includes "IMidiOutput.h" standalone, proving the interface header is includable
//     SDK-free (Req 1.3, 5.1, 5.5).
//   * It #includes "sequencer.h", proving the Sequencer class header is usable host-side with
//     no Pico SDK UART symbol pulled in (Req 3.2).
//   * The CMake target links the genseq_emission library (the real sequencer.cpp + common/*.cpp
//     objects) and NONE of the Pico SDK hardware libraries. If any emission TU pulled in an
//     unstubbed Pico SDK symbol, the LINK would fail — making this a genuine end-to-end
//     host-compilation-and-link smoke check.
//
// The static_asserts below make the compile-time contract explicit and self-documenting.

#include "IMidiOutput.h"
#include "sequencer.h"

#include <type_traits>
#include <cstdio>

// Compile-time contract: IMidiOutput is an abstract, polymorphic transport with a virtual
// destructor (Req 5.1 / 1.2). These fire at compile time, so a violation breaks the build —
// which is exactly the check this smoke test exists to perform.
static_assert(std::is_abstract<sequencer::IMidiOutput>::value,
              "IMidiOutput must be abstract");
static_assert(std::has_virtual_destructor<sequencer::IMidiOutput>::value,
              "IMidiOutput must have a virtual destructor");

int main() {
    // Reaching this line means the interface header and the Sequencer header both compiled
    // host-side, and this TU linked against the emission logic with zero Pico SDK libraries.
    std::printf(
        "PASS: emission logic + IMidiOutput interface compiled and linked with zero Pico "
        "SDK libraries\n");
    return 0;
}
