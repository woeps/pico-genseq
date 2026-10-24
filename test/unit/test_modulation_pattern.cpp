// generative-engine-expansion task 15 — global modulation patterns and
// per-target configuration (FR11, FR13, FR14).
//
// Validates:
//   - ModulationPattern: step values are clamped to 0-127, the fixed step
//     capacity is enforced, and stepAt() wraps modulo the active length;
//   - ModulationTarget: the step index is DERIVED from the authoritative
//     transport position (TransportTimeline -> TransportPosition) for both
//     beat- and bar-step durations; every playing order (forward / reverse /
//     ping-pong / random) yields its expected index sequence, random being a
//     deterministic function of (seed, counter); two targets on the SAME
//     pattern with different playing orders and step durations advance
//     independently; the scaling/balance output formula clamps to the target
//     range; the default scope is per-pattern (FR13);
//   - ModulationRegistry: fixed capacity of the global pattern pool and the
//     target-slot pool is enforced; the global registry instance is
//     statically allocated (single instance).
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/ so
// the property-test glob (test/*.cpp) never sweeps it; registered in the host
// test build under the ctest name "test_modulation_pattern".

#include "ModulationPattern.h"
#include "ModulationRegistry.h"
#include "ModulationTarget.h"
#include "TransportTimeline.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

using sequencer::ModulationOrder;
using sequencer::ModulationPattern;
using sequencer::ModulationRegistry;
using sequencer::ModulationScope;
using sequencer::ModulationTarget;
using sequencer::StepDuration;
using sequencer::StepUnit;
using sequencer::TransportPosition;
using sequencer::TransportTimeline;
using sequencer::g_modulationRegistry;
using sequencer::transportPositionOf;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

TransportPosition pos(uint64_t beatIndex, uint64_t barIndex) {
    TransportPosition p;
    p.beatIndex = beatIndex;
    p.barIndex = barIndex;
    return p;
}

// --- ModulationPattern ------------------------------------------------------

// FR11: steps hold values 0-127; setting a step CLAMPS to that range and the
// fixed step capacity rejects out-of-range indices without side effects.
void test_step_values_clamped() {
    ModulationPattern p;
    check(p.setStep(0, 200), "setStep accepts an in-range index");
    check(p.stepAt(0) == 127, "step value clamps up to 127");
    check(p.setStep(1, 64), "setStep accepts an in-range index");
    check(p.stepAt(1) == 64, "in-range step value is kept as-is");
    check(!p.setStep(ModulationPattern::MAX_STEPS, 10),
          "setStep beyond the fixed capacity is rejected");
    check(p.stepAt(0) == 127 && p.stepAt(1) == 64,
          "a rejected setStep leaves the pattern unchanged");
}

// The active length constrains traversal: stepAt() wraps modulo the length and
// invalid lengths are rejected.
void test_length_and_wrap() {
    ModulationPattern p;
    for (size_t i = 0; i < ModulationPattern::MAX_STEPS; ++i) {
        check(p.setStep(i, static_cast<uint8_t>((i + 1) * 10)), "fill steps");
    }
    check(p.setLength(4), "setLength accepts 1..MAX_STEPS");
    check(p.length() == 4, "length round-trips");
    check(p.stepAt(0) == 10 && p.stepAt(3) == 40, "steps read back in order");
    check(p.stepAt(4) == 10, "stepAt wraps modulo the active length");
    check(p.stepAt(9) == 20, "stepAt wraps for any monotonic counter");
    check(p.stepAt(10) == 30, "stepAt wrap lands on the reduced index");
    check(!p.setLength(0), "length 0 is rejected");
    check(!p.setLength(ModulationPattern::MAX_STEPS + 1), "length beyond capacity is rejected");
    check(p.length() == 4, "a rejected setLength leaves the length unchanged");
    p.clear();
    check(p.length() == ModulationPattern::MAX_STEPS, "clear restores full length");
    check(p.stepAt(5) == 0, "clear zeroes every step");
}

// --- ModulationTarget: step index from the transport ------------------------

// FR11: one step per beat (beat unit, length 1) advances the pattern index
// every beat; longer beat lengths hold each step for N beats.
void test_step_index_beat_duration() {
    ModulationTarget t;  // factory: beat unit, length 1, forward
    check(t.stepDuration.unit == StepUnit::Beat, "factory step unit is Beat");
    check(t.stepDuration.length == 1, "factory step length is 1");
    const uint32_t expected[] = {0, 1, 2, 3, 0, 1};
    for (uint64_t beat = 0; beat < 6; ++beat) {
        check(t.stepIndexForTransport(pos(beat, beat / 4), 4) == expected[beat],
              "one step per beat advances and wraps forward");
    }
    t.stepDuration.length = 2;  // one step per 2 beats
    check(t.stepIndexForTransport(pos(0, 0), 4) == 0, "beat 0 at 2-beat duration -> step 0");
    check(t.stepIndexForTransport(pos(1, 0), 4) == 0, "beat 1 still holds step 0");
    check(t.stepIndexForTransport(pos(2, 0), 4) == 1, "beat 2 advances to step 1");
    check(t.stepIndexForTransport(pos(8, 2), 4) == 0, "beat 8 wraps forward at 2-beat duration");
}

