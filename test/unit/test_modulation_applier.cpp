// task 17 — modulation integration (FR11-FR14, AC3).
//
// Pins the per-tick ModulationApplier pass and its integration into the
// Sequencer tick loop, end to end:
//   - per-tick value: the applier derives each target's step index from the
//     single authoritative transport position, reads the pattern step value,
//     maps balance + scaling * (step - center) onto the target parameter's
//     range (clamped) and writes ONE absolute overlay on the ParameterBus.
//     The stored base (working state / timeline BPM) is NEVER modified —
//     modulating is strictly non-destructive (AC3);
//   - step advance follows the target's step duration in beats or bars;
//   - playing order (forward/reverse/ping-pong/random, deterministic) is
//     honoured independently per target;
//   - scope: a per-pattern target writes only its own pattern slot's
//     parameter, a global target writes the global parameter;
//   - disabling a target (or scale 0) clears its overlay; unconfigured
//     factory targets (deliberately INVALID addresses) modulate nothing —
//     with nothing configured the bus is exactly Milestone 2's;
//   - per-tick processing is bounded by the registry's fixed target
//     capacity and allocation-free (the whole pass is fixed-slot loops over
//     statically sized storage);
//   - BPM modulation changes the EFFECTIVE tempo through the existing
//     transport: tick durations follow the bus overlay without compounding
//     (every tick recomputes from the stored base), the stored BPM read
//     from the bus is always the user's base (never the modulated value),
//     and late-tick reconciliation keeps working (a multi-tick catch-up
//     window runs the same restore/recompute protocol without faults,
//     bursts or drift).
//
// Standalone unit test (own main()); registered in test/CMakeLists.txt.

#include "FakeMonotonicClock.h"
#include "ModulationApplier.h"
#include "ModulationRegistry.h"
#include "ModulationTarget.h"
#include "ParameterAddress.h"
#include "ParameterBus.h"
#include "RecordingMidiOutput.h"
#include "TransportTimeline.h"
#include "WorkingStateManager.h"
#include "command.h"
#include "midi_messages.h"
#include "sequencer.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <vector>

namespace midi = sequencer::midi;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// The applier is a stateless pass: it owns no per-tick state at all.
static_assert(std::is_empty<sequencer::ModulationApplier>::value,
              "the modulation pass must be stateless");
// The per-tick work is bounded by the fixed registry capacity.
static_assert(sequencer::ModulationRegistry::MAX_TARGETS == 16,
              "target capacity is a fixed compile-time bound");
static_assert(sequencer::ModulationRegistry::MAX_PATTERNS == 8,
              "pattern capacity is a fixed compile-time bound");

// Count occurrences of a status byte in a stream.
int countByte(const std::vector<uint8_t>& s, uint8_t b) {
    int n = 0;
    for (uint8_t x : s) if (x == b) ++n;
    return n;
}

constexpr sequencer::ParameterAddress bpmAddress() {
    return sequencer::ParameterAddress{sequencer::ParameterScope::Global, 0,
                                       sequencer::ParamId::GlobalBpm};
}
sequencer::ParameterAddress patternAddress(uint8_t slot, sequencer::ParamId id) {
    return sequencer::ParameterAddress{sequencer::ParameterScope::Pattern, slot, id};
}

// A locally bound timeline + working state + ParameterBus, so base values
// are real (read through the bus like production code).
struct BusHarness {
    sequencer::TransportTimeline timeline;
    sequencer::WorkingStateManager working;
    sequencer::ParameterBus bus;

    BusHarness() { timeline.setBpm(120); bus.bind(&timeline, &working); }
};

// Claim and fully configure one target slot. Mirrors how operator
// configuration will populate the registry (task 21), minus the UI.
sequencer::ModulationTarget& addConfiguredTarget(
    sequencer::ModulationRegistry& registry, uint8_t patternIndex,
    sequencer::ParameterAddress address,
    sequencer::StepDuration duration = {sequencer::StepUnit::Beat, 1},
    sequencer::ModulationOrder order = sequencer::ModulationOrder::Forward,
    uint8_t scale = 127, int8_t balance = 0, uint16_t randomSeed = 0) {
    const size_t slot = registry.addTarget();
    sequencer::ModulationTarget& t = registry.target(slot);
    t.patternIndex = patternIndex;
    t.address = address;
    t.scaleFactor = scale;
    t.balance = balance;
    t.stepDuration = duration;
    t.order = order;
    t.randomSeed = randomSeed;
    return t;
}

