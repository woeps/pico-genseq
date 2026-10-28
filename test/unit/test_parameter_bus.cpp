// generative-engine-expansion task 16 — Parameter Address and Bus (FR12,
// FR13, AC3).
//
// Validates the parameter addressing that lets modulation target ANY
// UI-settable parameter, including the global pair (BPM and the global
// scale, ui-integration T3: FR12/AC10/AC11):
//   - ParameterAddress: scope (FR13: per-pattern by default, global the
//     option) + pattern slot + a ParamId from the task-16 parameter table;
//     every table entry (the UI-settable scalars — global BPM, the Euclidean
//     gate config, the pitch/velocity set size and playing order — plus the
//     new generator/scale parameters) carries min/max/name/default metadata;
//   - validity: scope/paramId must match (BPM is global-only, the rest are
//     per-pattern), the pattern index is capacity-bounded and unknown ids
//     are rejected;
//   - ParameterBus: the STORED base value is the live sequencer state (the
//     transport timeline for BPM, the per-pattern generative working state
//     for everything else), while modulation overlays live in separate
//     FIXED arrays — setting an overlay changes effectiveValue but never
//     baseValue, the stored state, an undo snapshot or the saved baseline;
//     effective values are always clamped to the parameter range; clearing
//     the overlay restores the base; the real Sequencer exposes the bus
//     bound to its own timeline and working-state manager;
//   - ModulationTarget binds to a ParameterAddress (factory scope
//     per-pattern; the factory address is deliberately unconfigured).
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/ so
// the property-test glob (test/*.cpp) never sweeps it; registered in the host
// test build under the ctest name "test_parameter_bus".

#include "FakeMonotonicClock.h"
#include "ModulationRegistry.h"
#include "ModulationTarget.h"
#include "ParameterAddress.h"
#include "ParameterBus.h"
#include "RecordingMidiOutput.h"
#include "TransportTimeline.h"
#include "WorkingStateManager.h"
#include "command.h"
#include "sequencer.h"

#include <cstdint>
#include <cstdio>
#include <type_traits>

using sequencer::ModulationRegistry;
using sequencer::ModulationScope;
using sequencer::ModulationTarget;
using sequencer::ParameterAddress;
using sequencer::ParameterBus;
using sequencer::ParameterScope;
using sequencer::ParamId;
using sequencer::ParamInfo;
using sequencer::WorkingStateManager;
using sequencer::TransportTimeline;
using sequencer::kParamIdCount;
using sequencer::paramInfoOf;
using sequencer::validAddress;

// Task-16 required test item: static size asserts hold. The new addressing
// POD stays a small fixed value type and the bus stays a fixed-capacity
// store (global constraint: the parameter bus uses fixed-capacity storage).
static_assert(std::is_trivially_copyable<ParameterAddress>::value,
              "ParameterAddress must be trivially copyable");
static_assert(sizeof(ParameterAddress) <= 4,
              "ParameterAddress stays a small fixed POD");
static_assert(std::is_trivially_copyable<ParamInfo>::value,
              "ParamInfo must be trivially copyable");
static_assert(sizeof(ParameterBus) <= 1024,
              "ParameterBus stays within its fixed RAM budget");
static_assert(ParameterBus::kPatternCount == 16,
              "the bus tracks the fixed pattern capacity (16 slots)");
static_assert(sizeof(ModulationTarget) <= 32,
              "the re-bound ModulationTarget stays a small fixed POD");

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

// The canonical address of one table parameter: global parameters under the
// Global scope, everything else per-pattern in slot 0.
ParameterAddress canonicalAddress(uint16_t paramId) {
    const ParamInfo* info = paramInfoOf(paramId);
    ParameterAddress a;
    a.paramId = static_cast<ParamId>(paramId);
    if (info != nullptr) a.scope = info->scope;
    return a;
}

// --- 1. every UI-settable parameter resolves with min/max metadata ---------

