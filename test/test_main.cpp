// Feature: midi-output-interface — shared test-runner entry point.
//
// Why this file exists (shared-runner arrangement):
//
// test/CMakeLists.txt sweeps test/*.cpp into a SINGLE genseq_property_tests executable via
// file(GLOB ...). RapidCheck's bare `rc::check` does not provide a test-runner main(), so if
// each property file defined its own main() the link would fail with duplicate `main`
// symbols. Rather than pull in RapidCheck's optional Catch2/gtest integration (which would add
// a dependency), each property source exposes a `bool runPropertyN()` entry point (true = all
// of that file's properties passed) and this single translation unit owns the one and only
// main(), aggregating them.
//
// Aggregation semantics: run every property (do not short-circuit) so that a failure in one
// property still lets the others run and report; the process exits non-zero if any failed.

// Property entry points defined in their respective translation units.
bool runProperty1();  // property1_bytestream_preservation.cpp
bool runProperty2();  // property2_sequencer_equivalence.cpp
bool runPersistProperty1();  // property1_persist_roundtrip.cpp (persist-settings Property 1)
bool runPersistProperty2();  // property2_store_roundtrip.cpp (persist-settings Property 2)
bool runPersistProperty3();  // property3_crc_detection.cpp (persist-settings Property 3)
bool runPersistProperty4();  // property4_invalid_records.cpp (persist-settings Property 4)
bool runPersistProperty5();  // property5_capacity_guard.cpp (persist-settings Property 5)
bool runPersistProperty6();  // property6_load_readonly.cpp (persist-settings Property 6)

int main() {
    bool ok = true;
    ok &= runProperty1();
    ok &= runProperty2();
    ok &= runPersistProperty1();
    ok &= runPersistProperty2();
    ok &= runPersistProperty3();
    ok &= runPersistProperty4();
    ok &= runPersistProperty5();
    ok &= runPersistProperty6();
    return ok ? 0 : 1;
}