// FR11: bar-unit durations derive the counter from the BAR index instead.
void test_step_index_bar_duration() {
    ModulationTarget t;
    t.stepDuration.unit = StepUnit::Bar;
    const uint32_t expected[] = {0, 1, 2, 3, 0, 1};
    for (uint64_t bar = 0; bar < 6; ++bar) {
        check(t.stepIndexForTransport(pos(bar * 4, bar), 4) == expected[bar],
              "one step per bar advances and wraps forward");
    }
    t.stepDuration.length = 2;  // one step per 2 bars
    check(t.stepIndexForTransport(pos(0, 0), 4) == 0, "bar 0 at 2-bar duration -> step 0");
    check(t.stepIndexForTransport(pos(4, 1), 4) == 0, "bar 1 still holds step 0");
    check(t.stepIndexForTransport(pos(8, 2), 4) == 1, "bar 2 advances to step 1");
    t.stepDuration.length = 0;  // guarded: a zero length behaves as 1
    check(t.stepIndexForTransport(pos(4, 1), 4) == 1,
          "a zero step length is guarded to one unit");
}

// FR11/FR13/FR14: the step position is DERIVED from the authoritative
// TransportTimeline — the same beat/bar indices the one transport reports —
// never from a separate modulation clock.
void test_step_index_derives_from_authoritative_transport() {
    TransportTimeline timeline;  // factory: 4/4, ticksPerBeat 24
    timeline.start(0);
    ModulationTarget beatTarget;  // one step per beat, forward
    ModulationTarget barTarget;
    barTarget.stepDuration.unit = StepUnit::Bar;

    TransportPosition p = transportPositionOf(timeline);  // tick 0
    check(p.beatIndex == 0 && p.barIndex == 0, "transport snapshot starts at beat/bar 0");
    check(beatTarget.stepIndexForTransport(p, 4) == 0, "beat target starts at step 0");
    check(barTarget.stepIndexForTransport(p, 4) == 0, "bar target starts at step 0");

    for (uint32_t i = 0; i < timeline.ticksPerBeat(); ++i) timeline.advance();  // beat 1
    p = transportPositionOf(timeline);
    check(p.beatIndex == 1 && p.barIndex == 0, "24 ticks advance one beat");
    check(beatTarget.stepIndexForTransport(p, 4) == 1, "beat target follows the transport beat");
    check(barTarget.stepIndexForTransport(p, 4) == 0, "bar target holds within the bar");

    for (uint32_t i = 0; i < 3 * timeline.ticksPerBeat(); ++i) timeline.advance();  // bar 1
    p = transportPositionOf(timeline);
    check(p.beatIndex == 4 && p.barIndex == 1, "96 ticks advance one bar");
    check(beatTarget.stepIndexForTransport(p, 4) == 0, "beat target wraps at the bar");
    check(barTarget.stepIndexForTransport(p, 4) == 1, "bar target follows the transport bar");
}