// The expected per-tick output of `target` at `position`: the task-15 pure
// helpers (pinned by test_modulation_pattern) define the composition this
// test pins through the applier.
int32_t expectedOutput(const sequencer::ModulationTarget& target,
                       const sequencer::ModulationRegistry& registry,
                       const sequencer::ParameterBus& bus,
                       const sequencer::TransportPosition& position) {
    const sequencer::ModulationPattern& pattern = registry.pattern(target.patternIndex);
    const uint32_t idx = target.stepIndexForTransport(position, pattern.length());
    const sequencer::ParamInfo* info = bus.paramInfo(target.address);
    return target.applyToBase(bus.baseValue(target.address), info->minValue, info->maxValue,
                              pattern.stepAt(idx));
}

// --------------------------------------------------------------------------
// Pure applier pass (no Sequencer): value formula, overlay writes, bases.
// --------------------------------------------------------------------------

// Per-tick value = clamp(balance + scaling * (step - center)) mapped onto the
// parameter range, written as ONE absolute overlay; base untouched.
void test_per_tick_value_formula_and_overlay_writes() {
    BusHarness h;
    sequencer::ModulationRegistry registry;

    // Pattern [100, 95, 64], target on Gate Steps (range 1..64), base 16.
    sequencer::ModulationPattern& pattern = registry.pattern(0);
    check(pattern.setLength(3), "pattern length 3");
    pattern.setStep(0, 100);
    pattern.setStep(1, 95);
    pattern.setStep(2, 64);
    check(h.bus.setBase(patternAddress(0, sequencer::ParamId::GateSteps), 16),
          "set Gate Steps base");
    addConfiguredTarget(registry, 0, patternAddress(0, sequencer::ParamId::GateSteps));

    // Beat 0 -> step 100: off = 36 * 127 * 63 / 8128 = 35 -> 51.
    sequencer::TransportPosition pos{0, 0};
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    const sequencer::ParameterAddress gate0 =
        patternAddress(0, sequencer::ParamId::GateSteps);
    check(h.bus.hasOverlay(gate0), "overlay written for beat 0");
    check(h.bus.effectiveValue(gate0) == 51, "beat 0 -> step 100 -> effective 51");
    check(h.bus.baseValue(gate0) == 16, "stored base untouched (modulation is non-destructive)");

    // Beat 1 -> step 95 -> 46; beat 2 -> step 64 (center) -> 16.
    pos.beatIndex = 1;
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    check(h.bus.effectiveValue(gate0) == 46, "beat 1 -> step 95 -> effective 46");
    pos.beatIndex = 2;
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    check(h.bus.effectiveValue(gate0) == 16, "beat 2 -> step 64 (center) -> effective 16 == base");

    // Beat 3 wraps to step 100 again (forward order over a 3-step pattern).
    pos.beatIndex = 3;
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    check(h.bus.effectiveValue(gate0) == 51, "beat 3 wraps to step 100 -> 51");

    // Every processed position writes exactly the task-15 composition.
    for (uint64_t beat = 0; beat < 10; ++beat) {
        pos.beatIndex = beat;
        sequencer::ModulationApplier::process(pos, registry, h.bus);
        check(h.bus.effectiveValue(gate0) ==
                  expectedOutput(registry.target(0), registry, h.bus, pos),
              "per-tick value matches the task-15 step/scaling composition");
    }
    // The stored base was never touched by any of the passes above.
    check(h.bus.baseValue(gate0) == 16, "base preserved across the whole sweep");
}