void test_parameter_table_metadata() {
    check(kParamIdCount == 19,
          "the table covers BPM + the global scale pair + 10 generator/scale "
          "params + 4 set params");

    for (uint16_t id = 0; id < kParamIdCount; ++id) {
        const ParamInfo* info = paramInfoOf(id);
        check(info != nullptr, "every table id resolves to metadata");
        if (info == nullptr) continue;
        check(info->name != nullptr && info->name[0] != '\0',
              "every parameter has a non-empty display name");
        check(info->minValue <= info->maxValue, "every range is well-formed");
        check(info->defaultValue >= info->minValue &&
                  info->defaultValue <= info->maxValue,
              "every default lies inside its range");
        check(validAddress(canonicalAddress(id)),
              "every table parameter resolves to a valid canonical address");
        // AC3: the canonical address's scope matches the table's scope, and
        // the metadata round-trips through the address.
        const ParameterAddress a = canonicalAddress(id);
        check(a.scope == info->scope, "the canonical address uses the table scope");
    }

    // Unknown ids carry NO metadata and resolve to NO valid address.
    check(paramInfoOf(kParamIdCount) == nullptr, "the sentinel id has no metadata");
    check(paramInfoOf(0xFFFF) == nullptr, "an out-of-table id has no metadata");

    // Spot checks of the required UI-settable parameters (the values the UI
    // itself clamps to): BPM 40-255, Euclidean steps 1-64 / pulses 0-64 /
    // rotation 0-63, pitch/velocity set sizes up to 16, playing orders 0-3,
    // scale root 0-11, the fixed built-in scale template table.
    const ParamInfo* bpm = paramInfoOf(static_cast<uint16_t>(ParamId::GlobalBpm));
    check(bpm != nullptr && bpm->minValue == 40 && bpm->maxValue == 255,
          "global BPM keeps the UI's 40-255 range");
    check(bpm != nullptr && bpm->scope == ParameterScope::Global,
          "global BPM is a GLOBAL-scope parameter");
    const ParamInfo* steps = paramInfoOf(static_cast<uint16_t>(ParamId::GateSteps));
    check(steps != nullptr && steps->minValue == 1 && steps->maxValue == 64,
          "gate steps keep the UI's 1-64 range");
    const ParamInfo* pulses = paramInfoOf(static_cast<uint16_t>(ParamId::GatePulses));
    check(pulses != nullptr && pulses->minValue == 0 && pulses->maxValue == 64,
          "gate pulses keep the UI's 0-64 range");
    const ParamInfo* rotation = paramInfoOf(static_cast<uint16_t>(ParamId::GateRotation));
    check(rotation != nullptr && rotation->minValue == 0 && rotation->maxValue == 63,
          "gate rotation keeps the UI's 0-63 range");
    const ParamInfo* root = paramInfoOf(static_cast<uint16_t>(ParamId::ScaleRoot));
    check(root != nullptr && root->minValue == 0 && root->maxValue == 11,
          "the scale root spans the 12 pitch classes");
    const ParamInfo* tmpl = paramInfoOf(static_cast<uint16_t>(ParamId::ScaleTemplate));
    check(tmpl != nullptr && tmpl->minValue == 0 &&
              tmpl->maxValue == static_cast<int16_t>(common::scaleTemplateCount() - 1),
          "the scale template index spans the fixed built-in template table");
    const ParamInfo* pcount = paramInfoOf(static_cast<uint16_t>(ParamId::PitchSetCount));
    check(pcount != nullptr && pcount->maxValue == 16,
          "the pitch set size spans the fixed 16-value set capacity");
}

// --- 2. address validity: scope match, pattern capacity, unknown ids -------