// FR11: each playing order maps a monotonic step counter onto the expected
// index sequence.
void test_playing_orders() {
    ModulationTarget t;
    t.stepDuration = StepDuration{StepUnit::Beat, 1};

    t.order = ModulationOrder::Forward;
    const uint32_t fwd[] = {0, 1, 2, 3, 0, 1, 2, 3};
    for (uint64_t beat = 0; beat < 8; ++beat) {
        check(t.stepIndexForTransport(pos(beat, 0), 4) == fwd[beat],
              "forward order walks 0,1,2,3,0,...");
    }

    t.order = ModulationOrder::Reverse;
    const uint32_t rev[] = {3, 2, 1, 0, 3, 2, 1, 0};
    for (uint64_t beat = 0; beat < 8; ++beat) {
        check(t.stepIndexForTransport(pos(beat, 0), 4) == rev[beat],
              "reverse order walks 3,2,1,0,3,...");
    }

    t.order = ModulationOrder::PingPong;
    const uint32_t pong[] = {0, 1, 2, 3, 2, 1, 0, 1};
    for (uint64_t beat = 0; beat < 8; ++beat) {
        check(t.stepIndexForTransport(pos(beat, 0), 4) == pong[beat],
              "ping-pong order walks 0,1,2,3,2,1,0,1");
    }

    // A single-step pattern is order-independent.
    const ModulationOrder orders[] = {ModulationOrder::Forward, ModulationOrder::Reverse,
                                      ModulationOrder::PingPong, ModulationOrder::Random};
    for (ModulationOrder o : orders) {
        t.order = o;
        for (uint64_t beat = 0; beat < 4; ++beat) {
            check(t.stepIndexForTransport(pos(beat, 0), 1) == 0,
                  "a 1-step pattern stays at index 0 under every order");
        }
    }

    // Random is a DETERMINISTIC function of (seed, counter): repeated walks
    // with the same seed reproduce the exact sequence; a different seed
    // yields a different sequence; every index stays inside the pattern.
    t.order = ModulationOrder::Random;
    t.randomSeed = 0x1234;
    uint32_t first[16];
    for (uint64_t beat = 0; beat < 16; ++beat) {
        first[beat] = t.stepIndexForTransport(pos(beat, 0), 4);
        check(first[beat] < 4, "random order stays inside the pattern");
    }
    for (uint64_t beat = 0; beat < 16; ++beat) {
        check(t.stepIndexForTransport(pos(beat, 0), 4) == first[beat],
              "random order is deterministic for a fixed seed and position");
    }
    t.randomSeed = 0x4321;
    bool differs = false;
    for (uint64_t beat = 0; beat < 16; ++beat) {
        if (t.stepIndexForTransport(pos(beat, 0), 4) != first[beat]) differs = true;
    }
    check(differs, "a different seed yields a different random sequence");

    // Zero steps cannot invent an index.
    check(t.stepIndexForTransport(pos(7, 1), 0) == 0, "a 0-step pattern yields index 0");
}

// FR14: two targets bound to the SAME pattern but configured with different
// playing orders and step durations advance independently.
void test_targets_on_same_pattern_advance_independently() {
    ModulationRegistry reg;
    ModulationPattern& pattern = reg.pattern(0);
    for (uint8_t i = 0; i < 4; ++i) pattern.setStep(i, static_cast<uint8_t>(10 * (i + 1)));
    check(pattern.setLength(4), "pattern uses 4 active steps");

    const size_t a = reg.addTarget();
    const size_t b = reg.addTarget();
    check(a != ModulationRegistry::INVALID_INDEX && b != ModulationRegistry::INVALID_INDEX,
          "two target slots are available");
    ModulationTarget& beatFwd = reg.target(a);
    beatFwd.patternIndex = 0;  // 1 step per beat, forward
    ModulationTarget& barRev = reg.target(b);
    barRev.patternIndex = 0;
    barRev.stepDuration.unit = StepUnit::Bar;
    barRev.order = ModulationOrder::Reverse;

    const uint32_t expectA[] = {0, 1, 2, 3, 0, 1, 2, 3};
    const uint8_t valueA[] = {10, 20, 30, 40, 10, 20, 30, 40};
    const uint32_t expectB[] = {3, 3, 3, 3, 2, 2, 2, 2};
    const uint8_t valueB[] = {40, 40, 40, 40, 30, 30, 30, 30};
    for (uint64_t beat = 0; beat < 8; ++beat) {
        const uint64_t bar = beat / 4;
        const TransportPosition p = pos(beat, bar);
        const uint32_t ia = beatFwd.stepIndexForTransport(p, pattern.length());
        const uint32_t ib = barRev.stepIndexForTransport(p, pattern.length());
        check(ia == expectA[beat], "beat/forward target advances per beat");
        check(pattern.stepAt(ia) == valueA[beat], "beat/forward target reads its own steps");
        check(ib == expectB[beat], "bar/reverse target advances per bar, reversed");
        check(pattern.stepAt(ib) == valueB[beat], "bar/reverse target reads its own steps");
    }
}

// --- ModulationTarget: scaling/balance formula ------------------------------

// FR14: output = clamp(base + centered(step + balance) * scale * span / (64*127))
// — always clamped to the target range.
void test_apply_to_base_formula() {
    ModulationTarget t;  // factory: scale 64, balance 0
    check(t.scaleFactor == 64, "factory scale factor is 64");
    check(t.balance == 0, "factory balance is 0");

    check(t.applyToBase(50, 0, 127, 64) == 50, "a centered step leaves the base unchanged");
    check(t.applyToBase(0, 0, 127, 127) == 63, "scale 64 maps step 127 onto +63 units");
    check(t.applyToBase(64, 0, 127, 0) == 0, "scale 64 maps step 0 onto -64 units");

    t.scaleFactor = 127;  // full range
    check(t.applyToBase(0, 0, 127, 127) == 125, "scale 127 maps step 127 onto +125 units");
    check(t.applyToBase(5, 0, 127, 127) == 127, "positive excursion clamps to rangeMax");
    check(t.applyToBase(5, 0, 127, 0) == 0, "negative excursion clamps to rangeMin");
    check(t.applyToBase(42, 40, 44, 127) == 44, "a narrow range clamps to its rangeMax");

    t.scaleFactor = 0;
    check(t.applyToBase(77, 0, 127, 127) == 77, "scale 0 disables the modulation");
    check(t.applyToBase(200, 0, 127, 0) == 127, "output is clamped even when idle");

    t.scaleFactor = 64;
    t.balance = -6;
    check(t.applyToBase(50, 0, 127, 70) == 50, "balance shifts the step center (cancels +6)");
    t.balance = 0;
    check(t.applyToBase(50, 0, 127, 70) == 56, "step 70 at scale 64 adds +6");

    // Degenerate ranges stay total: a pinned range pins the output.
    check(t.applyToBase(90, 50, 50, 127) == 50, "a degenerate range pins the output");
}