// Balance offsets the curve; outputs clamp at the parameter range edges.
void test_balance_offset_and_range_clamping() {
    BusHarness h;
    sequencer::ModulationRegistry registry;

    // Pattern 0: [127, 0, 64] — full-scale steps. Pattern 1: centre steps.
    sequencer::ModulationPattern& p0 = registry.pattern(0);
    p0.setLength(3);
    p0.setStep(0, 127);
    p0.setStep(1, 0);
    p0.setStep(2, 64);
    sequencer::ModulationPattern& p1 = registry.pattern(1);
    p1.setLength(3);
    p1.setStep(0, 64);
    p1.setStep(1, 64);
    p1.setStep(2, 64);

    for (uint8_t slot = 0; slot < 3; ++slot) {
        check(h.bus.setBase(patternAddress(slot, sequencer::ParamId::GateSteps), 16),
              "set Gate Steps base");
    }
    // balance 0 (steps 127/0), balance +8 and balance -8 (step 64).
    addConfiguredTarget(registry, 0, patternAddress(0, sequencer::ParamId::GateSteps));
    addConfiguredTarget(registry, 1, patternAddress(1, sequencer::ParamId::GateSteps), {sequencer::StepUnit::Beat, 1}, sequencer::ModulationOrder::Forward, 127, 8);
    addConfiguredTarget(registry, 1, patternAddress(2, sequencer::ParamId::GateSteps), {sequencer::StepUnit::Beat, 1}, sequencer::ModulationOrder::Forward, 127, -8);

    sequencer::TransportPosition pos{0, 0};
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    check(h.bus.effectiveValue(patternAddress(0, sequencer::ParamId::GateSteps)) == 64,
          "step 127 from base 16 clamps at range max 64");
    check(h.bus.baseValue(patternAddress(0, sequencer::ParamId::GateSteps)) == 16,
          "base intact while clamped high");
    check(h.bus.effectiveValue(patternAddress(1, sequencer::ParamId::GateSteps)) == 23,
          "balance +8 lifts the centre step: 16 + 7 -> 23");
    check(h.bus.effectiveValue(patternAddress(2, sequencer::ParamId::GateSteps)) == 9,
          "balance -8 drops the centre step: 16 - 7 -> 9");

    pos.beatIndex = 1;  // step 0 of pattern 0
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    check(h.bus.effectiveValue(patternAddress(0, sequencer::ParamId::GateSteps)) == 1,
          "step 0 from base 16 clamps at range min 1");
    check(h.bus.baseValue(patternAddress(0, sequencer::ParamId::GateSteps)) == 16,
          "base intact while clamped low");
}

// Step advance follows the configured step duration in beats or bars.
void test_step_duration_beat_and_bar() {
    BusHarness h;
    sequencer::ModulationRegistry registry;

    // Pattern [10, 60, 110]: distinct effective values 1, 13, 61 from base 16.
    sequencer::ModulationPattern& pattern = registry.pattern(0);
    pattern.setLength(3);
    pattern.setStep(0, 10);
    pattern.setStep(1, 60);
    pattern.setStep(2, 110);
    check(h.bus.setBase(patternAddress(0, sequencer::ParamId::GateSteps), 16), "base p0");
    check(h.bus.setBase(patternAddress(1, sequencer::ParamId::GateSteps), 16), "base p1");

    // Beat/2: advances every two beats. Bar/1: advances every bar.
    addConfiguredTarget(registry, 0, patternAddress(0, sequencer::ParamId::GateSteps),
                        {sequencer::StepUnit::Beat, 2});
    addConfiguredTarget(registry, 0, patternAddress(1, sequencer::ParamId::GateSteps),
                        {sequencer::StepUnit::Bar, 1});

    static const int32_t kBeat2Expected[8] = {1, 1, 13, 13, 61, 61, 1, 1};
    static const int32_t kBar1Expected[8] = {1, 1, 1, 1, 13, 13, 13, 13};
    for (uint64_t beat = 0; beat < 8; ++beat) {
        sequencer::TransportPosition pos{beat, beat / 4};
        sequencer::ModulationApplier::process(pos, registry, h.bus);
        check(h.bus.effectiveValue(patternAddress(0, sequencer::ParamId::GateSteps)) ==
                  kBeat2Expected[beat],
              "beat/2 target advances every two beats");
        check(h.bus.effectiveValue(patternAddress(1, sequencer::ParamId::GateSteps)) ==
                  kBar1Expected[beat],
              "bar/1 target advances every bar");
    }

    // Bar-derived positions (beat resets inside a later bar).
    sequencer::TransportPosition pos{1, 2};
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    check(h.bus.effectiveValue(patternAddress(1, sequencer::ParamId::GateSteps)) == 61,
          "bar 2 reads step 2 (110) regardless of beat position");
}

