// generative-engine-expansion task 11 — three-level timing policy (FR10, AC7).
//
// Validates the generative-engine timing configuration (OperationTimingConfig)
// and the three-level resolution + apply-tick math added to TimingPolicy:
//
//   Level 1  a per-invocation override wins outright;
//   Level 2  else the operation's configured mode (unless it is Inherit);
//   Level 3  else the global default mode.
//
// Target ticks are computed from the AUTHORITATIVE TransportTimeline:
//   Immediate    -> the next serviced tick (the current logical tick index);
//   QuantizeBeat -> the next beat boundary;
//   QuantizeBar  -> the next bar boundary.
//
// A request arriving EXACTLY on a boundary resolves to the NEXT boundary of
// that kind, never the tick currently being serviced — the deterministic
// Milestone-2 convention (zero logical-tick application error) that keeps
// on-boundary requests race-free under late-tick reconciliation, and a LATE
// service still applies a due operation (isDue: applied, never skipped).
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/ so
// the property-test glob (test/*.cpp) never sweeps it; registered in the host
// test build under the ctest name "test_timing_policy".

#include "TimingPolicy.h"
#include "OperationTimingConfig.h"
#include "TransportTimeline.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

using sequencer::GenerativeOperation;
using sequencer::OperationTimingConfig;
using sequencer::OperationTimingMode;
using sequencer::TransportTimeline;
namespace tp = sequencer::timing_policy;

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

constexpr OperationTimingMode IMM = OperationTimingMode::Immediate;
constexpr OperationTimingMode BEAT = OperationTimingMode::QuantizeBeat;
constexpr OperationTimingMode BAR = OperationTimingMode::QuantizeBar;
constexpr OperationTimingMode INH = OperationTimingMode::Inherit;

constexpr GenerativeOperation ALL_OPS[] = {
    GenerativeOperation::Mutate,
    GenerativeOperation::Undo,
    GenerativeOperation::Revert,
    GenerativeOperation::Keep,
    GenerativeOperation::GeneratorChange,
    GenerativeOperation::ScaleChange,
    GenerativeOperation::ModulationEnable,
};

// 4/4 @ 120 BPM (PPQN 24): beat = 24 ticks, bar = 96 ticks; advanced to `tick`.
TransportTimeline timelineAt(uint64_t tick) {
    TransportTimeline tl;
    tl.setBpm(120);
    tl.start(0);
    while (tl.tickIndex() < tick) tl.advance();
    return tl;
}

// Same, under an explicit meter.
TransportTimeline timelineAt(common::TimeSignature ts, uint64_t tick) {
    TransportTimeline tl;
    tl.setBpm(120);
    tl.setTimeSignature(ts);
    tl.start(0);
    while (tl.tickIndex() < tick) tl.advance();
    return tl;
}

// --- Factory defaults ------------------------------------------------------

void test_factory_defaults() {
    check(sequencer::GENERATIVE_OPERATION_COUNT == 7,
          "seven timed generative operations (fixed static table size)");
    check(sizeof(ALL_OPS) / sizeof(ALL_OPS[0]) == sequencer::GENERATIVE_OPERATION_COUNT,
          "the test table covers every operation");
    // Approved per-operation defaults: recovery gestures stay Immediate;
    // musical content changes quantize to the beat; structural changes to the bar.
    check(sequencer::factoryDefaultMode(GenerativeOperation::Mutate) == BEAT,
          "Mutate factory default QuantizeBeat");
    check(sequencer::factoryDefaultMode(GenerativeOperation::Undo) == IMM,
          "Undo factory default Immediate");
    check(sequencer::factoryDefaultMode(GenerativeOperation::Revert) == IMM,
          "Revert factory default Immediate");
    check(sequencer::factoryDefaultMode(GenerativeOperation::Keep) == IMM,
          "Keep factory default Immediate");
    check(sequencer::factoryDefaultMode(GenerativeOperation::GeneratorChange) == BAR,
          "GeneratorChange factory default QuantizeBar");
    check(sequencer::factoryDefaultMode(GenerativeOperation::ScaleChange) == BAR,
          "ScaleChange factory default QuantizeBar");
    check(sequencer::factoryDefaultMode(GenerativeOperation::ModulationEnable) == BEAT,
          "ModulationEnable factory default QuantizeBeat");
    check(sequencer::factoryGlobalTimingDefault() == IMM,
          "shipped global default is Immediate");
    // factoryDefaults() carries exactly the approved defaults.
    const OperationTimingConfig cfg = OperationTimingConfig::factoryDefaults();
    for (GenerativeOperation op : ALL_OPS) {
        check(cfg.modeFor(op) == sequencer::factoryDefaultMode(op),
              "factoryDefaults() matches factoryDefaultMode() for every operation");
    }
    check(cfg.globalDefaultMode() == sequencer::factoryGlobalTimingDefault(),
          "factoryDefaults() carries the shipped global default");
}