void test_address_validity() {
    // FR12: global BPM is addressable with GLOBAL scope.
    ParameterAddress bpm;
    bpm.scope = ParameterScope::Global;
    bpm.paramId = ParamId::GlobalBpm;
    check(validAddress(bpm), "global BPM resolves under the global scope");

    // FR13: every per-pattern parameter resolves under the PATTERN scope,
    // in every pattern slot, and only there (the global scale pair joins
    // BPM in the Global scope — see test_global_scale_parameters).
    for (uint16_t id = 1; id < kParamIdCount; ++id) {
        const ParamInfo* info = paramInfoOf(id);
        if (info != nullptr && info->scope == ParameterScope::Global) continue;
        ParameterAddress a = canonicalAddress(id);
        check(validAddress(a), "every per-pattern parameter resolves in slot 0");
        a.patternIndex = ParameterBus::kPatternCount - 1;
        check(validAddress(a), "every per-pattern parameter resolves in the last slot");
        a.scope = ParameterScope::Global;
        check(!validAddress(a), "a per-pattern parameter is rejected under global scope");
    }

    // Scope mismatches both ways.
    ParameterAddress mismatched;
    mismatched.scope = ParameterScope::Pattern;
    mismatched.paramId = ParamId::GlobalBpm;
    check(!validAddress(mismatched),
          "the global-only BPM is rejected under the per-pattern scope");
    mismatched.paramId = ParamId::GlobalScaleRoot;
    check(!validAddress(mismatched),
          "the global-only scale root is rejected under the per-pattern scope");
    mismatched.paramId = ParamId::GlobalScaleTemplate;
    check(!validAddress(mismatched),
          "the global-only scale template is rejected under the per-pattern scope");

    // The pattern index is bounded by the fixed pattern capacity.
    ParameterAddress oob;
    oob.paramId = ParamId::GateSteps;
    oob.patternIndex = ParameterBus::kPatternCount;
    check(!validAddress(oob), "a pattern index beyond the capacity is rejected");

    // Unknown parameter ids are rejected under both scopes.
    ParameterAddress unknownPattern;
    unknownPattern.paramId = static_cast<ParamId>(kParamIdCount);
    check(!validAddress(unknownPattern), "an unknown param id is rejected");
    ParameterAddress unknownGlobal;
    unknownGlobal.scope = ParameterScope::Global;
    unknownGlobal.paramId = static_cast<ParamId>(0xFF);
    check(!validAddress(unknownGlobal), "an unknown global param id is rejected");

    // Unknown scope values are rejected.
    ParameterAddress badScope;
    badScope.scope = static_cast<ParameterScope>(2);
    check(!validAddress(badScope), "an unknown scope value is rejected");
}

// --- 3. the default scope is per-pattern (FR13) ------------------------------

void test_default_scope_is_per_pattern() {
    const ParameterAddress factory{};
    check(factory.scope == ParameterScope::Pattern,
          "a factory ParameterAddress is per-pattern scoped");
    check(factory.patternIndex == 0, "a factory ParameterAddress targets slot 0");

    // ModulationTarget keeps its per-pattern factory scope (FR13) and its
    // address field IS a task-16 ParameterAddress.
    ModulationTarget target;
    check(target.address.scope == ModulationScope::Pattern,
          "a factory ModulationTarget is per-pattern scoped");
    check(target.address.patternIndex == 0, "a factory ModulationTarget targets slot 0");

    ModulationRegistry registry;
    const size_t slot = registry.addTarget();
    check(registry.target(slot).address.scope == ModulationScope::Pattern,
          "a fresh registry slot defaults to the per-pattern scope");
}

// --- 4/5/6. base values, overlays, clamping, clearing ------------------------