// Playing order is honoured independently per target.
void test_playing_order_per_target() {
    BusHarness h;
    sequencer::ModulationRegistry registry;

    // Pattern [0, 40, 80, 120]. On Pitch Low Note (range 0..127) with scale
    // 64 and base 64 the effective value IS the step value (off == centered).
    sequencer::ModulationPattern& pattern = registry.pattern(0);
    check(pattern.setLength(4), "length 4");
    pattern.setStep(0, 0);
    pattern.setStep(1, 40);
    pattern.setStep(2, 80);
    pattern.setStep(3, 120);
    const sequencer::ParamId id = sequencer::ParamId::PitchLowNote;
    for (uint8_t slot = 0; slot < 4; ++slot) {
        check(h.bus.setBase(patternAddress(slot, id), 64), "set Pitch Low base");
    }
    addConfiguredTarget(registry, 0, patternAddress(0, id), {sequencer::StepUnit::Beat, 1},
                        sequencer::ModulationOrder::Forward, 64, 0);
    addConfiguredTarget(registry, 0, patternAddress(1, id), {sequencer::StepUnit::Beat, 1},
                        sequencer::ModulationOrder::Reverse, 64, 0);
    addConfiguredTarget(registry, 0, patternAddress(2, id), {sequencer::StepUnit::Beat, 1},
                        sequencer::ModulationOrder::PingPong, 64, 0);
    addConfiguredTarget(registry, 0, patternAddress(3, id), {sequencer::StepUnit::Beat, 1},
                        sequencer::ModulationOrder::Random, 64, 0, 0xC0FF);

    static const int32_t kForward[8] = {0, 40, 80, 120, 0, 40, 80, 120};
    static const int32_t kReverse[8] = {120, 80, 40, 0, 120, 80, 40, 0};
    static const int32_t kPingPong[8] = {0, 40, 80, 120, 80, 40, 0, 40};
    for (uint64_t beat = 0; beat < 8; ++beat) {
        sequencer::TransportPosition pos{beat, 0};
        sequencer::ModulationApplier::process(pos, registry, h.bus);
        check(h.bus.effectiveValue(patternAddress(0, id)) == kForward[beat],
              "forward order advances 0,1,2,3 and wraps");
        check(h.bus.effectiveValue(patternAddress(1, id)) == kReverse[beat],
              "reverse order runs backwards independently");
        check(h.bus.effectiveValue(patternAddress(2, id)) == kPingPong[beat],
              "ping-pong order bounces 0,1,2,3,2,1");
        check(h.bus.effectiveValue(patternAddress(3, id)) ==
                  expectedOutput(registry.target(3), registry, h.bus, pos),
              "random order matches the deterministic seed/counter draw");
    }

    // Random order is deterministic: the same position reproduces the same
    // overlay (same seed, same counter — no hidden state in the applier).
    sequencer::TransportPosition pos{5, 0};
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    const int32_t first = h.bus.effectiveValue(patternAddress(3, id));
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    check(h.bus.effectiveValue(patternAddress(3, id)) == first,
          "random order is deterministic per position (reproducible, no applier state)");
}

// Scope: a per-pattern target writes only its own pattern slot; a global
// target writes the single global parameter.
void test_scope_per_pattern_vs_global() {
    BusHarness h;
    sequencer::ModulationRegistry registry;

    sequencer::ModulationPattern& pattern = registry.pattern(0);
    pattern.setLength(2);
    pattern.setStep(0, 100);  // -> GateSteps 51 from base 16; BPM 240 from 120
    pattern.setStep(1, 64);
    check(h.bus.setBase(patternAddress(0, sequencer::ParamId::GateSteps), 16), "base p0");
    check(h.bus.setBase(patternAddress(1, sequencer::ParamId::GateSteps), 16), "base p1");

    addConfiguredTarget(registry, 0, patternAddress(0, sequencer::ParamId::GateSteps));
    addConfiguredTarget(registry, 0, bpmAddress());

    sequencer::TransportPosition pos{0, 0};
    sequencer::ModulationApplier::process(pos, registry, h.bus);

    check(h.bus.hasOverlay(patternAddress(0, sequencer::ParamId::GateSteps)),
          "per-pattern target arms its own slot's overlay");
    check(!h.bus.hasOverlay(patternAddress(1, sequencer::ParamId::GateSteps)),
          "per-pattern target does not touch any other pattern slot");
    check(h.bus.effectiveValue(patternAddress(1, sequencer::ParamId::GateSteps)) == 16,
          "other pattern reads its stored base");
    check(h.bus.hasOverlay(bpmAddress()), "global target arms the global overlay");
    check(h.bus.effectiveValue(bpmAddress()) == 240,
          "global BPM target maps step 100 onto BPM 240 (from base 120)");
    check(h.bus.baseValue(bpmAddress()) == 120,
          "global BPM base (the transport's stored tempo) is untouched");
    check(h.bus.baseValue(patternAddress(0, sequencer::ParamId::GateSteps)) == 16,
          "per-pattern base untouched");
}