// --- Level 1: per-invocation override wins ---------------------------------

void test_override_wins() {
    for (GenerativeOperation op : ALL_OPS) {
        OperationTimingConfig cfg = OperationTimingConfig::factoryDefaults();
        // Levels 2 and 3 disagree with the override so a win is observable.
        cfg.setMode(op, BAR);
        cfg.setGlobalDefault(IMM);
        check(tp::resolveTiming(op, cfg) == BAR,
              "no override: the explicit operation mode is used");
        check(tp::resolveTiming(op, cfg, {true, IMM}) == IMM,
              "override Immediate wins over the explicit QuantizeBar mode");
        check(tp::resolveTiming(op, cfg, {true, BEAT}) == BEAT,
              "override QuantizeBeat wins over the explicit QuantizeBar mode");
        // Inherit + a disagreeing global default: the override still wins.
        cfg.setMode(op, INH);
        cfg.setGlobalDefault(BAR);
        check(tp::resolveTiming(op, cfg) == BAR,
              "no override: Inherit falls through to the global default");
        check(tp::resolveTiming(op, cfg, {true, IMM}) == IMM,
              "override Immediate wins over the inherited global QuantizeBar");
        check(tp::resolveTiming(op, cfg, {false, IMM}) == BAR,
              "an INACTIVE override is ignored");
    }
}

// --- Level 2: explicit operation mode wins ---------------------------------

void test_operation_mode_wins() {
    OperationTimingConfig cfg = OperationTimingConfig::factoryDefaults();
    cfg.setGlobalDefault(BAR);  // level 3 disagrees with everything below.
    cfg.setMode(GenerativeOperation::Mutate, IMM);
    check(tp::resolveTiming(GenerativeOperation::Mutate, cfg) == IMM,
          "explicit Immediate beats the global QuantizeBar default");
    cfg.setMode(GenerativeOperation::Mutate, BEAT);
    check(tp::resolveTiming(GenerativeOperation::Mutate, cfg) == BEAT,
          "explicit QuantizeBeat beats the global QuantizeBar default");
    // A global-default change never moves an explicitly configured operation.
    cfg.setGlobalDefault(IMM);
    check(tp::resolveTiming(GenerativeOperation::Mutate, cfg) == BEAT,
          "explicit mode is immune to a global-default change");
    // Factory defaults are themselves explicit level-2 modes.
    check(tp::resolveTiming(GenerativeOperation::Undo, cfg) == IMM,
          "factory-configured Undo resolves Immediate");
    check(tp::resolveTiming(GenerativeOperation::GeneratorChange, cfg) == BAR,
          "factory-configured GeneratorChange resolves QuantizeBar");
}

// --- Level 3: global default for Inherit ------------------------------------

void test_global_default_applies() {
    OperationTimingConfig cfg = OperationTimingConfig::factoryDefaults();
    cfg.setMode(GenerativeOperation::Mutate, INH);
    cfg.setGlobalDefault(IMM);
    check(tp::resolveTiming(GenerativeOperation::Mutate, cfg) == IMM,
          "Inherit resolves to the global Immediate default");
    cfg.setGlobalDefault(BEAT);
    check(tp::resolveTiming(GenerativeOperation::Mutate, cfg) == BEAT,
          "Inherit follows a global-default change to QuantizeBeat");
    cfg.setGlobalDefault(BAR);
    check(tp::resolveTiming(GenerativeOperation::Mutate, cfg) == BAR,
          "Inherit follows a global-default change to QuantizeBar");
    // Only Inherit operations follow: an explicit op stays put.
    check(tp::resolveTiming(GenerativeOperation::ModulationEnable, cfg) == BEAT,
          "explicit QuantizeBeat is unaffected by the global default");
}

// --- Apply-tick computation from the authoritative transport ---------------

void test_compute_immediate() {
    // Immediate means the NEXT SERVICED TICK: the timeline's current logical
    // tick index, matching the Milestone-2 Immediate convention.
    check(tp::computeApplyTick(IMM, timelineAt(0)) == 0,
          "Immediate at tick 0 applies at tick 0");
    check(tp::computeApplyTick(IMM, timelineAt(5)) == 5,
          "Immediate at tick 5 applies at tick 5 (the next serviced tick)");
    check(tp::computeApplyTick(IMM, timelineAt(123)) == 123,
          "Immediate tracks the current logical tick index");
}

