// separate-velocity-set-gate-set-and-note-set tasks 2.2-2.4 -- set library model (host).
//
// Covers the pure library helpers (create/copy/delete/assign/usersOf/nextOccupied) and the
// pattern add/remove behaviour on the reference table: a new pattern references the lowest
// occupied slot of each library (R2.4) and removing a pattern drops only its references (R2.5).
// Compiles the real Reducer.cpp + SetLibrary.cpp with host-safe commands:: seam stubs. Own main().

#include "ui/state/Reducer.h"
#include "ui/state/SetLibrary.h"
#include "ui/state/UIState.h"
#include "commands/command.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace commands {
void sendCommand(Command, uint8_t, uint8_t) {}
void sendGateSet(uint8_t, const std::vector<bool>&) {}
void sendPitchSet(uint8_t, uint8_t, common::PlayingOrder, const std::vector<uint8_t>&) {}
void sendVelocitySet(uint8_t, uint8_t, common::PlayingOrder, const std::vector<uint8_t>&) {}
void sendGenParams(uint8_t, const common::GenerationParameters&) {}
}  // namespace commands

namespace {

using namespace ui::state;

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

constexpr SetType kTypes[] = {SetType::GATE, SetType::NOTE, SetType::VELOCITY};

void test_defaults() {
    UIState s;
    for (SetType t : kTypes) {
        check(occupiedMask(s, t) == 0x0001, "fresh library has only slot 0");
        check(lowestOccupied(s, t) == 0, "lowest occupied is 0");
    }
    check(s.patternRefs[0].gate == 0 && s.patternRefs[0].note == 0 && s.patternRefs[0].velocity == 0,
          "pattern 0 references slot 0");
}

void test_create_lowest_free_and_full() {
    for (SetType t : kTypes) {
        UIState s;
        uint8_t slot = 99;
        check(createSet(s, t, slot) && slot == 1, "first create takes slot 1");
        check(createSet(s, t, slot) && slot == 2, "second create takes slot 2");
        // Free slot 1 again (no users): next create reuses it.
        check(deleteSet(s, t, 1), "unused slot deletes");
        check(createSet(s, t, slot) && slot == 1, "create reuses lowest free slot");
        while (createSet(s, t, slot)) {}
        check(occupiedMask(s, t) == 0xFFFF, "library fills to 16 slots");
        const uint16_t before = occupiedMask(s, t);
        check(!createSet(s, t, slot), "create refused when full");
        check(occupiedMask(s, t) == before, "refused create leaves state unchanged");
    }
}

void test_create_resets_contents_and_isolates_libraries() {
    UIState s;
    s.gateLib.slots[3].steps = 7;  // stale content in a free slot
    uint8_t slot = 0;
    check(createSet(s, SetType::GATE, slot) && slot == 1, "gate create");
    check(occupiedMask(s, SetType::NOTE) == 0x0001 && occupiedMask(s, SetType::VELOCITY) == 0x0001,
          "creating a gate set does not touch the other libraries");
    check(s.gateLib.slots[1] == GateSetConfig{}, "created set has defaults");
    while (createSet(s, SetType::GATE, slot)) {}
    check(s.gateLib.slots[3] == GateSetConfig{}, "stale content in a free slot is reset on create");
}

void test_copy() {
    UIState s;
    s.noteLib.slots[0].pitches[0] = 55;
    uint8_t slot = 0;
    check(copySet(s, SetType::NOTE, 0, slot) && slot == 1, "copy lands in lowest free slot");
    check(s.noteLib.slots[1] == s.noteLib.slots[0], "copy equals source");
    s.noteLib.slots[1].pitches[0] = 56;
    check(s.noteLib.slots[0].pitches[0] == 55, "copy is independent of source");
    check(s.patternRefs[0].note == 0, "copy does not change references");
    check(!copySet(s, SetType::NOTE, 5, slot), "copy of a free slot refused");
    check(!copySet(s, SetType::NOTE, 16, slot), "copy of an out-of-range slot refused");
    while (createSet(s, SetType::NOTE, slot)) {}
    check(!copySet(s, SetType::NOTE, 0, slot), "copy refused when full");
}

void test_delete_rules() {
    UIState s;
    uint8_t slot = 0;
    check(!deleteSet(s, SetType::VELOCITY, 0), "last set cannot be deleted");
    check(createSet(s, SetType::VELOCITY, slot), "create second velocity set");
    check(!deleteSet(s, SetType::VELOCITY, 0), "set in use by pattern 0 cannot be deleted");
    check(occupiedMask(s, SetType::VELOCITY) == 0x0003, "refused delete leaves state unchanged");
    check(deleteSet(s, SetType::VELOCITY, 1), "unused set deletes");
    check(occupiedMask(s, SetType::VELOCITY) == 0x0001, "deleted slot is free, others not renumbered");
    check(!deleteSet(s, SetType::VELOCITY, 1), "deleting a free slot refused");
    check(!deleteSet(s, SetType::VELOCITY, 16), "deleting out of range refused");
}

void test_assign_ref() {
    UIState s;
    s.patternCount = 2;
    uint8_t slot = 0;
    createSet(s, SetType::GATE, slot);
    check(assignRef(s, 1, SetType::GATE, 1), "assign to occupied slot");
    check(s.patternRefs[1].gate == 1 && s.patternRefs[0].gate == 0, "only the target pattern changes");
    check(s.patternRefs[1].note == 0 && s.patternRefs[1].velocity == 0, "other types unchanged");
    check(!assignRef(s, 1, SetType::GATE, 5), "assign to free slot refused");
    check(s.patternRefs[1].gate == 1, "refused assign keeps previous reference");
    check(!assignRef(s, 1, SetType::GATE, 16), "assign out of range refused");
    check(!assignRef(s, 2, SetType::GATE, 0), "assign to a nonexistent pattern refused");
}

void test_users_of_and_next_occupied() {
    UIState s;
    s.patternCount = 3;
    uint8_t a = 0, b = 0;
    createSet(s, SetType::NOTE, a);  // slot 1
    createSet(s, SetType::NOTE, b);  // slot 2
    assignRef(s, 1, SetType::NOTE, 2);
    assignRef(s, 2, SetType::NOTE, 2);
    check(usersOf(s, SetType::NOTE, 0) == 0b001, "slot 0 used by pattern 0");
    check(usersOf(s, SetType::NOTE, 1) == 0, "slot 1 unused");
    check(usersOf(s, SetType::NOTE, 2) == 0b110, "slot 2 shared by patterns 1 and 2");
    check(popcount16(usersOf(s, SetType::NOTE, 2)) == 2, "user count 2");
    check(usersOf(s, SetType::GATE, 0) == 0b111, "gate slot 0 used by all three patterns");

    check(nextOccupied(s, SetType::NOTE, 0, +1) == 1, "next from 0 is 1");
    check(nextOccupied(s, SetType::NOTE, 2, +1) == 0, "next wraps from the last occupied to 0");
    check(nextOccupied(s, SetType::NOTE, 0, -1) == 2, "previous wraps from 0 to the highest occupied");
    deleteSet(s, SetType::NOTE, 1);
    check(nextOccupied(s, SetType::NOTE, 0, +1) == 2, "next skips free slots");
    UIState single;
    check(nextOccupied(single, SetType::GATE, 0, +1) == 0, "single set: stays on itself");
}

void test_add_pattern_uses_lowest_occupied() {
    UIState s;
    uint8_t slot = 0;
    createSet(s, SetType::GATE, slot);
    s.gateLib.occupied = 0x0002;  // only slot 1 occupied
    s.patternRefs[0].gate = 1;
    addPattern(s);
    check(s.patternCount == 2 && s.selectedPattern == 1, "pattern added and selected");
    check(s.patternRefs[1].gate == 1, "new pattern references lowest occupied gate slot");
    check(s.patternRefs[1].note == 0 && s.patternRefs[1].velocity == 0, "other types reference slot 0");
    check(occupiedMask(s, SetType::GATE) == 0x0002, "adding a pattern creates no sets");
}

void test_remove_pattern_drops_only_refs() {
    UIState s;
    s.patternCount = 3;
    uint8_t slot = 0;
    createSet(s, SetType::GATE, slot);
    createSet(s, SetType::GATE, slot);
    assignRef(s, 0, SetType::GATE, 0);
    assignRef(s, 1, SetType::GATE, 1);
    assignRef(s, 2, SetType::GATE, 2);
    s.gateLib.slots[1].steps = 12;
    const uint16_t occBefore = occupiedMask(s, SetType::GATE);

    removePattern(s, 1);

    check(s.patternCount == 2, "pattern removed");
    check(s.patternRefs[0].gate == 0 && s.patternRefs[1].gate == 2, "references compact");
    check(occupiedMask(s, SetType::GATE) == occBefore, "libraries keep every set");
    check(s.gateLib.slots[1].steps == 12, "orphaned set contents are preserved");
    check(usersOf(s, SetType::GATE, 1) == 0, "orphaned set now has no users");
}

void test_two_patterns_share_after_add() {
    UIState s;
    addPattern(s);
    check(usersOf(s, SetType::NOTE, 0) == 0b11, "new pattern shares slot 0 with pattern 0");
    s.noteLib.slots[0].pitches[0] = 50;
    check(s.pitchSetOf(0).pitches[0] == 50 && s.pitchSetOf(1).pitches[0] == 50,
          "accessors resolve through references to the same set");
}

}  // namespace

int main() {
    test_defaults();
    test_create_lowest_free_and_full();
    test_create_resets_contents_and_isolates_libraries();
    test_copy();
    test_delete_rules();
    test_assign_ref();
    test_users_of_and_next_occupied();
    test_add_pattern_uses_lowest_occupied();
    test_remove_pattern_drops_only_refs();
    test_two_patterns_share_after_add();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("set library OK\n");
    return 0;
}