void test_overlay_changes_effective_not_base() {
    TransportTimeline timeline;
    WorkingStateManager working;
    ParameterBus bus;
    check(bus.bind(&timeline, &working), "the bus binds to live sequencer state");
    check(bus.bound(), "a bound bus reports bound");

    // Global BPM: the base value IS the timeline's stored BPM (FR12, AC3) —
    // and setBase writes through to the live state.
    ParameterAddress bpm;
    bpm.scope = ParameterScope::Global;
    bpm.paramId = ParamId::GlobalBpm;
    check(bus.baseValue(bpm) == 120, "the BPM base reads the timeline's BPM");
    check(bus.setBase(bpm, 96), "setBase accepts the BPM address");
    check(timeline.bpm() == 96, "setBase writes through to the timeline (backed state)");
    check(bus.baseValue(bpm) == 96, "the BPM base follows the timeline");

    // Per-pattern: the base value IS the working state's stored configuration.
    ParameterAddress steps;
    steps.scope = ParameterScope::Pattern;
    steps.patternIndex = 0;
    steps.paramId = ParamId::GateSteps;
    check(bus.setBase(steps, 16), "setBase accepts a per-pattern address");
    check(working.working(0)->generatorParam(sequencer::GeneratorRole::GATE, 0) == 16,
          "setBase writes through to the generative working state");

    // Setting an overlay changes effectiveValue but NOT baseValue — and never
    // the stored state the base is backed by.
    check(bus.setOverlay(steps, 40), "setOverlay accepts a valid address");
    check(bus.hasOverlay(steps), "an active overlay is reported");
    check(bus.effectiveValue(steps) == 40, "the overlay drives the effective value");
    check(bus.baseValue(steps) == 16, "the overlay leaves baseValue untouched");
    check(working.working(0)->generatorParam(sequencer::GeneratorRole::GATE, 0) == 16,
          "the overlay never modifies the stored working state");

    // The same guarantee for global BPM: the overlay masks the effective
    // value while the stored BPM (what undo/save/the timeline hold) is safe.
    check(bus.setOverlay(bpm, 60), "setOverlay accepts the global BPM address");
    check(bus.effectiveValue(bpm) == 60, "the BPM overlay drives the effective BPM");
    check(timeline.bpm() == 96, "the BPM overlay never modifies the stored BPM");
    check(bus.baseValue(bpm) == 96, "the BPM overlay leaves baseValue untouched");

    // Clearing the overlay restores the base (and the stored state never
    // moved, so nothing needs restoring there).
    check(bus.clearOverlay(steps), "clearOverlay accepts a valid address");
    check(!bus.hasOverlay(steps), "a cleared overlay is inactive");
    check(bus.effectiveValue(steps) == 16, "clearing the overlay restores the base");
    check(bus.clearOverlay(bpm), "clearOverlay accepts the global BPM address");
    check(bus.effectiveValue(bpm) == 96, "clearing the BPM overlay restores the base");

    // Clearing an inactive overlay is a safe no-op.
    check(bus.clearOverlay(steps), "clearOverlay on an inactive overlay is a no-op");
    check(bus.effectiveValue(steps) == 16, "a no-op clear leaves the effective value");

    // setBase while an overlay is active changes the base (the user's edit)
    // but the overlay keeps masking until cleared.
    check(bus.setOverlay(steps, 32), "re-arm the overlay");
    check(bus.setBase(steps, 20), "the user edits the base under an active overlay");
    check(bus.baseValue(steps) == 20, "the edited base reads back");
    check(bus.effectiveValue(steps) == 32, "the overlay still masks the edit");
    check(bus.clearOverlay(steps), "clear the overlay again");
    check(bus.effectiveValue(steps) == 20, "clearing reveals the edited base");
}

void test_effective_value_clamped() {
    TransportTimeline timeline;
    WorkingStateManager working;
    ParameterBus bus;
    bus.bind(&timeline, &working);

    ParameterAddress steps;
    steps.paramId = ParamId::GateSteps;  // Pattern scope, slot 0, range 1-64
    check(bus.setOverlay(steps, 999), "an out-of-range overlay is accepted (and clamped)");
    check(bus.effectiveValue(steps) == 64, "the effective value clamps to the range max");
    check(bus.setOverlay(steps, -50), "an under-range overlay is accepted (and clamped)");
    check(bus.effectiveValue(steps) == 1, "the effective value clamps to the range min");
    bus.clearOverlay(steps);  // drop the overlay: the next checks read the BASE

    // Read-time clamping is total: even a base that someone wrote into the
    // state OUTSIDE the parameter's range reads back clamped (the stored
    // byte is not trusted).
    working.working(0)->setGeneratorParam(sequencer::GeneratorRole::GATE, 0, 999);
    check(bus.baseValue(steps) == 999 || bus.baseValue(steps) == 64,
          "baseValue reads the stored byte (clamping happens on effectiveValue)");
    check(bus.effectiveValue(steps) == 64,
          "an out-of-range stored base is clamped on the effective read");
    working.working(0)->setGeneratorParam(sequencer::GeneratorRole::GATE, 0, 0);
    check(bus.effectiveValue(steps) == 1, "a zero stored base clamps to the range min");

    // setBase clamps into the parameter range before it stores.
    ParameterAddress bpm;
    bpm.scope = ParameterScope::Global;
    bpm.paramId = ParamId::GlobalBpm;
    check(bus.setBase(bpm, 999), "setBase accepts an out-of-range BPM");
    check(timeline.bpm() == 255, "setBase clamps the BPM to its UI range max");
    check(bus.setBase(bpm, 1), "setBase accepts an under-range BPM");
    check(timeline.bpm() == 40, "setBase clamps the BPM to its UI range min");
}