// Disabled targets, scale-0 targets, factory (invalid) addresses and inactive
// slots contribute nothing — and a disabled target's overlay is cleared.
void test_disable_and_inert_slots() {
    BusHarness h;
    sequencer::ModulationRegistry registry;

    sequencer::ModulationPattern& pattern = registry.pattern(0);
    pattern.setLength(2);
    pattern.setStep(0, 100);
    pattern.setStep(1, 95);
    const sequencer::ParameterAddress gate0 =
        patternAddress(0, sequencer::ParamId::GateSteps);
    check(h.bus.setBase(gate0, 16), "base p0");

    sequencer::ModulationTarget& t = addConfiguredTarget(registry, 0, gate0);

    sequencer::TransportPosition pos{0, 0};
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    check(h.bus.hasOverlay(gate0) && h.bus.effectiveValue(gate0) == 51,
          "enabled target arms the overlay");

    // Disabling clears the overlay: the parameter reads its stored base again.
    t.enabled = false;
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    check(!h.bus.hasOverlay(gate0), "disabled target clears its overlay");
    check(h.bus.effectiveValue(gate0) == 16, "cleared overlay falls back to the stored base");

    // Re-enabling resumes from the preserved configuration (step 1 -> 46).
    t.enabled = true;
    pos.beatIndex = 1;
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    check(h.bus.hasOverlay(gate0) && h.bus.effectiveValue(gate0) == 46,
          "re-enabled target resumes from its preserved configuration");

    // FR14: scale 0 is the "modulation off" point — behaves like disabled.
    t.scaleFactor = 0;
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    check(!h.bus.hasOverlay(gate0), "scale 0 (FR14) clears the overlay");

    // Factory targets carry a deliberately INVALID address: they modulate
    // nothing, no matter the position.
    t.scaleFactor = 127;
    t.address = sequencer::ParameterAddress{};
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    check(!h.bus.hasOverlay(gate0) && !h.bus.hasOverlay(bpmAddress()),
          "unconfigured (invalid-address) target modulates nothing");
    check(h.bus.effectiveValue(gate0) == 16, "parameter reads its base with an invalid target");

    // A registry with no configured targets writes nothing at all.
    sequencer::ModulationRegistry fresh;
    sequencer::ModulationApplier::process(pos, fresh, h.bus);
    check(!h.bus.hasOverlay(gate0) && !h.bus.hasOverlay(bpmAddress()),
          "empty registry (Milestone 2 default) arms no overlays");

    // An unbound bus is still safe: overlays are bus-local and stay clamped.
    sequencer::ParameterBus unbound;
    sequencer::ModulationRegistry solo;
    sequencer::ModulationPattern& p = solo.pattern(0);
    p.setStep(0, 100);
    addConfiguredTarget(solo, 0, bpmAddress());
    pos = sequencer::TransportPosition{0, 0};
    sequencer::ModulationApplier::process(pos, solo, unbound);
    check(unbound.hasOverlay(bpmAddress()) && unbound.effectiveValue(bpmAddress()) == 120,
          "unbound bus: process is total and the overlay stays in range");
}

// The per-tick pass services the full fixed target capacity.
void test_capacity_bound() {
    BusHarness h;
    sequencer::ModulationRegistry registry;

    // Claim every slot: the registry is a fixed-capacity table (the tick
    // loop's modulation cost is bounded by this, never by configuration).
    size_t slots[sequencer::ModulationRegistry::MAX_TARGETS];
    for (size_t i = 0; i < sequencer::ModulationRegistry::MAX_TARGETS; ++i) {
        slots[i] = registry.addTarget();
        check(slots[i] != sequencer::ModulationRegistry::INVALID_INDEX, "slot claimed");
    }
    check(registry.addTarget() == sequencer::ModulationRegistry::INVALID_INDEX,
          "17th target rejected: fixed capacity");
    check(registry.activeTargetCount() == sequencer::ModulationRegistry::MAX_TARGETS,
          "registry reports a full table");

    // Configure all 16 on distinct addresses: half GateSteps (-> 51 from 16),
    // half Pitch Low Note (-> clamped 127 from 64), all reading step 100.
    sequencer::ModulationPattern& pattern = registry.pattern(0);
    pattern.setLength(2);
    pattern.setStep(0, 100);
    pattern.setStep(1, 100);
    for (size_t i = 0; i < 8; ++i) {
        const uint8_t slot = static_cast<uint8_t>(i);
        sequencer::ModulationTarget& t = registry.target(slots[i]);
        t.patternIndex = 0;
        t.address = patternAddress(slot, sequencer::ParamId::GateSteps);
        t.scaleFactor = 127;
        check(h.bus.setBase(t.address, 16), "gate base");
        sequencer::ModulationTarget& u = registry.target(slots[i + 8]);
        u.patternIndex = 0;
        u.address = patternAddress(slot, sequencer::ParamId::PitchLowNote);
        u.scaleFactor = 127;
        check(h.bus.setBase(u.address, 64), "pitch base");
    }

    sequencer::TransportPosition pos{0, 0};
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    for (size_t i = 0; i < 8; ++i) {
        const uint8_t slot = static_cast<uint8_t>(i);
        check(h.bus.hasOverlay(patternAddress(slot, sequencer::ParamId::GateSteps)) &&
                  h.bus.effectiveValue(patternAddress(slot, sequencer::ParamId::GateSteps)) == 51,
              "full-capacity pass reaches every GateSteps target");
        check(h.bus.hasOverlay(patternAddress(slot, sequencer::ParamId::PitchLowNote)) &&
                  h.bus.effectiveValue(patternAddress(slot, sequencer::ParamId::PitchLowNote)) == 127,
              "full-capacity pass reaches every PitchLowNote target");
    }

    // A disabled slot mid-table contributes nothing; the rest still process.
    registry.target(slots[4]).enabled = false;
    sequencer::ModulationApplier::process(pos, registry, h.bus);
    check(!h.bus.hasOverlay(patternAddress(4, sequencer::ParamId::GateSteps)),
          "disabled mid-table slot clears its overlay");
    check(h.bus.hasOverlay(patternAddress(5, sequencer::ParamId::GateSteps)) &&
              h.bus.effectiveValue(patternAddress(5, sequencer::ParamId::GateSteps)) == 51,
          "slots around the disabled one still process");
}

