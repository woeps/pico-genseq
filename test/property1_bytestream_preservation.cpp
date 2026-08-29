// Feature: midi-output-interface, Property 1: byte-stream order and value preservation through IMidiOutput
//
// Validates: Requirements 1.4, 2.4, 5.2.
//
// Property 1 (design.md "Correctness Properties"): For any finite sequence of MIDI bytes
// written one at a time through an IMidiOutput reference into a recording implementation, the
// recorded sequence equals the written sequence exactly — same length, same values, same
// order, with no byte dropped, duplicated, altered, or reordered. The single-byte case
// establishes that a write through the base reference dispatches exactly that one byte to the
// concrete implementation (Req 1.4).
//
// -------------------------------------------------------------------------------------------
// Shared test-runner arrangement (see test/test_main.cpp):
//
// RapidCheck's bare `rc::check` does NOT bundle a test-runner main(), and this feature's
// property tests are swept into a SINGLE `genseq_property_tests` executable by
// test/CMakeLists.txt's `file(GLOB test/*.cpp)`. Two files each defining their own main()
// would fail to link (duplicate symbol). To avoid that WITHOUT adding a new dependency
// (RapidCheck's Catch2/gtest integration is intentionally not used), each property file
// exposes a `bool runPropertyN()` entry point (true = all properties passed) and a single
// shared test/test_main.cpp aggregates them under one main(). This file therefore defines
// `runProperty1()` and NO main().
// -------------------------------------------------------------------------------------------

#include "RecordingMidiOutput.h"

#include <rapidcheck.h>

#include <cstdint>
#include <vector>

bool runProperty1() {
    bool ok = true;

    // --- Property 1 (primary): whole-sequence order & value preservation -----------------
    // For any random finite byte sequence, writing each byte one at a time through an
    // IMidiOutput& bound to a RecordingMidiOutput yields a recorded stream equal EXACTLY
    // (length, values, order) to the written input. RapidCheck runs 100 cases by default.
    ok &= rc::check(
        "Property 1: bytes written one at a time through an IMidiOutput& are recorded "
        "exactly — same length, values, and order, nothing dropped/duplicated/altered/reordered",
        [](const std::vector<uint8_t>& input) {
            RecordingMidiOutput recording;
            // Bind through the BASE reference to exercise polymorphic dispatch (Req 1.4).
            sequencer::IMidiOutput& out = recording;

            for (uint8_t byte : input) {
                out.write(byte);
            }

            // Exact-equality: any dropped, duplicated, altered, or reordered byte fails here.
            RC_ASSERT(recording.record() == input);
        });

    // --- Property 1 (single-byte dispatch): Req 1.4 -------------------------------------
    // A single write through the base reference dispatches exactly that one byte to the
    // concrete implementation: the record has size 1 and holds the written value.
    ok &= rc::check(
        "Property 1 (single-byte dispatch): one write through IMidiOutput& records exactly "
        "that one byte (size 1, value preserved)",
        [](uint8_t byte) {
            RecordingMidiOutput recording;
            sequencer::IMidiOutput& out = recording;

            out.write(byte);

            RC_ASSERT(recording.record().size() == 1u);
            RC_ASSERT(recording.record()[0] == byte);
        });

    return ok;
}
