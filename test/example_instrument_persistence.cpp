// Instruments tasks 9.5 -- format v4 Instrument block, legacy migration and validation.
//
//   - a v4 record round-trips Instruments (channel, mute, members, enabled);
//   - v1, v2 and v3 records load as one Instrument per distinct channel, ascending, every route
//     on, nothing muted, and a v3 record's channels survive;
//   - each inconsistent v4 record (duplicate channel, channel out of range, mask naming a missing
//     pattern, route flag outside the Pattern Selection, no Instrument) is rejected and leaves the
//     output untouched;
//   - the worst-case v4 record (15 patterns, 16 full libraries, 16 Instruments) fits half of the
//     8 KB FRAM region (the record store keeps two slots), with the margin printed;
//   - the UIState mapping round-trips Instruments, including mute and a shared pattern.
// Own main(), excluded from the glob.

#include "PersistTestSupport.h"
#include "persistence/PersistableConfig.h"
#include "persistence/SaveRecord.h"
#include "ui/state/PersistMapping.h"
#include "ui/state/UIState.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace persistence;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// A v4 config with patternCount patterns and a few Instruments.
PersistableConfig sample() {
    PersistableConfig c = persist_test::makeSentinel();  // 7 patterns, valid libraries
    c.instrumentOccupied = 0b101;                         // slots 0 and 2
    c.instruments[0] = InstrumentRecord{3, 0, 0b0000111, 0b0000101};
    c.instruments[2] = InstrumentRecord{9, 1, 0b1000100, 0b1000100};
    return c;
}

std::vector<uint8_t> encode(const PersistableConfig& c) { return serialize(c); }

LoadStatus decode(const std::vector<uint8_t>& rec, PersistableConfig& out) {
    return deserialize(rec.data(), rec.size(), out);
}

void test_v4_round_trip() {
    const PersistableConfig c = sample();
    PersistableConfig out{};
    check(decode(encode(c), out) == LoadStatus::OK, "v4 record loads");
    check(out == c, "Instruments round-trip");
    check(out.instruments[2].muted == 1 && out.instruments[2].channel == 9, "mute and channel kept");
    check(encode(c)[2] == FORMAT_VERSION && FORMAT_VERSION == 4, "records are written as v4");
}

void test_v3_record_derives_instruments_from_channels() {
    PersistableConfig c = persist_test::makeSentinel();  // 7 patterns
    const uint8_t channels[7] = {5, 1, 5, 16, 1, 9, 5};
    const std::vector<uint8_t> rec = persist_test::serializeV3(c, channels);
    PersistableConfig out{};
    check(decode(rec, out) == LoadStatus::OK, "v3 record loads");
    // Distinct channels ascending: 1 -> {1,4}, 5 -> {0,2,6}, 9 -> {5}, 16 -> {3}.
    check(out.instrumentOccupied == 0b1111, "four Instruments");
    check(out.instruments[0].channel == 1 && out.instruments[0].members == 0b0010010, "channel 1 patterns");
    check(out.instruments[1].channel == 5 && out.instruments[1].members == 0b1000101, "channel 5 patterns");
    check(out.instruments[2].channel == 9 && out.instruments[2].members == 0b0100000, "channel 9 patterns");
    check(out.instruments[3].channel == 16 && out.instruments[3].members == 0b0001000, "channel 16 patterns");
    for (uint8_t s = 0; s < 4; ++s) {
        check(out.instruments[s].enabled == out.instruments[s].members, "every route on");
        check(out.instruments[s].muted == 0, "nothing muted");
    }
}

void test_legacy_channels_outside_range_are_clamped_like_the_old_emission_path() {
    PersistableConfig c = persist_test::makeSentinel();
    const uint8_t channels[7] = {0, 17, 255, 1, 16, 2, 2};  // 0 -> 1, 17 and 255 -> 16
    PersistableConfig out{};
    check(decode(persist_test::serializeV3(c, channels), out) == LoadStatus::OK, "loads");
    check(out.instruments[0].channel == 1 && out.instruments[0].members == 0b0001001, "0 and 1 -> channel 1");
    check(out.instruments[1].channel == 2 && out.instruments[1].members == 0b1100000, "channel 2");
    check(out.instruments[2].channel == 16 && out.instruments[2].members == 0b0010110, "16, 17, 255 -> 16");
}

void test_empty_legacy_record_gets_the_default_instrument() {
    PersistableConfig c = persist_test::makeSentinel();
    c.patternCount = 0;
    const uint8_t none[1] = {0};
    PersistableConfig out{};
    check(decode(persist_test::serializeV3(c, none), out) == LoadStatus::OK, "loads");
    check(out.instrumentOccupied == 0x0001 && out.instruments[0].channel == 1 &&
              out.instruments[0].members == 0,
          "default Instrument, no patterns");
}