// --------------------------------------------------------------------------
// Sequencer integration: the real tick loop, g_modulationRegistry and the
// Sequencer's bound ParameterBus (the global registry is reset around each
// test — it is process-wide).
// --------------------------------------------------------------------------

struct SeqHarness {
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
    sequencer::GeneratorRegistry registry;
    sequencer::Sequencer seq{out, clock, registry};
    uint64_t base = 0;

    void play() {
        base = clock.nowMicros();
        commands::CommandMessage m;
        m.cmd = commands::Command::PLAY;
        seq.processCommand(m);
    }
    void stop() {
        commands::CommandMessage m;
        m.cmd = commands::Command::STOP;
        seq.processCommand(m);
    }
    sequencer::ParameterBus& bus() { return seq.parameterBus(); }
};

// Advance the fake clock in small steps until exactly one new Clock byte is
// dispatched, then report the wall-clock gap in microseconds (0 if none
// within budget). Small steps keep every serviced tick authoritative (no
// reconciliation), and the gap measures the transport's real tick duration.
uint64_t driveOneClock(SeqHarness& h, uint64_t budgetUs) {
    const int before = countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK);
    uint64_t gap = 0;
    while (gap < budgetUs) {
        h.clock.advanceMicros(500);
        gap += 500;
        h.seq.update();
        if (countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK) > before) {
            return gap;
        }
    }
    return 0;
}

// Configure the global registry with the standard tempo test shape: BPM
// target on a 2-step pattern [100, 64] — beat 0 modulates 120 -> 240, beat 1
// holds 120 (centre). Effective BPM must be exactly 240/120 per beat.
void configureTempoTarget() {
    sequencer::ModulationRegistry& registry = sequencer::g_modulationRegistry;
    registry.reset();
    sequencer::ModulationPattern& pattern = registry.pattern(0);
    pattern.setLength(2);
    pattern.setStep(0, 100);
    pattern.setStep(1, 64);
    const size_t slot = registry.addTarget();
    sequencer::ModulationTarget& t = registry.target(slot);
    t.patternIndex = 0;
    t.address = bpmAddress();
    t.scaleFactor = 127;
    t.balance = 0;
    t.stepDuration = {sequencer::StepUnit::Beat, 1};
    t.order = sequencer::ModulationOrder::Forward;
}

// No modulation configured: the bus carries no overlays at all and the
// transport runs at the stored base tempo (Milestone 2 behaviour).
void test_sequencer_default_unmodulated() {
    sequencer::g_modulationRegistry.reset();
    SeqHarness h;
    h.play();
    for (int i = 0; i < 3; ++i) {
        const uint64_t gap = driveOneClock(h, 260000);
        check(gap > 18000 && gap < 24000, "unmodulated transport ticks at the base tempo");
    }
    check(!h.bus().hasOverlay(bpmAddress()), "no BPM overlay without modulation");
    check(h.bus().effectiveValue(bpmAddress()) == 120, "effective BPM is the base 120");
    check(h.bus().baseValue(bpmAddress()) == 120, "stored base BPM 120");
    check(!h.bus().hasOverlay(patternAddress(0, sequencer::ParamId::GateSteps)),
          "no pattern overlays without modulation");
    h.stop();
    sequencer::g_modulationRegistry.reset();
}

