// performance-safe-foundation task 8.1 — Save Snapshot semantics.
//
// Validates Requirements 8.1-8.4 against persistence::SaveSnapshot:
//   - 8.1: capture() takes one immutable, internally consistent copy of accepted
//     Working State.
//   - 8.2: an accepted change already present in the Working State is included,
//     even though its audible application might still be pending a boundary (we
//     model this: the caller folds accepted intent into the config BEFORE
//     capture, and the snapshot reflects it).
//   - 8.3: unkept audition drafts are excluded (the caller passes committed
//     Working State; the snapshot never sees the draft).
//   - 8.4: a later edit to the live Working State CANNOT mutate an in-progress
//     snapshot — the capture is a by-value freeze.
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "SaveSnapshot.h"
#include "PersistableConfig.h"

#include <cstdint>
#include <cstdio>

using persistence::SaveSnapshot;
using persistence::PersistableConfig;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

PersistableConfig makeConfig(uint16_t bpm, uint8_t patternCount, uint8_t firstPitch) {
    PersistableConfig c{};
    c.bpm = bpm;
    c.patternCount = patternCount;
    c.midiClockEnabled = 1;
    if (patternCount > 0) {
        c.patterns[0].pitchCount = 1;
        c.patterns[0].pitches[0] = firstPitch;
        c.patterns[0].active = 1;
    }
    return c;
}

// 8.1: capture freezes a valid, equal copy.
void test_capture_is_valid_copy() {
    SaveSnapshot snap;
    check(!snap.valid(), "a fresh snapshot is invalid until capture");
    PersistableConfig working = makeConfig(120, 2, 60);
    snap.capture(working);
    check(snap.valid(), "capture makes the snapshot valid");
    check(snap.config() == working, "captured snapshot equals the accepted working state");
}

// 8.4: later edits to the live working state do not mutate the snapshot.
void test_snapshot_immutable_after_capture() {
    SaveSnapshot snap;
    PersistableConfig working = makeConfig(120, 1, 60);
    snap.capture(working);

    // Mutate the LIVE working state after capture (a new live edit).
    working.bpm = 200;
    working.patterns[0].pitches[0] = 72;
    working.patternCount = 3;

    // The snapshot is frozen at its captured values (Req 8.4).
    check(snap.config().bpm == 120, "snapshot bpm unchanged by later live edit");
    check(snap.config().patterns[0].pitches[0] == 60, "snapshot pitch unchanged by later edit");
    check(snap.config().patternCount == 1, "snapshot pattern count unchanged by later edit");
    check(snap.config() != working, "snapshot diverges from the mutated live state");
}

// 8.2: an accepted change folded into the working state before capture IS
// included (even if its audible application is pending a boundary). 8.3: a draft
// NOT folded in is excluded.
void test_accepted_included_draft_excluded() {
    // committed = accepted working state (includes an accepted-but-pending edit:
    // pitch 64). The audition DRAFT (pitch 99) is intentionally NOT folded in.
    PersistableConfig committed = makeConfig(120, 1, 64);  // accepted change present
    SaveSnapshot snap;
    snap.capture(committed);
    check(snap.config().patterns[0].pitches[0] == 64,
          "accepted (pending-application) change is included in the snapshot (8.2)");
    check(snap.config().patterns[0].pitches[0] != 99,
          "unkept audition draft is excluded from the snapshot (8.3)");
}

// A fresh capture replaces the prior snapshot wholesale.
void test_recapture_replaces() {
    SaveSnapshot snap;
    snap.capture(makeConfig(120, 1, 60));
    snap.capture(makeConfig(90, 2, 48));
    check(snap.config().bpm == 90 && snap.config().patternCount == 2,
          "a new capture replaces the previous snapshot");
    snap.clear();
    check(!snap.valid(), "clear invalidates the snapshot");
}

}  // namespace

int main() {
    test_capture_is_valid_copy();
    test_snapshot_immutable_after_capture();
    test_accepted_included_draft_excluded();
    test_recapture_replaces();

    if (failures == 0) {
        std::printf("example_save_snapshot: all checks passed\n");
        return 0;
    }
    std::printf("example_save_snapshot: %d check(s) failed\n", failures);
    return 1;
}