void test_overlay_isolation_and_clear_all() {
    TransportTimeline timeline;
    WorkingStateManager working;
    ParameterBus bus;
    bus.bind(&timeline, &working);

    // Overlays are per (scope, pattern, parameter): one pattern's overlay
    // never leaks into another slot or the global scope.
    ParameterAddress p2;
    p2.patternIndex = 2;
    p2.paramId = ParamId::GateSteps;
    ParameterAddress p3;
    p3.patternIndex = 3;
    p3.paramId = ParamId::GateSteps;
    ParameterAddress globalBpm;
    globalBpm.scope = ParameterScope::Global;
    globalBpm.paramId = ParamId::GlobalBpm;

    check(bus.setBase(p2, 24) && bus.setBase(p3, 24), "arm two pattern bases");
    check(bus.setOverlay(p2, 48), "overlay pattern 2");
    check(bus.effectiveValue(p2) == 48, "pattern 2 sees its overlay");
    check(bus.effectiveValue(p3) == 24, "pattern 3 is untouched by pattern 2's overlay");
    check(!bus.hasOverlay(p3), "pattern 3 has no overlay");
    check(bus.effectiveValue(globalBpm) == 120, "the global scope is untouched");

    // A different parameter in the SAME pattern is independent too.
    ParameterAddress p2Rotation;
    p2Rotation.patternIndex = 2;
    p2Rotation.paramId = ParamId::GateRotation;
    check(bus.effectiveValue(p2Rotation) == 0, "another parameter of pattern 2 is untouched");

    // clearAllOverlays drops every overlay at once (bounded fixed arrays).
    check(bus.setOverlay(p3, 50) && bus.setOverlay(globalBpm, 100),
          "arm two more overlays");
    bus.clearAllOverlays();
    check(!bus.hasOverlay(p2) && !bus.hasOverlay(p3) && !bus.hasOverlay(globalBpm),
          "clearAllOverlays drops every overlay");
    check(bus.effectiveValue(p2) == 24 && bus.effectiveValue(p3) == 24,
          "the pattern bases survive clearAllOverlays");
    check(bus.effectiveValue(globalBpm) == 120, "the global base survives clearAllOverlays");
}

// --- 7. a ModulationTarget binds to a ParameterAddress ----------------------

void test_modulation_target_binds_address() {
    ModulationTarget target;

    // A per-pattern binding round-trips through the target.
    ParameterAddress patternAddress;
    patternAddress.scope = ParameterScope::Pattern;
    patternAddress.patternIndex = 3;
    patternAddress.paramId = ParamId::GatePulses;
    target.address = patternAddress;
    check(target.address.scope == ParameterScope::Pattern,
          "a bound target keeps the per-pattern scope");
    check(target.address.patternIndex == 3, "a bound target keeps the pattern slot");
    check(target.address.paramId == ParamId::GatePulses, "a bound target keeps the param id");
    check(validAddress(target.address), "a bound target's address is valid");

    // A global binding (FR12/FR13: global BPM as the option) round-trips.
    ParameterAddress globalAddress;
    globalAddress.scope = ParameterScope::Global;
    globalAddress.paramId = ParamId::GlobalBpm;
    target.address = globalAddress;
    check(validAddress(target.address), "a globally bound target's address is valid");
    check(target.address.scope == ModulationScope::Global,
          "the legacy ModulationScope alias still names the scope");

    // The factory target's address is deliberately UNCONFIGURED (per-pattern
    // scope holding the global-only BPM id): an unbound target modulates
    // nothing until the performer binds it (default behavior stays stable).
    const ModulationTarget factory{};
    check(!validAddress(factory.address),
          "a factory target's address is invalid until configured");

    // Equality: addresses compare by (scope, pattern, param).
    check(patternAddress == patternAddress, "address equality is reflexive");
    check(!(patternAddress == globalAddress), "distinct addresses compare unequal");
    check(patternAddress != globalAddress, "the inequality operator mirrors equality");
}

// --- 8. invalid addresses (and an unbound bus) are rejected ------------------