// --- ModulationTarget: scope (FR13) ------------------------------------------

void test_default_scope_is_per_pattern() {
    ModulationTarget t;
    check(t.address.scope == ModulationScope::Pattern, "factory target scope is per-pattern");
    check(ModulationTarget{}.address.patternIndex == 0, "factory target pattern index is 0");
    ModulationRegistry reg;
    const size_t i = reg.addTarget();
    check(reg.target(i).address.scope == ModulationScope::Pattern,
          "registry slots default to per-pattern scope");
    reg.target(i).address.scope = ModulationScope::Global;
    check(reg.target(i).address.scope == ModulationScope::Global,
          "global scope is selectable per target");
}

// --- ModulationRegistry ------------------------------------------------------

// The fixed capacity of the target-slot pool is enforced; released slots are
// reusable; out-of-range access degrades to a safe default.
void test_registry_capacity_enforced() {
    ModulationRegistry reg;
    check(reg.activeTargetCount() == 0, "a fresh registry has no active targets");
    for (size_t i = 0; i < ModulationRegistry::MAX_TARGETS; ++i) {
        const size_t idx = reg.addTarget();
        check(idx == i, "addTarget claims sequential slots");
    }
    check(reg.activeTargetCount() == ModulationRegistry::MAX_TARGETS,
          "active count matches the fixed capacity");
    check(reg.addTarget() == ModulationRegistry::INVALID_INDEX,
          "target capacity is enforced: a full registry refuses new targets");
    check(reg.targetActive(3), "claimed slots report active");
    check(reg.releaseTarget(3), "release frees a claimed slot");
    check(!reg.targetActive(3), "a released slot reports inactive");
    check(reg.addTarget() == 3, "a released slot is reused");
    check(!reg.releaseTarget(ModulationRegistry::MAX_TARGETS), "out-of-range release is rejected");
    check(reg.releaseTarget(0), "a second release succeeds for a claimed slot");
    check(!reg.releaseTarget(0), "double release is rejected");
    reg.target(2).patternIndex = 5;  // claimed slot 2 stays writable
    check(reg.target(2).patternIndex == 5, "claimed slots round-trip config");

    reg.clearTargets();
    check(reg.activeTargetCount() == 0, "clearTargets empties every slot");
    check(!reg.targetActive(2), "cleared slots report inactive");

    // Global pattern pool: bounds-checked, safe fallback outside the pool.
    check(reg.patternIndexValid(0), "pattern 0 is valid");
    check(!reg.patternIndexValid(ModulationRegistry::MAX_PATTERNS),
          "pattern indices beyond the pool are invalid");
    reg.pattern(0).setStep(0, 42);
    check(reg.pattern(0).stepAt(0) == 42, "pattern storage round-trips");
    check(reg.pattern(ModulationRegistry::MAX_PATTERNS).stepAt(0) == 0,
          "out-of-range pattern access degrades to a safe default");

    reg.reset();
    check(reg.pattern(0).stepAt(0) == 0, "reset clears the patterns");
    check(reg.activeTargetCount() == 0, "reset releases the targets");
}

// The global registry is a single statically allocated instance.
void test_global_registry_singleton() {
    check(&g_modulationRegistry == &g_modulationRegistry,
          "the global registry is one statically allocated instance");
    check(g_modulationRegistry.activeTargetCount() <= ModulationRegistry::MAX_TARGETS,
          "the global registry respects its fixed capacity");
}

}  // namespace

int main() {
    test_step_values_clamped();
    test_length_and_wrap();
    test_step_index_beat_duration();
    test_step_index_bar_duration();
    test_step_index_derives_from_authoritative_transport();
    test_playing_orders();
    test_targets_on_same_pattern_advance_independently();
    test_apply_to_base_formula();
    test_default_scope_is_per_pattern();
    test_registry_capacity_enforced();
    test_global_registry_singleton();

    if (failures == 0) {
        std::printf("test_modulation_pattern: all checks passed\n");
        return 0;
    }
    std::printf("test_modulation_pattern: %d check(s) failed\n", failures);
    return 1;
}