// BPM modulation drives the transport's tick duration through the existing
// timeline, without compounding and without touching the stored base.
void test_sequencer_bpm_modulation_tempo() {
    configureTempoTarget();
    SeqHarness h;
    h.play();

    // The first serviced tick (index 1 — tick 0 is the start anchor) runs
    // at the base tempo and arms the first BPM overlay.
    uint64_t gap = driveOneClock(h, 260000);
    check(gap != 0, "first tick serviced");
    check(h.bus().hasOverlay(bpmAddress()), "BPM overlay armed by the first tick");
    check(h.bus().effectiveValue(bpmAddress()) == 240, "beat 0 overlay is 240");
    check(h.bus().baseValue(bpmAddress()) == 120, "stored BPM still 120 (never modified)");

    // From the next tick the transport runs at the effective tempo: tick
    // duration halves (120 -> 240 BPM), and every further tick keeps exactly
    // 240 — each tick recomputes the overlay from the stored base, so the
    // modulation never compounds.
    for (int i = 0; i < 12; ++i) {
        gap = driveOneClock(h, 260000);
        check(gap > 9000 && gap < 12000, "tick duration follows the effective 240 BPM");
        check(h.bus().effectiveValue(bpmAddress()) == 240,
              "effective BPM stays exactly 240 (no compounding)");
        check(h.bus().baseValue(bpmAddress()) == 120,
              "stored BPM stays 120 after every update() window");
    }
    const int clocks = countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK);
    check(clocks == 13, "exactly one Clock byte per serviced tick (no bursts)");
    h.stop();
    sequencer::g_modulationRegistry.reset();
}

// The BPM target's step advances relative to the authoritative timeline's
// beats: when beat 1 (step 64, the centre) comes up, the effective tempo
// returns to the base and the transport slows down live.
void test_sequencer_beat_relative_step_change() {
    configureTempoTarget();
    SeqHarness h;
    h.play();

    uint64_t gap = driveOneClock(h, 260000);  // first serviced tick (index 1)
    check(gap != 0, "first tick serviced");
    // Serviced ticks 2..23 stay inside beat 0 (tick 0 is the start anchor; a
    // beat holds 24 ticks, so tick 24 is the first of beat 1).
    for (int i = 0; i < 22; ++i) {
        gap = driveOneClock(h, 260000);
        check(gap > 9000 && gap < 12000, "beat 0 runs at the modulated tempo");
        check(h.bus().effectiveValue(bpmAddress()) == 240, "beat 0 overlay stays 240");
    }
    // The next serviced tick is index 24: the first tick of beat 1 -> step 64
    // (centre) -> 120.
    gap = driveOneClock(h, 260000);
    check(h.bus().effectiveValue(bpmAddress()) == 120,
          "beat 1 reads the centre step: effective BPM returns to 120");
    // The transport slows back down for the NEXT tick (one full beat period).
    gap = driveOneClock(h, 260000);
    check(gap > 18000 && gap < 24000, "transport slows back to the 120 BPM duration");
    check(h.bus().baseValue(bpmAddress()) == 120, "stored BPM untouched across the change");
    h.stop();
    sequencer::g_modulationRegistry.reset();
}

// Late-tick reconciliation survives BPM modulation (AC3): one late window
// services several missed ticks without faults, clock bursts or compounding.
void test_sequencer_late_tick_reconciliation() {
    configureTempoTarget();
    SeqHarness h;
    h.play();

    check(driveOneClock(h, 260000) != 0, "tick 0 serviced");
    for (int i = 0; i < 3; ++i) {
        check(driveOneClock(h, 260000) > 9000, "modulated ticks serviced");
    }
    const int clocksBefore = countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK);

    // Jump the clock three modulated tick-periods in ONE step: the three
    // missed ticks are reconciled (no obsolete Note On / no Clock for them)
    // and only the final on-phase tick dispatches its Clock.
    h.clock.advanceMicros(3 * 10417);
    h.seq.update();

    const int clocksAfter = countByte(h.out.record(), midi::SystemRealTimeMessage::TIMING_CLOCK);
    check(clocksAfter == clocksBefore + 1, "late window reconciles without clock bursts");
    check(countByte(h.out.record(), midi::SystemRealTimeMessage::STOP) == 0,
          "late window does not fault (below one beat)");
    check(h.bus().effectiveValue(bpmAddress()) == 240,
          "multi-tick window recomputes from the base: still exactly 240 (no compounding)");
    check(h.bus().baseValue(bpmAddress()) == 120, "stored BPM intact after the late window");

    // The transport is coherent afterwards: ticks keep coming at 240 BPM.
    const uint64_t gap = driveOneClock(h, 260000);
    check(gap > 9000 && gap < 12000, "transport still runs at the modulated tempo after catch-up");
    h.stop();
    sequencer::g_modulationRegistry.reset();
}

