// ui-integration task 4 — T2 Add Global Scale to Timeline (FR12, FR13, FR14).
//
// Validates the TransportTimeline global scale storage and accessors:
//
//   - scaleRoot_ / scaleTemplateIndex_ defaults (root 0-11 as a pitch class,
//     template index inside common::SCALE_TEMPLATES);
//   - setScale() accepting only valid combinations (root a pitch class
//     0-11, template index naming a real built-in template) and rejecting
//     everything else without changing stored state;
//   - scaleRoot() / scaleTemplateIndex() / scale() returning the stored
//     values and the derived common::Scale;
//   - setScale() + scaleRoot()/scaleTemplateIndex()/scale() round-trip.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/ so
// the property-test glob (test/*.cpp) never sweeps it; registered in the host
// test build under the ctest name "test_transport_timeline_scale".
//
// The scale is a TRUE GLOBAL setting stored in a single source
// (TransportTimeline) and modulatable with Global scope alongside BPM
// (AC10/AC11). No data structures are shared between cores — the UI (Core0)
// and the Sequencer (Core1) have separate optimized state and communicate
// exclusively via the command queue.

#include "TransportTimeline.h"
#include "Scale.h"
#include "ScaleTemplates.h"

#include <cstdint>
#include <cstdio>

using common::Scale;
using sequencer::TransportTimeline;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// --- 1: factory default scale values ----------------------------------------

void test_factory_defaults() {
    TransportTimeline tl;
    check(tl.scaleRoot() == 0, "default scale root is 0 (C)");
    check(tl.scaleTemplateIndex() == 0, "default scale template index is 0");
    check(tl.scale().root() == 0 && tl.scale().templateIndex() == 0,
          "derived default Scale matches the stored fields");
}

// --- 2: setScale() accepts valid combinations and stores them ---------------

void test_set_and_read() {
    TransportTimeline tl;

    // Valid combinations are accepted and stored; the derived Scale matches.
    check(tl.setScale(3, 5), "setScale(3, 5) accepted");
    check(tl.scaleRoot() == 3 && tl.scaleTemplateIndex() == 5,
          "setScale stores root and template index");
    check(tl.scale().root() == 3 && tl.scale().templateIndex() == 5,
          "derived Scale matches the stored fields after setScale");

    // Extremes of the valid domain.
    check(tl.setScale(11, common::scaleTemplateCount() - 1),
          "setScale(11, last template) accepted");
    check(tl.scaleRoot() == 11 && tl.scaleTemplateIndex() == common::scaleTemplateCount() - 1,
          "setScale stores extreme valid values");
    check(tl.scale().root() == 11 && tl.scale().templateIndex() == common::scaleTemplateCount() - 1,
          "derived Scale matches the extreme stored values");

    // Round-trip back to the first template.
    check(tl.setScale(0, 0), "setScale(0, 0) accepted");
    check(tl.scaleRoot() == 0 && tl.scaleTemplateIndex() == 0,
          "setScale round-trips to the default scale");
}

// --- 3: setScale() rejects invalid values without changing state -------------

void test_rejection() {
    TransportTimeline tl;
    check(tl.setScale(7, 2), "setScale(7, 2) accepted to seed state");

    // Root must be a pitch class 0-11.
    check(!tl.setScale(12, 2), "root 12 rejected");
    check(!tl.setScale(255, 2), "root 255 rejected");
    // Template index must name a real built-in template.
    check(!tl.setScale(7, common::scaleTemplateCount()), "template index == count rejected");
    check(!tl.setScale(7, common::scaleTemplateCount() + 7), "out-of-range template rejected");

    // Rejection, not rounding (Req 1.7): the stored values are unchanged.
    check(tl.scaleRoot() == 7 && tl.scaleTemplateIndex() == 2,
          "rejected calls leave stored values unchanged");
    check(tl.scale().root() == 7 && tl.scale().templateIndex() == 2,
          "rejected calls leave the derived Scale unchanged");
}

// --- 4: the scale survives transport operations (true global single source) --

void test_scale_independent_of_transport() {
    TransportTimeline tl;
    check(tl.setScale(9, 3), "setScale(9, 3) accepted");
    check(tl.setBpm(96), "setBpm(96) accepted");
    tl.start(1000);
    tl.advance();
    tl.advance();
    check(tl.scaleRoot() == 9 && tl.scaleTemplateIndex() == 3,
          "scale storage is independent of BPM / tick state");
    check(tl.tickIndex() == 2 && tl.running(), "transport still works after setScale");
}

}  // namespace

int main() {
    test_factory_defaults();
    test_set_and_read();
    test_rejection();
    test_scale_independent_of_transport();

    if (failures == 0) {
        std::printf("test_transport_timeline_scale: all checks passed\n");
        return 0;
    }
    std::printf("test_transport_timeline_scale: %d check(s) failed\n", failures);
    return 1;
}
