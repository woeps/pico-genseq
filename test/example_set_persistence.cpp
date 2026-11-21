// separate-velocity-set-gate-set-and-note-set task 7.5 -- format v3 persistence (host).
//
// Covers: shared references and orphaned sets surviving a save/load through the real
// UIState mapping (R7.1, R7.2); v1/v2 records migrating to one set per pattern in the same slot
// index, loading into a UIState that plays like the old firmware (R7.3, R7.4); rejection of
// records with a free or out-of-range reference, an empty library, or a truncated payload, with
// `out` left untouched (R7.5); and the worst-case record size staying inside the store (R7.6).
// Own main(), excluded from the glob.

#include "PersistTestSupport.h"
#include "persistence/PersistableConfig.h"
#include "persistence/SaveRecord.h"
#include "ui/state/PersistMapping.h"
#include "ui/state/UIState.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using namespace persistence;
using ui::state::UIState;

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// Rebuild a record around a modified payload (fresh length and CRC), so structural checks that
// run AFTER the CRC check can be exercised.
std::vector<uint8_t> reframe(const std::vector<uint8_t>& rec, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> out(rec.begin(), rec.begin() + HEADER_SIZE);
    const uint32_t len = static_cast<uint32_t>(payload.size());
    const uint32_t crc = crc32(payload.data(), payload.size());
    for (int i = 0; i < 4; ++i) out[4 + i] = static_cast<uint8_t>((len >> (8 * i)) & 0xFF);
    for (int i = 0; i < 4; ++i) out[8 + i] = static_cast<uint8_t>((crc >> (8 * i)) & 0xFF);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

// Three patterns; 0 and 1 share every slot-0 set, pattern 2 owns slot 1; an unused extra note
// set sits in slot 2.
UIState sharedState() {
    UIState s;
    s.patternCount = 3;
    s.activePatterns = 0b101;
    s.bpm = 133;
    s.gateLib.occupied = 0b011;
    s.noteLib.occupied = 0b111;
    s.velocityLib.occupied = 0b011;
    s.patternRefs[0] = {0, 0, 0};
    s.patternRefs[1] = {0, 0, 0};
    s.patternRefs[2] = {1, 1, 1};
    s.noteLib.slots[0].pitches[0] = 50;
    s.noteLib.slots[0].scale = common::ScaleId::MAJOR;
    s.noteLib.slots[1].pitches[0] = 51;
    s.noteLib.slots[2].pitches[0] = 52;  // orphan
    s.gateLib.slots[1].steps = 12;
    s.velocityLib.slots[1].humanizeRange = 7;
    return s;
}

void test_shared_refs_and_orphans_survive() {
    UIState s = sharedState();
    PersistableConfig cfg;
    ui::state::toPersistableConfig(s, cfg);
    const std::vector<uint8_t> rec = serialize(cfg);
    check(rec[2] == FORMAT_VERSION, "record is written as the current version");

    PersistableConfig out{};
    check(deserialize(rec.data(), rec.size(), out) == LoadStatus::OK, "v3 record loads");
    check(out == cfg, "v3 round-trips");

    UIState loaded;
    ui::state::applyPersistableConfig(loaded, out);
    check(loaded.patternCount == 3 && loaded.bpm == 133, "pattern count and bpm restored");
    check(loaded.gateLib.occupied == 0b011 && loaded.noteLib.occupied == 0b111 &&
              loaded.velocityLib.occupied == 0b011,
          "slot occupancy restored");
    check(loaded.patternRefs[0].note == 0 && loaded.patternRefs[1].note == 0 &&
              loaded.patternRefs[2].note == 1,
          "references restored");
    check(&loaded.pitchSetOf(0) == &loaded.pitchSetOf(1), "patterns 0 and 1 still share one note set");
    check(loaded.pitchSetOf(0).pitches[0] == 50 && loaded.pitchSetOf(0).scale == common::ScaleId::MAJOR,
          "shared note set contents restored");
    check(loaded.noteLib.slots[2].pitches[0] == 52, "orphaned set survives the save");
    check(loaded.gateSetOf(2).steps == 12 && loaded.velocitySetOf(2).humanizeRange == 7,
          "per-set generation parameters restored");
    check(loaded.activePatterns == 0b101, "active mask restored");
}

void test_v2_migrates_to_one_set_per_pattern() {
    persist_test::LegacyConfig legacy;
    legacy.patternCount = 3;
    for (uint8_t i = 0; i < 3; ++i) {
        LegacyPatternConfig& p = legacy.patterns[i];
        p.gateSteps = 16; p.gatePulses = static_cast<uint8_t>(2 + i); p.gateRotation = 0;
        p.gateNoteLength = 1; p.gateLength = 50;
        p.pitchCount = 2; p.pitches[0] = static_cast<uint8_t>(60 + i); p.pitches[1] = 67;
        p.velocityCount = 1; p.velocities[0] = static_cast<uint8_t>(90 + i);
        p.midiChannel = 1; p.active = (i != 1);
        p.genScale = 1; p.genScaleRoot = static_cast<uint8_t>(i); p.genPatternProbability = 80;
        p.genHumanizeRange = static_cast<uint8_t>(3 + i);
        p.genStepProbCount = 2; p.genStepProbability[0] = 0xFF; p.genStepProbability[1] = 40;
        p.gateStepCount = 4; p.gateStates[0] = 1; p.gateStates[2] = 1;
    }
    const std::vector<uint8_t> rec = persist_test::serializeLegacy(legacy, FORMAT_VERSION_V2_LEGACY);

    PersistableConfig out{};
    check(deserialize(rec.data(), rec.size(), out) == LoadStatus::OK, "v2 record loads");
    check(out == persist_test::expectedMigration(legacy), "v2 migrates to per-pattern sets");

    UIState s;
    ui::state::applyPersistableConfig(s, out);
    for (uint8_t i = 0; i < 3; ++i) {
        check(s.patternRefs[i].gate == i && s.patternRefs[i].note == i && s.patternRefs[i].velocity == i,
              "pattern i references set i");
        check(s.pitchSetOf(i).pitches[0] == 60 + i, "note set holds the pattern's pitches");
        check(s.pitchSetOf(i).scale == common::ScaleId::MAJOR && s.pitchSetOf(i).scaleRoot == i,
              "scale moved to the note set");
        check(s.gateSetOf(i).patternProbability == 80 && s.gateSetOf(i).stepProbability[1] == 40,
              "probability moved to the gate set");
        check(s.velocitySetOf(i).humanizeRange == 3 + i, "humanize moved to the velocity set");
    }
    check(s.gateLib.occupied == 0b111 && s.noteLib.occupied == 0b111, "slots 0..2 occupied");
    check((s.activePatterns & 0b111) == 0b101, "active flags preserved");
}

void test_v1_loads_and_empty_legacy_record_keeps_a_default_set() {
    persist_test::LegacyConfig legacy;
    legacy.patternCount = 0;
    const std::vector<uint8_t> rec = persist_test::serializeLegacy(legacy, FORMAT_VERSION_V1_LEGACY);
    PersistableConfig out{};
    check(deserialize(rec.data(), rec.size(), out) == LoadStatus::OK, "empty v1 record loads");
    check(out.gateOccupied == 1 && out.noteOccupied == 1 && out.velocityOccupied == 1,
          "an empty legacy record keeps the default set 0 in every library");
    UIState s;
    ui::state::applyPersistableConfig(s, out);
    check(s.patternCount == 1 && s.patternRefs[0].gate == 0, "UI keeps one pattern on set 0");
}

void test_invalid_references_are_rejected_and_out_untouched() {
    const PersistableConfig sentinel = persist_test::makeSentinel();

    auto expectReject = [&](PersistableConfig bad, LoadStatus want, const char* what) {
        const std::vector<uint8_t> rec = serialize(bad);
        PersistableConfig out = sentinel;
        check(deserialize(rec.data(), rec.size(), out) == want, what);
        check(out == sentinel, "rejected record leaves out untouched");
    };

    PersistableConfig base = persist_test::makeSentinel();

    PersistableConfig freeSlot = base;  // occupied slots 0,1 -> slot 5 is free
    freeSlot.patterns[2].noteRef = 5;
    expectReject(freeSlot, LoadStatus::INVALID_REFERENCE, "reference to a free slot rejected");

    PersistableConfig outOfRange = base;
    outOfRange.patterns[0].gateRef = 16;
    expectReject(outOfRange, LoadStatus::INVALID_REFERENCE, "reference >= library size rejected");

    PersistableConfig wild = base;
    wild.patterns[6].velocityRef = 255;
    expectReject(wild, LoadStatus::INVALID_REFERENCE, "reference 255 rejected");

    PersistableConfig emptyLib = base;
    emptyLib.noteOccupied = 0;
    expectReject(emptyLib, LoadStatus::INVALID_REFERENCE, "empty library rejected");
}

void test_truncated_v3_payload_is_rejected() {
    const PersistableConfig sentinel = persist_test::makeSentinel();
    const std::vector<uint8_t> rec = serialize(sentinel);
    std::vector<uint8_t> payload(rec.begin() + HEADER_SIZE, rec.end());
    for (std::size_t keep : {std::size_t(3), std::size_t(7), payload.size() / 2, payload.size() - 1}) {
        const std::vector<uint8_t> cut =
            reframe(rec, std::vector<uint8_t>(payload.begin(), payload.begin() + keep));
        PersistableConfig out = persist_test::makeSentinel();
        out.bpm = 1;
        const PersistableConfig before = out;
        check(deserialize(cut.data(), cut.size(), out) == LoadStatus::TRUNCATED,
              "payload cut short is TRUNCATED, not an invalid reference");
        check(out == before, "truncated record leaves out untouched");
    }
}

void test_worst_case_record_fits() {
    PersistableConfig cfg{};
    cfg.patternCount = MAX_PATTERNS;
    cfg.gateOccupied = cfg.noteOccupied = cfg.velocityOccupied = 0xFFFF;
    for (uint8_t s = 0; s < SET_LIBRARY_SIZE; ++s) {
        cfg.gateSets[s].steps = 64;
        cfg.gateSets[s].stepProbCount = MAX_SET_LEN;
        cfg.gateSets[s].gateStepCount = GateSetRecord::kMaxGateEditSteps;
        cfg.noteSets[s].count = MAX_SET_LEN;
        cfg.velocitySets[s].count = MAX_SET_LEN;
    }
    for (uint8_t p = 0; p < MAX_PATTERNS; ++p) {
        cfg.patterns[p].gateRef = p;
        cfg.patterns[p].noteRef = static_cast<uint8_t>(15 - p);
        cfg.patterns[p].velocityRef = p;
    }
    const std::vector<uint8_t> rec = serialize(cfg);
    std::printf("worst-case v3 record: %u bytes\n", static_cast<unsigned>(rec.size()));
    // RecordStore reads a slot into a 4096 - slot-header static buffer (8 KB region split in two).
    check(rec.size() < 4000, "worst-case record fits the RecordStore slot buffer");
    PersistableConfig out{};
    check(deserialize(rec.data(), rec.size(), out) == LoadStatus::OK && out == cfg,
          "worst-case record round-trips");
}

void test_ui_state_round_trip_is_stable() {
    UIState s = sharedState();
    PersistableConfig a;
    ui::state::toPersistableConfig(s, a);
    UIState back;
    ui::state::applyPersistableConfig(back, a);
    PersistableConfig b;
    ui::state::toPersistableConfig(back, b);
    check(a == b, "toPersistableConfig(applyPersistableConfig(cfg)) == cfg");
}

}  // namespace

int main() {
    test_shared_refs_and_orphans_survive();
    test_v2_migrates_to_one_set_per_pattern();
    test_v1_loads_and_empty_legacy_record_keeps_a_default_set();
    test_invalid_references_are_rejected_and_out_untouched();
    test_truncated_v3_payload_is_rejected();
    test_worst_case_record_fits();
    test_ui_state_round_trip_is_stable();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("set persistence OK\n");
    return 0;
}