// Per-pattern isolation through the real tick loop, and Stop clears every
// modulation overlay so stored bases rule again until Play re-arms them.
void test_sequencer_stop_clears_and_per_pattern_isolation() {
    configureTempoTarget();  // global BPM target
    // Plus a per-pattern GateSteps target on pattern 0 only.
    sequencer::ModulationRegistry& registry = sequencer::g_modulationRegistry;
    sequencer::ModulationPattern& pattern = registry.pattern(1);  // unused step source
    (void)pattern;
    const size_t slot = registry.addTarget();
    sequencer::ModulationTarget& t = registry.target(slot);
    t.patternIndex = 0;
    t.address = patternAddress(0, sequencer::ParamId::GateSteps);
    t.scaleFactor = 127;
    t.balance = 0;
    t.stepDuration = {sequencer::StepUnit::Beat, 1};
    t.order = sequencer::ModulationOrder::Forward;

    SeqHarness h;
    check(h.bus().setBase(patternAddress(0, sequencer::ParamId::GateSteps), 16),
          "Gate Steps base 16");
    h.play();
    check(driveOneClock(h, 260000) != 0, "tick 0 serviced");
    check(driveOneClock(h, 260000) != 0, "tick 1 serviced");

    check(h.bus().hasOverlay(bpmAddress()), "global target armed through the tick loop");
    check(h.bus().hasOverlay(patternAddress(0, sequencer::ParamId::GateSteps)),
          "per-pattern target armed through the tick loop");
    check(h.bus().effectiveValue(patternAddress(0, sequencer::ParamId::GateSteps)) == 51,
          "per-pattern overlay is the expected 51 from base 16");
    check(!h.bus().hasOverlay(patternAddress(1, sequencer::ParamId::GateSteps)),
          "pattern 1's parameter is untouched by pattern 0's target");
    check(h.bus().baseValue(patternAddress(0, sequencer::ParamId::GateSteps)) == 16,
          "per-pattern base untouched");
    check(h.bus().baseValue(bpmAddress()) == 120, "stored BPM untouched");

    // Stop clears every overlay (the reset ParameterBus documents for the
    // play/stop lifecycle): stored bases rule again.
    h.stop();
    check(!h.bus().hasOverlay(bpmAddress()), "stop clears the global overlay");
    check(!h.bus().hasOverlay(patternAddress(0, sequencer::ParamId::GateSteps)),
          "stop clears the per-pattern overlay");
    check(h.bus().effectiveValue(bpmAddress()) == 120, "BPM reads its base after stop");
    check(h.bus().effectiveValue(patternAddress(0, sequencer::ParamId::GateSteps)) == 16,
          "GateSteps reads its base after stop");

    // Play re-arms the overlays from tick 0 again.
    h.play();
    check(driveOneClock(h, 260000) != 0, "tick 0 after re-play serviced");
    check(h.bus().hasOverlay(bpmAddress()) && h.bus().effectiveValue(bpmAddress()) == 240,
          "modulation resumes after Play");
    check(h.bus().hasOverlay(patternAddress(0, sequencer::ParamId::GateSteps)) &&
              h.bus().effectiveValue(patternAddress(0, sequencer::ParamId::GateSteps)) == 51,
          "per-pattern modulation resumes after Play");
    h.stop();
    sequencer::g_modulationRegistry.reset();
}

}  // namespace

int main() {
    test_per_tick_value_formula_and_overlay_writes();
    test_balance_offset_and_range_clamping();
    test_step_duration_beat_and_bar();
    test_playing_order_per_target();
    test_scope_per_pattern_vs_global();
    test_disable_and_inert_slots();
    test_capacity_bound();
    test_sequencer_default_unmodulated();
    test_sequencer_bpm_modulation_tempo();
    test_sequencer_beat_relative_step_change();
    test_sequencer_late_tick_reconciliation();
    test_sequencer_stop_clears_and_per_pattern_isolation();

    if (failures == 0) std::printf("test_modulation_applier: all checks passed\n");
    else std::printf("test_modulation_applier: %d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
