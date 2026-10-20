// performance-safe-foundation task 8.2 — bounded nonblocking save service.
//
// Validates Requirements 8.5-8.9 against persistence::SaveService over the
// in-memory FakeFlashStore:
//   - begin() captures the accepted Working State; a second begin() while in
//     flight is refused (no overlapping saves).
//   - service() pumps the save in bounded steps and reaches a DISTINCT terminal
//     outcome (SUCCESS vs a failure) the UI maps to its matrix feedback (8.9).
//   - the snapshot is immutable: editing Working State after begin() does not
//     change what gets written (8.1-8.4 interplay).
//   - a failed flash write yields a Failed terminal state with no SUCCESS.
//   - the UI/input service-gap tracker records the longest gap between pumps
//     (8.6/8.7 responsiveness metric).
//   - unsafe coordination (injected failure) performs no SUCCESS write.
//
// Standalone example (own main(), no RapidCheck); built unconditionally; excluded from glob.

#include "SaveService.h"
#include "PersistenceManager.h"
#include "PersistableConfig.h"
#include "persistence/FakeFlashStore.h"

#include <cstdint>
#include <cstdio>

using persistence::SaveService;
using persistence::SaveState;
using persistence::SaveOutcome;
using persistence::PersistenceManager;
using persistence::PersistableConfig;

constexpr std::size_t kFakeCap = 8192;  // mirrors firmware STORAGE_SIZE (FlashStore.h is SDK-only)

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

PersistableConfig makeConfig(uint16_t bpm, uint8_t firstPitch) {
    PersistableConfig c{};
    c.bpm = bpm; c.patternCount = 1; c.midiClockEnabled = 1;
    c.patterns[0].pitchCount = 1; c.patterns[0].pitches[0] = firstPitch;
    c.patterns[0].active = 1;
    return c;
}

// Happy path: begin -> pump -> Done(SUCCESS); the bytes are written.
void test_success_path() {
    FakeFlashStore store(kFakeCap);
    PersistenceManager mgr(store);
    SaveService svc(mgr);

    check(svc.state() == SaveState::Idle, "starts Idle");
    check(svc.begin(makeConfig(120, 60)), "begin accepted");
    check(svc.inProgress(), "in progress after begin");

    // Pump once per UI iteration (bounded steps). Reaches terminal.
    uint64_t t = 1000;
    while (svc.service(t)) t += 1000;
    check(svc.succeeded(), "reaches Done on success");
    check(svc.outcome() == SaveOutcome::SUCCESS, "outcome SUCCESS");
    check(store.writeCount() == 1, "exactly one flash write performed");

    svc.acknowledge();
    check(svc.state() == SaveState::Idle, "acknowledge returns to Idle");
}

// No overlapping saves: a second begin() while in flight is refused.
void test_no_overlapping_saves() {
    FakeFlashStore store(kFakeCap);
    PersistenceManager mgr(store);
    SaveService svc(mgr);
    check(svc.begin(makeConfig(120, 60)), "first begin accepted");
    check(!svc.begin(makeConfig(90, 48)), "second begin refused while in flight");
}

// Immutability: editing Working State after begin() does not change the write.
void test_snapshot_immutable() {
    FakeFlashStore store(kFakeCap);
    PersistenceManager mgr(store);
    SaveService svc(mgr);

    PersistableConfig working = makeConfig(120, 60);
    svc.begin(working);
    working.bpm = 200;                 // live edit AFTER begin
    working.patterns[0].pitches[0] = 99;
    uint64_t t = 1000;
    while (svc.service(t)) t += 1000;
    check(svc.succeeded(), "save completes");

    // Load back: the written config reflects the SNAPSHOT (120/60), not the edit.
    PersistableConfig loaded{};
    mgr.load(loaded);
    check(loaded.bpm == 120 && loaded.patterns[0].pitches[0] == 60,
          "written bytes reflect the immutable snapshot, not the post-begin edit");
}

// Failure path: an injected flash failure yields Failed, no SUCCESS.
void test_failure_path() {
    FakeFlashStore store(kFakeCap);
    PersistenceManager mgr(store);
    SaveService svc(mgr);
    store.injectFailure(persistence::FlashResult::VERIFY_MISMATCH);

    svc.begin(makeConfig(120, 60));
    uint64_t t = 1000;
    while (svc.service(t)) t += 1000;
    check(svc.failed(), "reaches Failed on injected flash failure");
    check(svc.outcome() == SaveOutcome::FAIL_FLASH, "distinct FAIL_FLASH outcome (8.9)");
    check(!svc.succeeded(), "never reports success on failure (no unsafe success write)");
}

// Gap tracking: the longest inter-pump gap is recorded (8.6/8.7 proxy).
void test_service_gap_metric() {
    FakeFlashStore store(kFakeCap);
    PersistenceManager mgr(store);
    SaveService svc(mgr);
    svc.begin(makeConfig(120, 60));
    svc.service(1000);
    svc.service(1000 + 40000);  // 40 ms gap
    check(svc.maxServiceGapMicros() == 40000, "records the max inter-pump gap");
    svc.resetGapMetric();
    check(svc.maxServiceGapMicros() == 0, "gap metric reset");
}

}  // namespace

int main() {
    test_success_path();
    test_no_overlapping_saves();
    test_snapshot_immutable();
    test_failure_path();
    test_service_gap_metric();

    if (failures == 0) {
        std::printf("example_save_service: all checks passed\n");
        return 0;
    }
    std::printf("example_save_service: %d check(s) failed\n", failures);
    return 1;
}
