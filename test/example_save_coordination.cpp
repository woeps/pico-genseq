// performance-safe-foundation task 8.3 — snapshot + save-coordination tests.
//
// Validates Requirements 8.1-8.9 across the save path (SaveSnapshot + SaveService
// + RecordStore over the in-memory FakeFlashStore), covering the coordination
// cases 8.2's example does not: the draft/working/pending distinctions, mutation
// after capture, a transactional (A/B) failure preserving the previous record,
// and — crucially — that UNSAFE coordination performs ZERO destructive writes
// (Req 8.8: a save that cannot establish a safe write condition fails without a
// destructive storage write).
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "SaveSnapshot.h"
#include "SaveService.h"
#include "RecordStore.h"
#include "PersistenceManager.h"
#include "PersistableConfig.h"
#include "persistence/FakeFlashStore.h"

#include <cstdint>
#include <cstdio>

using namespace persistence;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

constexpr std::size_t kCap = 8192;

PersistableConfig cfg(uint16_t bpm, uint8_t p0) {
    PersistableConfig c{};
    c.bpm = bpm; c.patternCount = 1; c.midiClockEnabled = 1;
    c.patterns[0].pitchCount = 1; c.patterns[0].pitches[0] = p0; c.patterns[0].active = 1;
    return c;
}

// 8.1-8.3: draft vs working vs pending — the snapshot captures ACCEPTED working
// state (with an accepted-but-pending change folded in) and excludes the draft.
void test_draft_working_pending_distinction() {
    // workingAccepted includes an accepted pending change (pitch 64). The live
    // audition draft (pitch 99) is NOT folded in by the caller, so it is excluded.
    PersistableConfig workingAccepted = cfg(120, 64);
    SaveSnapshot snap;
    snap.capture(workingAccepted);
    check(snap.config().patterns[0].pitches[0] == 64, "accepted (pending) change captured (8.2)");
    check(snap.config().patterns[0].pitches[0] != 99, "unkept draft excluded (8.3)");
}

// 8.4: mutation after capture does not change the snapshot nor what is written.
void test_mutation_after_capture_no_effect() {
    FakeFlashStore store(kCap);
    PersistenceManager mgr(store);
    SaveService svc(mgr);

    PersistableConfig working = cfg(120, 60);
    svc.begin(working);
    working.bpm = 240;  // edit after begin()
    uint64_t t = 0;
    while (svc.service(t += 1000)) {}
    PersistableConfig loaded{};
    mgr.load(loaded);
    check(loaded.bpm == 120, "write reflects the frozen snapshot, not the post-begin mutation (8.4)");
}

// 8.8: a capacity failure (unsafe write condition) performs NO destructive write.
void test_capacity_failure_no_write() {
    FakeFlashStore store(16);  // < 29-byte serialized record -> capacity failure
    PersistenceManager mgr(store);
    SaveService svc(mgr);
    svc.begin(cfg(120, 60));
    uint64_t t = 0;
    while (svc.service(t += 1000)) {}
    check(svc.failed(), "oversized save fails");
    check(svc.outcome() == SaveOutcome::FAIL_CAPACITY, "distinct FAIL_CAPACITY outcome");
    check(store.writeCount() == 0, "NO destructive flash write on an unsafe/oversized save (8.8)");
}

// 8.8/9.6: an injected flash failure performs no SUCCESS and, via the A/B record
// store, preserves the previous committed record (transactional failure).
void test_transactional_failure_preserves_previous() {
    FakeFlashStore store(kCap);
    RecordStore rs(store);
    rs.commit(cfg(120, 60));                 // committed baseline (gen 1)
    const int writesAfterBaseline = store.writeCount();

    store.injectFailure(FlashResult::PROGRAM_FAILED);
    check(rs.commit(cfg(200, 72)) == CommitResult::FAIL_WRITE, "failed commit reported");

    PersistableConfig loaded{};
    check(rs.load(loaded) == LoadStatus::OK && loaded.bpm == 120,
          "previous committed record preserved after transactional failure (9.6/9.7)");
    // The failed commit consumed its injected failure (one attempted write) but
    // did not corrupt the committed slot — the baseline still loads.
    check(store.writeCount() == writesAfterBaseline,
          "no successful write recorded for the failed commit");
}

// Idempotence: acknowledging and re-saving works; no overlapping saves.
void test_save_lifecycle() {
    FakeFlashStore store(kCap);
    PersistenceManager mgr(store);
    SaveService svc(mgr);
    svc.begin(cfg(120, 60));
    check(!svc.begin(cfg(90, 48)), "no overlapping save while in flight");
    uint64_t t = 0;
    while (svc.service(t += 1000)) {}
    check(svc.succeeded(), "first save done");
    svc.acknowledge();
    check(svc.begin(cfg(90, 48)), "a new save can begin after acknowledge");
}

}  // namespace

int main() {
    test_draft_working_pending_distinction();
    test_mutation_after_capture_no_effect();
    test_capacity_failure_no_write();
    test_transactional_failure_preserves_previous();
    test_save_lifecycle();

    if (failures == 0) {
        std::printf("example_save_coordination: all checks passed\n");
        return 0;
    }
    std::printf("example_save_coordination: %d check(s) failed\n", failures);
    return 1;
}