void test_compute_quantized_beat() {
    // 4/4 at PPQN 24: beat = 24 ticks, bar = 96 ticks.
    check(tp::computeApplyTick(BEAT, timelineAt(5)) == 24,
          "mid-beat tick 5 -> next beat boundary 24");
    check(tp::computeApplyTick(BEAT, timelineAt(23)) == 24,
          "tick 23 -> next beat boundary 24");
    check(tp::computeApplyTick(BEAT, timelineAt(25)) == 48,
          "tick 25 -> next beat boundary 48");
    check(tp::computeApplyTick(BEAT, timelineAt(100)) == 120,
          "tick 100 -> next beat boundary 120");
}

void test_compute_quantized_bar() {
    check(tp::computeApplyTick(BAR, timelineAt(50)) == 96,
          "mid-bar tick 50 -> next bar boundary 96");
    check(tp::computeApplyTick(BAR, timelineAt(95)) == 96,
          "tick 95 -> next bar boundary 96");
    check(tp::computeApplyTick(BAR, timelineAt(100)) == 192,
          "tick 100 -> next bar boundary 192");
}

// A request EXACTLY on a boundary resolves to the NEXT boundary of that
// kind, never the tick currently being serviced — deterministic and
// consistent with Milestone 2 late-tick reconciliation.
void test_on_boundary_deterministic() {
    // Tick 0 is both a beat and a bar boundary.
    check(tp::computeApplyTick(BEAT, timelineAt(0)) == 24,
          "requested on beat boundary 0 -> NEXT beat 24, not 0");
    check(tp::computeApplyTick(BAR, timelineAt(0)) == 96,
          "requested on bar boundary 0 -> NEXT bar 96, not 0");
    // Tick 24 is a beat boundary mid-bar.
    check(tp::computeApplyTick(BEAT, timelineAt(24)) == 48,
          "requested on beat boundary 24 -> NEXT beat 48, not 24");
    check(tp::computeApplyTick(BAR, timelineAt(24)) == 96,
          "mid-bar tick 24 -> end of this bar 96");
    // Tick 96 is a bar boundary (and a beat boundary).
    check(tp::computeApplyTick(BAR, timelineAt(96)) == 192,
          "requested on bar boundary 96 -> NEXT bar 192, not 96");
    check(tp::computeApplyTick(BEAT, timelineAt(96)) == 120,
          "tick 96 -> next beat boundary 120");
}

// 3/8: beat = 12 ticks, bar = 36 ticks — boundaries derive from the meter.
void test_other_meter() {
    const common::TimeSignature ts38(3, 8);
    const TransportTimeline tl = timelineAt(ts38, 5);
    check(tl.ticksPerBeat() == 12 && tl.ticksPerBar() == 36,
          "3/8 -> beat 12 ticks, bar 36 ticks");
    check(tp::computeApplyTick(BEAT, timelineAt(ts38, 5)) == 12,
          "3/8 tick 5 -> next beat 12");
    check(tp::computeApplyTick(BAR, timelineAt(ts38, 5)) == 36,
          "3/8 tick 5 -> next bar 36");
    check(tp::computeApplyTick(BEAT, timelineAt(ts38, 12)) == 24,
          "3/8 requested on beat 12 -> NEXT beat 24");
    check(tp::computeApplyTick(BAR, timelineAt(ts38, 36)) == 72,
          "3/8 requested on bar 36 -> NEXT bar 72");
}

// Inherit is a CONFIGURATION value, not a resolvable mode: resolveTiming must
// consume it first. computeApplyTick fails SAFE to the immediate tick so a
// mis-wired caller can never stall an operation.
void test_inherit_failsafe() {
    check(tp::computeApplyTick(INH, timelineAt(7)) == 7,
          "raw Inherit fails safe to the immediate tick");
}

// Cross-check against the Milestone-2 single-level policy: every concrete
// mode computes EXACTLY the apply tick of the corresponding TimingPolicy
// value, across both meters, so the two subsystems stay consistent over the
// ONE authoritative transport timeline.
void test_matches_milestone2() {
    uint32_t mismatches = 0;
    for (uint64_t t = 0; t <= 200; ++t) {
        const TransportTimeline tl = timelineAt(t);
        if (tp::computeApplyTick(IMM, tl) != tp::effectiveTick(sequencer::TimingPolicy::Immediate, tl)) ++mismatches;
        if (tp::computeApplyTick(BEAT, tl) != tp::effectiveTick(sequencer::TimingPolicy::NextBeat, tl)) ++mismatches;
        if (tp::computeApplyTick(BAR, tl) != tp::effectiveTick(sequencer::TimingPolicy::NextBar, tl)) ++mismatches;
    }
    const common::TimeSignature ts38(3, 8);
    for (uint64_t t = 0; t <= 100; ++t) {
        const TransportTimeline tl = timelineAt(ts38, t);
        if (tp::computeApplyTick(IMM, tl) != tp::effectiveTick(sequencer::TimingPolicy::Immediate, tl)) ++mismatches;
        if (tp::computeApplyTick(BEAT, tl) != tp::effectiveTick(sequencer::TimingPolicy::NextBeat, tl)) ++mismatches;
        if (tp::computeApplyTick(BAR, tl) != tp::effectiveTick(sequencer::TimingPolicy::NextBar, tl)) ++mismatches;
    }
    check(mismatches == 0,
          "every concrete mode matches the Milestone-2 effectiveTick across 4/4 and 3/8");
}