void test_v1_and_v2_records_migrate_with_instruments() {
    persist_test::LegacyConfig legacy;
    legacy.patternCount = 3;
    for (uint8_t i = 0; i < 3; ++i) {
        LegacyPatternConfig& p = legacy.patterns[i];
        p.gateSteps = 16; p.gatePulses = 4; p.gateNoteLength = 1; p.gateLength = 50;
        p.pitchCount = 1; p.pitches[0] = 60; p.velocityCount = 1; p.velocities[0] = 100;
        p.active = 1;
        p.midiChannel = static_cast<uint8_t>(i == 1 ? 7 : 2);
    }
    for (uint8_t v : {FORMAT_VERSION_V1_LEGACY, FORMAT_VERSION_V2_LEGACY}) {
        PersistableConfig out{};
        const std::vector<uint8_t> rec = persist_test::serializeLegacy(legacy, v);
        check(decode(rec, out) == LoadStatus::OK, "legacy record loads");
        check(out.instrumentOccupied == 0b11, "two Instruments");
        check(out.instruments[0].channel == 2 && out.instruments[0].members == 0b101, "channel 2: patterns 0,2");
        check(out.instruments[1].channel == 7 && out.instruments[1].members == 0b010, "channel 7: pattern 1");
        check(out == persist_test::expectedMigration(legacy), "equals the independent expectation");
    }
}

// Corrupt v4 variants: each must be rejected with INVALID_REFERENCE and leave `out` untouched.
void expectRejected(PersistableConfig bad, const char* what) {
    const PersistableConfig sentinel = persist_test::makeSentinel();
    PersistableConfig out = sentinel;
    const LoadStatus st = decode(encode(bad), out);
    check(st == LoadStatus::INVALID_REFERENCE, what);
    check(out == sentinel, "rejected record leaves the output untouched");
}

void test_corrupt_v4_records_are_rejected() {
    { PersistableConfig c = sample(); c.instruments[2].channel = c.instruments[0].channel;
      expectRejected(c, "duplicate channel"); }
    { PersistableConfig c = sample(); c.instruments[0].channel = 0;
      expectRejected(c, "channel 0"); }
    { PersistableConfig c = sample(); c.instruments[0].channel = 17;
      expectRejected(c, "channel 17"); }
    { PersistableConfig c = sample(); c.instruments[0].members |= 1u << 7;  // only 7 patterns
      expectRejected(c, "members names a missing pattern"); }
    { PersistableConfig c = sample(); c.instruments[0].enabled |= 1u << 3;  // 3 is not a member
      expectRejected(c, "route enabled outside the Pattern Selection"); }
    { PersistableConfig c = sample(); c.instrumentOccupied = 0;
      expectRejected(c, "no Instrument"); }
    { PersistableConfig c = sample(); c.instruments[0].muted = 2;
      expectRejected(c, "mute flag out of range"); }
}

void test_worst_case_record_fits_half_of_the_fram_region() {
    PersistableConfig c{};
    c.patternCount = MAX_PATTERNS;
    c.bpm = 255;
    c.gateOccupied = c.noteOccupied = c.velocityOccupied = 0xFFFF;
    for (uint8_t s = 0; s < SET_LIBRARY_SIZE; ++s) {
        c.gateSets[s].stepProbCount = MAX_SET_LEN;
        c.gateSets[s].gateStepCount = GateSetRecord::kMaxGateEditSteps;
        c.noteSets[s].count = MAX_SET_LEN;
        c.velocitySets[s].count = MAX_SET_LEN;
    }
    c.instrumentOccupied = 0xFFFF;
    for (uint8_t s = 0; s < MAX_INSTRUMENTS; ++s) {
        c.instruments[s].channel = static_cast<uint8_t>(s + 1);
        c.instruments[s].members = c.instruments[s].enabled = 0x7FFF;
    }
    const std::vector<uint8_t> rec = encode(c);
    std::printf("worst-case v4 record: %zu bytes (slot budget 4096)\n", rec.size());
    check(rec.size() < 4096, "worst-case record fits one 4096-byte slot");
    PersistableConfig out{};
    check(decode(rec, out) == LoadStatus::OK && out == c, "worst-case record round-trips");
}

void test_ui_mapping_round_trips_instruments() {
    using namespace ui::state;
    UIState s;
    s.patternCount = 4;
    s.activePatterns = 0b1011;
    s.instruments[0] = InstrumentConfig{1, false, 0b0011, 0b0001};
    s.instruments[3] = InstrumentConfig{12, true, 0b1010, 0b1010};
    s.instrumentOccupied = 0b1001;
    s.selectedInstrument = 3;

    PersistableConfig cfg{};
    toPersistableConfig(s, cfg);
    PersistableConfig loaded{};
    check(decode(encode(cfg), loaded) == LoadStatus::OK, "mapped config encodes and loads");

    UIState t;
    applyPersistableConfig(t, loaded);
    check(t.instrumentOccupied == 0b1001, "occupancy restored");
    check(t.instruments[0].channel == 1 && t.instruments[0].members == 0b0011 &&
              t.instruments[0].enabled == 0b0001 && !t.instruments[0].muted,
          "Instrument 0 restored");
    check(t.instruments[3].channel == 12 && t.instruments[3].members == 0b1010 &&
              t.instruments[3].enabled == 0b1010 && t.instruments[3].muted,
          "Instrument 3 restored (muted, pattern 1 shared with Instrument 0)");
    check(t.selectedInstrument == 0, "selection resets to the lowest occupied slot");
}

}  // namespace

int main() {
    test_v4_round_trip();
    test_v3_record_derives_instruments_from_channels();
    test_legacy_channels_outside_range_are_clamped_like_the_old_emission_path();
    test_empty_legacy_record_gets_the_default_instrument();
    test_v1_and_v2_records_migrate_with_instruments();
    test_corrupt_v4_records_are_rejected();
    test_worst_case_record_fits_half_of_the_fram_region();
    test_ui_mapping_round_trips_instruments();
    if (failures == 0) std::printf("example_instrument_persistence: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