void test_invalid_addresses_rejected() {
    TransportTimeline timeline;
    WorkingStateManager working;
    ParameterBus bus;
    bus.bind(&timeline, &working);

    ParameterAddress invalid;
    invalid.scope = ParameterScope::Pattern;
    invalid.patternIndex = 0;
    invalid.paramId = ParamId::GlobalBpm;  // global-only under Pattern scope

    check(!bus.setBase(invalid, 10), "setBase rejects an invalid address");
    check(!bus.setOverlay(invalid, 10), "setOverlay rejects an invalid address");
    check(!bus.clearOverlay(invalid), "clearOverlay rejects an invalid address");
    check(!bus.hasOverlay(invalid), "hasOverlay rejects an invalid address");
    check(bus.baseValue(invalid) == 0, "baseValue degrades to 0 on an invalid address");
    check(bus.effectiveValue(invalid) == 0, "effectiveValue degrades to 0 on an invalid address");
    check(bus.paramInfo(invalid) == nullptr, "paramInfo rejects an invalid address");

    // Every other invalid flavor is rejected the same way.
    ParameterAddress oob;
    oob.paramId = ParamId::GateSteps;
    oob.patternIndex = ParameterBus::kPatternCount;
    check(!bus.setOverlay(oob, 10), "an out-of-capacity pattern index is rejected");
    check(!bus.setBase(oob, 10), "setBase rejects the out-of-capacity index too");
    ParameterAddress globalMismatch;
    globalMismatch.scope = ParameterScope::Global;
    globalMismatch.paramId = ParamId::GateSteps;
    check(!bus.setOverlay(globalMismatch, 10),
          "a per-pattern parameter is rejected under global scope");
    ParameterAddress unknown;
    unknown.paramId = static_cast<ParamId>(kParamIdCount);
    check(!bus.setOverlay(unknown, 10), "an unknown param id is rejected");
    ParameterAddress badScope;
    badScope.scope = static_cast<ParameterScope>(7);
    check(!bus.setOverlay(badScope, 10), "an unknown scope value is rejected");

    // Rejected writes leave the bus and the state untouched. (The default
    // stored base of GateSteps is 0 — outside its 1-64 range — so the
    // EFFECTIVE value reads back clamped to the range minimum, 1.)
    check(bus.effectiveValue(ParameterAddress{ParameterScope::Pattern, 0,
                                              ParamId::GateSteps}) == 1,
          "a rejected overlay leaves the effective value at its base");

    // An UNBOUND bus degrades safely on every operation.
    ParameterBus unbound;
    check(!unbound.bound(), "a fresh bus starts unbound");
    ParameterAddress bpm;
    bpm.scope = ParameterScope::Global;
    bpm.paramId = ParamId::GlobalBpm;
    check(unbound.setOverlay(bpm, 100), "overlays are bus-local (no backing needed)");
    check(unbound.hasOverlay(bpm), "an unbound bus still tracks its own overlays");
    check(unbound.effectiveValue(bpm) == 100,
          "an absolute overlay is effective even before binding");
    check(unbound.baseValue(bpm) == 0, "an unbound bus has no base to read");
    check(!unbound.setBase(bpm, 100), "an unbound bus rejects base writes");
    check(!unbound.bind(nullptr, &working), "bind rejects a null timeline");
    check(!unbound.bind(&timeline, nullptr), "bind rejects a null working-state manager");
    check(unbound.bind(&timeline, &working), "a full bind succeeds");
    check(unbound.bound(), "the bound bus reports bound");
    check(unbound.baseValue(bpm) == 120, "the late-bound bus reads the live base");
}

// --- 9. the global scale pair (ui-integration T3: FR12/AC10/AC11) -----------