// Pending-operation due semantics (Milestone 2 isDue): the resolved apply
// tick is consumed with isDue, so a LATE service still applies the operation
// at the first serviced tick at or after its boundary — never skipped, never
// retroactive.
void test_due_and_late_reconciliation() {
    OperationTimingConfig cfg = OperationTimingConfig::factoryDefaults();

    // Factory-default Mutate is QuantizeBeat: requested at tick 5 -> applies at 24.
    const uint64_t apply = tp::computeApplyTick(
        tp::resolveTiming(GenerativeOperation::Mutate, cfg), timelineAt(5));
    check(apply == 24, "factory-default Mutate requested at tick 5 applies at beat 24");
    check(!tp::isDue(apply, 23), "not due before its boundary");
    check(tp::isDue(apply, 24), "due exactly at its boundary");
    check(tp::isDue(apply, 27), "a LATE service (reconciliation) still applies the operation");

    // Per-invocation Immediate: applies at the tick it was requested at.
    const uint64_t now = tp::computeApplyTick(
        tp::resolveTiming(GenerativeOperation::Mutate, cfg, {true, IMM}), timelineAt(5));
    check(now == 5, "override-Immediate Mutate applies at the current tick");
    check(tp::isDue(now, 5), "an immediate operation is due on the next serviced tick");
    check(tp::isDue(now, 7),
          "an immediate operation overtaken by a late service still applies");

    // Override to QuantizeBar requested exactly ON a bar boundary: applies at
    // the NEXT bar, never the tick currently being serviced.
    const uint64_t onBar = tp::computeApplyTick(
        tp::resolveTiming(GenerativeOperation::Keep, cfg, {true, BAR}), timelineAt(96));
    check(onBar == 192, "override-QuantizeBar requested on bar 96 applies at the NEXT bar 192");
    check(!tp::isDue(onBar, 96),
          "never due at the tick being serviced when the request arrived");
}

// Full three-level pipeline: resolve against a config, then compute the apply
// tick from the authoritative transport.
void test_pipeline_resolve_then_compute() {
    OperationTimingConfig cfg = OperationTimingConfig::factoryDefaults();
    cfg.setMode(GenerativeOperation::Mutate, INH);  // follow the global default
    cfg.setGlobalDefault(BAR);                      // global default: next bar

    const TransportTimeline tl = timelineAt(10);
    // No override -> Inherit -> global QuantizeBar -> the next bar boundary.
    check(tp::computeApplyTick(tp::resolveTiming(GenerativeOperation::Mutate, cfg), tl) == 96,
          "Inherit + global QuantizeBar from tick 10 applies at bar 96");
    // Per-invocation override -> Immediate -> the tick itself.
    check(tp::computeApplyTick(
              tp::resolveTiming(GenerativeOperation::Mutate, cfg, {true, IMM}), tl) == 10,
          "override Immediate beats the inherited global QuantizeBar");
}

// Defensive: an out-of-range operation degrades to Inherit (the global
// default) and setMode ignores it — a closed enum plus a bounds-checked table
// can never invent timing or scribble out of bounds.
void test_invalid_operation_is_safe() {
    OperationTimingConfig cfg = OperationTimingConfig::factoryDefaults();
    const GenerativeOperation bad = static_cast<GenerativeOperation>(200);
    check(cfg.modeFor(bad) == INH, "an unconfigured operation inherits the global default");
    cfg.setGlobalDefault(BEAT);
    check(tp::resolveTiming(bad, cfg) == BEAT,
          "an unconfigured operation resolves through the global default");
    cfg.setMode(bad, BAR);  // ignored: out of range
    check(cfg.modeFor(bad) == INH, "setMode ignores an out-of-range operation");
}

}  // namespace

int main() {
    test_factory_defaults();
    test_override_wins();
    test_operation_mode_wins();
    test_global_default_applies();
    test_compute_immediate();
    test_compute_quantized_beat();
    test_compute_quantized_bar();
    test_on_boundary_deterministic();
    test_other_meter();
    test_inherit_failsafe();
    test_matches_milestone2();
    test_due_and_late_reconciliation();
    test_pipeline_resolve_then_compute();
    test_invalid_operation_is_safe();

    if (failures == 0) {
        std::printf("test_timing_policy: all checks passed\n");
        return 0;
    }
    std::printf("test_timing_policy: %d check(s) failed\n", failures);
    return 1;
}
