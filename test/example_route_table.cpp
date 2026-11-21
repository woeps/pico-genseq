// Instruments task 2.2 -- RouteTable and removePatternBit (design section 1).
//
// Validates: the boot default equals the pre-Instrument behaviour; set() keeps the transposed
// routes[] consistent with patterns[]; removePatternBit equals a brute-force reference for every
// mask and index; RouteTable::removePattern applies the same update to every slot.
//
// Standalone example (own main(), no RapidCheck).

#include "common/InstrumentRouting.h"

#include <cstdint>
#include <cstdio>

using common::MAX_INSTRUMENTS;
using common::MAX_ROUTE_PATTERNS;
using common::RouteTable;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// Brute force: rebuild the mask bit by bit.
uint16_t referenceRemove(uint16_t mask, uint8_t index) {
    uint16_t out = 0;
    uint8_t dst = 0;
    for (uint8_t src = 0; src < 16; ++src) {
        if (src == index) continue;
        if ((mask >> src) & 1u) out |= static_cast<uint16_t>(1u << dst);
        ++dst;
    }
    return out;
}

bool consistent(const RouteTable& t) {
    for (uint8_t p = 0; p < MAX_ROUTE_PATTERNS; ++p) {
        for (uint8_t s = 0; s < MAX_INSTRUMENTS; ++s) {
            const bool a = (t.patterns[s] >> p) & 1u;
            const bool b = (t.routes[p] >> s) & 1u;
            if (a != b) return false;
        }
    }
    return true;
}

void testDefault() {
    RouteTable t;
    check(t.channel[0] == 0, "default slot 0 is protocol channel 0");
    check(t.patterns[0] == 0x0001, "default slot 0 plays pattern 0");
    check(t.routes[0] == 0x0001, "default pattern 0 routes to slot 0");
    for (uint8_t s = 1; s < MAX_INSTRUMENTS; ++s) check(t.patterns[s] == 0, "other slots empty");
    check(consistent(t), "default is consistent");
}

void testSet() {
    RouteTable t;
    t.set(3, 9, 0x0005);  // patterns 0 and 2 on channel 10
    check(t.channel[3] == 9, "channel stored");
    check(t.patterns[3] == 0x0005, "mask stored");
    check((t.routes[0] & (1u << 3)) && (t.routes[2] & (1u << 3)) && !(t.routes[1] & (1u << 3)),
          "transpose has slot 3 for patterns 0 and 2 only");
    check(consistent(t), "consistent after set");

    t.set(3, 9, 0xFFFF);  // bit 15 is not a valid pattern
    check(t.patterns[3] == common::ROUTE_PATTERN_MASK, "mask limited to valid patterns");

    RouteTable before = t;
    t.set(16, 0, 1);   // slot out of range
    t.set(0, 16, 1);   // channel out of range
    check(consistent(t) && t.patterns[0] == before.patterns[0] && t.channel[0] == before.channel[0],
          "out-of-range input ignored");
}

void testRemovePatternBit() {
    for (uint32_t mask = 0; mask <= 0xFFFFu; ++mask) {
        for (uint8_t index = 0; index < MAX_ROUTE_PATTERNS; ++index) {
            if (common::removePatternBit(static_cast<uint16_t>(mask), index) !=
                referenceRemove(static_cast<uint16_t>(mask), index)) {
                std::printf("mask=%04x index=%u\n", mask, index);
                check(false, "removePatternBit differs from reference");
                return;
            }
        }
    }
}

void testRemovePattern() {
    RouteTable t;
    t.set(0, 0, 0x0007);
    t.set(1, 1, 0x0006);
    t.set(2, 2, 0x0004);
    t.removePattern(1);
    check(t.patterns[0] == 0x0003, "slot 0: patterns 0,2 -> 0,1");
    check(t.patterns[1] == 0x0002, "slot 1: removed 1 dropped, old 2 becomes 1");
    check(t.patterns[2] == 0x0002, "slot 2: old pattern 2 -> 1");
    check(consistent(t), "consistent after removePattern");
}

}  // namespace

int main() {
    testDefault();
    testSet();
    testRemovePatternBit();
    testRemovePattern();
    if (failures == 0) std::printf("example_route_table: OK\n");
    return failures == 0 ? 0 : 1;
}