void test_global_scale_parameters() {
    // Table metadata: Global scope, the same bounds the UI enforces.
    const ParamInfo* root =
        paramInfoOf(static_cast<uint16_t>(ParamId::GlobalScaleRoot));
    const ParamInfo* tmpl =
        paramInfoOf(static_cast<uint16_t>(ParamId::GlobalScaleTemplate));
    check(root != nullptr && root->scope == ParameterScope::Global,
          "the global scale root is a GLOBAL-scope parameter");
    check(root != nullptr && root->minValue == 0 && root->maxValue == 11,
          "the global scale root spans the 12 pitch classes");
    check(root != nullptr && root->defaultValue == 0,
          "the global scale root defaults to C");
    check(tmpl != nullptr && tmpl->scope == ParameterScope::Global,
          "the global scale template is a GLOBAL-scope parameter");
    check(tmpl != nullptr && tmpl->minValue == 0 &&
              tmpl->maxValue ==
                  static_cast<int16_t>(common::scaleTemplateCount() - 1),
          "the global scale template spans the fixed built-in template table");

    // Validity: both halves resolve under the Global scope (the pattern
    // index is ignored there).
    ParameterAddress rootAddress;
    rootAddress.scope = ParameterScope::Global;
    rootAddress.patternIndex = 15;  // ignored by the Global scope
    rootAddress.paramId = ParamId::GlobalScaleRoot;
    ParameterAddress tmplAddress;
    tmplAddress.scope = ParameterScope::Global;
    tmplAddress.paramId = ParamId::GlobalScaleTemplate;
    check(validAddress(rootAddress),
          "the global scale root resolves under the global scope");
    check(validAddress(tmplAddress),
          "the global scale template resolves under the global scope");

    // readBase routes to the TransportTimeline: the timeline IS the single
    // source of the global scale.
    TransportTimeline timeline;
    WorkingStateManager working;
    ParameterBus bus;
    check(bus.bind(&timeline, &working), "the bus binds to live sequencer state");
    check(bus.baseValue(rootAddress) == 0,
          "the scale root base reads the timeline");
    check(bus.baseValue(tmplAddress) == 0,
          "the scale template base reads the timeline");
    check(timeline.setScale(5, 3), "seed the timeline's global scale");
    check(bus.baseValue(rootAddress) == 5, "the root base follows the timeline");
    check(bus.baseValue(tmplAddress) == 3,
          "the template base follows the timeline");

    // writeBase routes to the TransportTimeline: setBase writes through to
    // the timeline's single source, preserving the other half of the pair.
    check(bus.setBase(rootAddress, 7), "setBase accepts the global root address");
    check(timeline.scaleRoot() == 7,
          "setBase writes the root through to the timeline");
    check(timeline.scaleTemplateIndex() == 3, "a root write preserves the template");
    check(bus.setBase(tmplAddress, 2),
          "setBase accepts the global template address");
    check(timeline.scaleTemplateIndex() == 2,
          "setBase writes the template through to the timeline");
    check(timeline.scaleRoot() == 7, "a template write preserves the root");
    check(bus.baseValue(rootAddress) == 7, "the written root base reads back");
    check(bus.baseValue(tmplAddress) == 2,
          "the written template base reads back");

    // setBase clamps into the UI range before it stores.
    check(bus.setBase(rootAddress, 99), "setBase accepts an out-of-range root");
    check(timeline.scaleRoot() == 11,
          "setBase clamps the root to its range max");
    check(bus.setBase(rootAddress, -9), "setBase accepts an under-range root");
    check(timeline.scaleRoot() == 0,
          "setBase clamps the root to its range min");
    check(bus.setBase(tmplAddress, 999),
          "setBase accepts an out-of-range template");
    check(timeline.scaleTemplateIndex() ==
              static_cast<uint8_t>(common::scaleTemplateCount() - 1),
          "setBase clamps the template to its range max");

    // Overlays: the modulation write path works on the global scale with
    // Global scope (AC11) and never moves the stored scale.
    check(bus.setOverlay(rootAddress, 9), "setOverlay accepts the global root");
    check(bus.effectiveValue(rootAddress) == 9,
          "the overlay drives the effective root");
    check(timeline.scaleRoot() == 0,
          "the overlay never modifies the stored root");
    check(bus.baseValue(rootAddress) == 0,
          "the overlay leaves baseValue untouched");
    check(bus.clearOverlay(rootAddress), "clearOverlay accepts the global root");
    check(bus.effectiveValue(rootAddress) == 0,
          "clearing restores the scale root base");
    check(bus.setOverlay(tmplAddress, 4),
          "setOverlay accepts the global template");
    check(bus.effectiveValue(tmplAddress) == 4,
          "the overlay drives the effective template");
    check(bus.clearOverlay(tmplAddress),
          "clearOverlay accepts the global template");

    // The sequencer's OWN bus addresses its OWN timeline (the single
    // source every pattern reads the global scale from).
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
    sequencer::GeneratorRegistry registry;
    sequencer::Sequencer seq{out, clock, registry};
    ParameterBus& seqBus = seq.parameterBus();
    check(seqBus.setBase(rootAddress, 6),
          "the sequencer's bus accepts the global root");
    check(seqBus.setBase(tmplAddress, 4),
          "the sequencer's bus accepts the global template");
    check(seqBus.baseValue(rootAddress) == 6 &&
              seqBus.baseValue(tmplAddress) == 4,
          "the sequencer's own timeline backs the global scale pair");
}

// --- 10. the real Sequencer exposes the bus over its own live state ----------

void test_sequencer_exposes_bound_bus() {
    FakeMonotonicClock clock{1000};
    RecordingMidiOutput out;
    sequencer::GeneratorRegistry registry;
    sequencer::Sequencer seq{out, clock, registry};

    ParameterBus& bus = seq.parameterBus();
    check(bus.bound(), "the exposed bus is bound to the sequencer's state");

    // The base of the global BPM is the sequencer's OWN timeline: a BPM_SET
    // command (the existing UI path) is visible through the bus with no
    // synchronization step, and a bus base write reaches the timeline.
    ParameterAddress bpm;
    bpm.scope = ParameterScope::Global;
    bpm.paramId = ParamId::GlobalBpm;
    check(bus.baseValue(bpm) == 120, "the sequencer's bus reads the factory BPM");
    commands::CommandMessage msg;
    msg.cmd = commands::Command::BPM_SET;
    msg.param1 = 150;
    seq.processCommand(msg);
    check(bus.baseValue(bpm) == 150,
          "the existing BPM command path is visible through the bus (live backing)");
    check(bus.setBase(bpm, 110), "the bus writes the sequencer's BPM base");
    check(bus.baseValue(bpm) == 110, "the written BPM base reads back");

    // Per-pattern bases are the working state the generative engine mutates:
    // a mutation-style state edit is visible through the bus and a bus base
    // write reaches the stored state (what undo/baseline/Keep snapshot).
    ParameterAddress steps;
    steps.paramId = ParamId::GateSteps;
    seq.workingStateManager().working(0)->setGeneratorParam(
        sequencer::GeneratorRole::GATE, 0, 24);
    check(bus.baseValue(steps) == 24,
          "a working-state edit is visible through the bus (live backing)");
    // The scale is the ONE global (ui-integration T16): a Pattern-scope
    // scale address reads/writes the single global source, never a
    // per-pattern field — the same timeline the Global-scope addresses
    // read (FR13: every pattern reads the scale from the single source).
    ParameterAddress root;
    root.paramId = ParamId::ScaleRoot;
    check(bus.setBase(root, 7), "the bus writes a pattern-scope scale base");
    check(bus.baseValue(root) == 7, "the scale base reads back");
    ParameterAddress globalRoot;
    globalRoot.scope = ParameterScope::Global;
    globalRoot.paramId = ParamId::GlobalScaleRoot;
    check(bus.baseValue(globalRoot) == 7,
          "a pattern-scope scale write reaches the ONE global source");

    // Modulation overlays never touch that stored state.
    check(bus.setOverlay(steps, 40), "arm an overlay through the sequencer's bus");
    check(bus.effectiveValue(steps) == 40, "the overlay drives the effective value");
    check(seq.workingStateManager().working(0)->generatorParam(
              sequencer::GeneratorRole::GATE, 0) == 24,
          "the overlay leaves the stored working state untouched");
    check(bus.baseValue(steps) == 24, "the overlay leaves the base value untouched");
}

}  // namespace

int main() {
    test_parameter_table_metadata();
    test_address_validity();
    test_default_scope_is_per_pattern();
    test_overlay_changes_effective_not_base();
    test_effective_value_clamped();
    test_overlay_isolation_and_clear_all();
    test_modulation_target_binds_address();
    test_invalid_addresses_rejected();
    test_global_scale_parameters();
    test_sequencer_exposes_bound_bus();

    if (failures == 0) {
        std::printf("test_parameter_bus: all checks passed\n");
        return 0;
    }
    std::printf("test_parameter_bus: %d check(s) failed\n", failures);
    return 1;
}
