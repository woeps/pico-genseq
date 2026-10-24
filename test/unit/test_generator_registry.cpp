// generative-engine-expansion task 2 — generator plugin interface (FR1,
// AC1, AC8).
//
// Validates the plugin architecture pieces with test-only fake generators
// (the concrete built-ins are registered by RegisterGenerators.cpp; T4
// adds the Euclidean gate generator, T5-T7 the rest):
//
//   1. registration succeeds and reports the registered count;
//   2. lookup by id returns the generator, unknown ids miss;
//   3. registering a duplicate id fails without changing the registry;
//   4. registration fails once the fixed capacity is reached;
//   5. capabilities().has(GATE)/has(PITCH) report the declared set;
//   6. iterating paramDefs() yields the declared count, ranges and
//      defaults;
//   7. dispatch through the IGenerator base pointer drives generate() with
//      the context/output plumbing and the deterministic seedable RNG;
//   8. registerGenerators() (RegisterGenerators.cpp) is the only built-in
//      listing; since T4 it registers the Euclidean gate generator.
//
// Standalone unit test (own main(), no RapidCheck). It lives in test/unit/
// so the property-test glob (test/*.cpp) never sweeps it; registered in the
// host test build under the ctest name "test_generator_registry".

#include "GeneratorCapabilities.h"
#include "GeneratorRegistry.h"
#include "IGenerator.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int failures = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("FAIL: %s\n", what); ++failures; }
}

using sequencer::GeneratorCapabilities;
using sequencer::GeneratorCapability;
using sequencer::GeneratorContext;
using sequencer::GeneratorOutput;
using sequencer::GeneratorParamHint;
using sequencer::GeneratorParamSet;
using sequencer::GeneratorRng;

// --- test-only fake generators ---------------------------------------------

// The full-featured fake's parameter table: declared at namespace scope so
// the constexpr evaluation happens after the GeneratorParamSet type is
// complete (a static member initializer inside the class body would not be).
constexpr GeneratorParamSet makeFakeGatePitchParams() {
    GeneratorParamSet set;
    set.defs[0] = {0, "Density", 0, 127, 96, GeneratorParamHint::Percent};
    set.defs[1] = {1, "Offset", -64, 64, 0, GeneratorParamHint::None};
    set.defs[2] = {2, "Root Note", 0, 127, 60, GeneratorParamHint::Note};
    set.count = 3;
    return set;
}

constexpr GeneratorParamSet kFakeGatePitchParams = makeFakeGatePitchParams();
constexpr GeneratorParamSet kNoParams{};

// The full-featured fake: declares GATE+PITCH and three parameter
// definitions. generate() records what the context carried so the dispatch
// test can assert the plumbing (recorded mutably: generate() is const —
// generators are pure functions of config + inputs).
class FakeGatePitchGenerator final : public sequencer::IGenerator {
public:
    const char* id() const override { return "fake.gatepitch"; }
    const char* name() const override { return "Fake Gate+Pitch"; }

    GeneratorCapabilities capabilities() const override {
        return GeneratorCapability::GATE | GeneratorCapability::PITCH;
    }

    const GeneratorParamSet& paramDefs() const override { return kFakeGatePitchParams; }

    void generate(uint32_t stepIndex, const GeneratorContext& context,
                  GeneratorOutput& output) const override {
        output = GeneratorOutput{};
        output.hasGate = true;
        output.gate = (stepIndex % 2u) == 0u;
        output.hasPitch = true;
        output.pitch = static_cast<uint8_t>(context.patternLength + stepIndex);
        lastStepIndex = stepIndex;
        lastPatternLength = context.patternLength;
        lastScale = context.scale;
        lastRandomByte = context.rng.nextByte();
    }

    mutable uint32_t lastStepIndex = 0;
    mutable uint8_t lastPatternLength = 0;
    mutable const common::Scale* lastScale = nullptr;
    mutable uint8_t lastRandomByte = 0;
};

// A minimal configurable fake used to exercise other capability bits and to
// fill the registry to its fixed capacity.
class StubGenerator final : public sequencer::IGenerator {
public:
    StubGenerator(const char* anId, GeneratorCapabilities caps)
        : id_(anId), caps_(caps) {}

    const char* id() const override { return id_; }
    const char* name() const override { return id_; }
    GeneratorCapabilities capabilities() const override { return caps_; }
    const GeneratorParamSet& paramDefs() const override { return kNoParams; }
    void generate(uint32_t, const GeneratorContext&, GeneratorOutput&) const override {}

private:
    const char* id_;
    GeneratorCapabilities caps_;
};

// --- 1-3: registration, lookup, duplicates, null ----------------------------

void test_registration_and_lookup() {
    sequencer::GeneratorRegistry registry;
    FakeGatePitchGenerator fake;
    FakeGatePitchGenerator duplicate;
    StubGenerator second("stub.second", GeneratorCapability::VELOCITY);

    check(registry.count() == 0, "a fresh registry is empty");
    check(registry.at(0) == nullptr, "iteration slot 0 of an empty registry is null");
    check(registry.find("fake.gatepitch") == nullptr, "a fresh registry misses lookups");

    check(registry.add(&fake), "registration succeeds");
    check(registry.count() == 1, "count reports the registered generator");
    check(registry.find("fake.gatepitch") == &fake, "lookup by id returns the generator");
    check(registry.find("missing") == nullptr, "an unknown id misses");
    check(registry.find(nullptr) == nullptr, "a null id misses");

    check(!registry.add(nullptr), "registering a null generator fails");
    check(registry.count() == 1, "a failed null registration leaves the count unchanged");
    check(!registry.add(&duplicate), "registering a duplicate id fails");
    check(registry.count() == 1, "a failed duplicate registration leaves the count unchanged");
    check(registry.find("fake.gatepitch") == &fake,
          "a failed duplicate registration does not replace the original");

    check(registry.add(&second), "a second distinct registration succeeds");
    check(registry.count() == 2, "count reports both registrations");
    check(registry.at(0) == &fake, "iteration follows registration order (first)");
    check(registry.at(1) == &second, "iteration follows registration order (second)");
    check(registry.at(2) == nullptr, "iteration ends at the count");

    registry.clear();
    check(registry.count() == 0, "clear empties the registry");
    check(registry.find("fake.gatepitch") == nullptr, "a cleared registry misses lookups");
    check(registry.at(0) == nullptr, "a cleared registry iterates nothing");
}

// --- 4: the fixed capacity ----------------------------------------------------

void test_capacity() {
    sequencer::GeneratorRegistry registry;
    StubGenerator stubs[sequencer::kMaxGenerators] = {
        StubGenerator("stub.0", GeneratorCapability::GATE),
        StubGenerator("stub.1", GeneratorCapability::PITCH),
        StubGenerator("stub.2", GeneratorCapability::VELOCITY),
        StubGenerator("stub.3", GeneratorCapability::DURATION),
        StubGenerator("stub.4", GeneratorCapability::GATE),
        StubGenerator("stub.5", GeneratorCapability::GATE),
        StubGenerator("stub.6", GeneratorCapability::GATE),
        StubGenerator("stub.7", GeneratorCapability::GATE),
    };
    static_assert(sequencer::kMaxGenerators >= 4,
                  "the registry must hold at least the four built-in generators");

    for (uint8_t i = 0; i < sequencer::kMaxGenerators; ++i) {
        check(registry.add(&stubs[i]), "filling to capacity succeeds");
        check(registry.count() == i + 1, "count follows each registration");
    }
    check(registry.count() == sequencer::kMaxGenerators,
          "the registry holds exactly its fixed capacity");

    StubGenerator overflow("stub.overflow", GeneratorCapability::GATE);
    check(!registry.add(&overflow), "registration fails once the capacity is reached");
    check(registry.count() == sequencer::kMaxGenerators,
          "a failed overflow registration leaves the count unchanged");
    check(registry.find("stub.overflow") == nullptr,
          "the overflow generator is not reachable");
    check(registry.find("stub.3") == &stubs[3],
          "existing registrations survive the overflow attempt");
}

// --- 5: capability bits (FR1, AC8) --------------------------------------------

void test_capabilities() {
    const FakeGatePitchGenerator fake;
    const StubGenerator velocityDuration("stub.vel", GeneratorCapability::VELOCITY |
                                                         GeneratorCapability::DURATION);
    const StubGenerator empty("stub.none", GeneratorCapabilities{});

    const GeneratorCapabilities caps = fake.capabilities();
    check(caps.has(GeneratorCapability::GATE), "the fake reports its GATE capability");
    check(caps.has(GeneratorCapability::PITCH), "the fake reports its PITCH capability");
    check(!caps.has(GeneratorCapability::VELOCITY), "the fake reports no VELOCITY capability");
    check(!caps.has(GeneratorCapability::DURATION), "the fake reports no DURATION capability");

    const GeneratorCapabilities vd = velocityDuration.capabilities();
    check(vd.has(GeneratorCapability::VELOCITY), "the stub reports its VELOCITY capability");
    check(vd.has(GeneratorCapability::DURATION), "the stub reports its DURATION capability");
    check(!vd.has(GeneratorCapability::GATE), "the stub reports no GATE capability");
    check(!vd.has(GeneratorCapability::PITCH), "the stub reports no PITCH capability");

    check(empty.capabilities().none(), "an empty capability set reports none");
    check(!empty.capabilities().has(GeneratorCapability::GATE),
          "an empty capability set has no GATE");

    // The four flags are pairwise-distinct single bits.
    const uint8_t allBits = static_cast<uint8_t>(GeneratorCapability::GATE) |
                            static_cast<uint8_t>(GeneratorCapability::PITCH) |
                            static_cast<uint8_t>(GeneratorCapability::VELOCITY) |
                            static_cast<uint8_t>(GeneratorCapability::DURATION);
    check(allBits == 0x0F, "the four capability flags are the low nibble");
}

// --- 6: parameter definitions (FR1, AC8) ---------------------------------------

void test_param_defs() {
    const FakeGatePitchGenerator fake;
    const StubGenerator stub("stub.noparams", GeneratorCapability::GATE);
    static_assert(sequencer::kMaxGeneratorParams >= 3,
                  "a generator must be able to declare at least three parameters");

    const GeneratorParamSet& defs = fake.paramDefs();
    check(defs.count == 3, "paramDefs declares exactly three parameters");
    check(defs.count <= sequencer::kMaxGeneratorParams, "count stays within the fixed capacity");

    check(defs.defs[0].id == 0 && defs.defs[1].id == 1 && defs.defs[2].id == 2,
          "parameter ids are as declared");
    for (uint8_t i = 0; i < defs.count; ++i) {
        check(defs.defs[i].name != nullptr && defs.defs[i].name[0] != '\0',
              "parameter names are non-empty");
        check(defs.defs[i].minValue <= defs.defs[i].maxValue,
              "parameter ranges are ordered");
        check(defs.defs[i].defaultValue >= defs.defs[i].minValue &&
              defs.defs[i].defaultValue <= defs.defs[i].maxValue,
              "parameter defaults lie inside their ranges");
    }
    for (uint8_t i = 0; i < defs.count; ++i)
        for (uint8_t j = i + 1; j < defs.count; ++j)
            check(defs.defs[i].id != defs.defs[j].id, "parameter ids are unique");

    check(std::strcmp(defs.defs[0].name, "Density") == 0, "Density name");
    check(defs.defs[0].minValue == 0 && defs.defs[0].maxValue == 127 &&
          defs.defs[0].defaultValue == 96, "Density range and default");
    check(defs.defs[0].hint == GeneratorParamHint::Percent, "Density UI hint");
    check(defs.defs[1].minValue == -64 && defs.defs[1].maxValue == 64 &&
          defs.defs[1].defaultValue == 0, "Offset range and default");
    check(defs.defs[1].hint == GeneratorParamHint::None, "Offset UI hint");
    check(defs.defs[2].minValue == 0 && defs.defs[2].maxValue == 127 &&
          defs.defs[2].defaultValue == 60, "Root Note range and default");
    check(defs.defs[2].hint == GeneratorParamHint::Note, "Root Note UI hint");

    check(stub.paramDefs().count == 0, "a stub with no parameters declares none");
}

// --- 7: base-pointer dispatch + context/RNG plumbing ---------------------------

void test_generate_dispatch() {
    FakeGatePitchGenerator fake;
    sequencer::IGenerator& byBase = fake;  // dispatch through the interface

    common::Scale scale(2, 1);  // any built-in root/template: plumbing only
    GeneratorRng rng(0xC0FFEEu);
    GeneratorContext context(16, &scale, rng);
    GeneratorOutput out;

    byBase.generate(5, context, out);
    check(out.hasGate, "the gate field is present in the output");
    check(!out.gate, "an odd step index drives gate=false");
    check(out.hasPitch, "the pitch field is present in the output");
    check(out.pitch == 21, "the pitch field carries the generated value");
    check(fake.lastStepIndex == 5, "generate() received the step index");
    check(fake.lastPatternLength == 16, "the context carried the pattern length");
    check(fake.lastScale == &scale, "the context carried the scale pointer");

    // Same seed -> the fake consumed the same RNG byte; different seed ->
    // a different byte (deterministic, seedable randomness).
    GeneratorRng sameSeed(0xC0FFEEu);
    GeneratorContext sameContext(16, &scale, sameSeed);
    GeneratorOutput sameOut;
    byBase.generate(5, sameContext, sameOut);
    check(sameOut.gate == out.gate && sameOut.pitch == out.pitch,
          "the same (seed, step) produces the same output");

    GeneratorRng otherSeed(0xBADF00Du);
    GeneratorContext otherContext(16, &scale, otherSeed);
    GeneratorOutput otherOut;
    byBase.generate(5, otherContext, otherOut);
    check(otherOut.pitch == out.pitch, "the pitch path itself is seed-independent");

    // The fake consumed RNG entropy through the context reference, and a
    // fresh RNG with the same seed reproduces the exact same byte.
    GeneratorRng replay(0xC0FFEEu);
    const uint8_t expectedByte = replay.nextByte();
    check(fake.lastRandomByte == expectedByte,
          "generate() consumed the context RNG (deterministic replay)");
}

void test_rng_determinism() {
    GeneratorRng a(42);
    GeneratorRng b(42);
    GeneratorRng c(43);

    bool sameSequence = true;
    bool diverged = false;
    for (int i = 0; i < 256; ++i) {
        const uint8_t va = a.nextByte();
        const uint8_t vb = b.nextByte();
        const uint8_t vc = c.nextByte();
        if (va != vb) sameSequence = false;
        if (va != vc) diverged = true;
    }
    check(sameSequence, "the same seed replays the same RNG sequence");
    check(diverged, "a different seed produces a different sequence");

    // nextBelow stays inside [0, bound) for every small bound.
    bool inBounds = true;
    for (int i = 0; i < 256; ++i) {
        const uint8_t v = a.nextBelow(5);
        if (v >= 5) inBounds = false;
    }
    check(inBounds, "nextBelow stays inside its bound");
    check(a.nextBelow(0) == 0, "nextBelow(0) is a total 0");
    check(a.nextBelow(1) == 0, "nextBelow(1) always yields 0");
}

// --- 8: the built-in registration list (AC1) -----------------------------------

void test_register_generators_builtin_list() {
    sequencer::GeneratorRegistry registry;
    sequencer::registerGenerators(registry);
    // Since T4 the built-in list holds the Euclidean gate generator; T5
    // appends the scale-aware pitch generator and T6 the probability gate
    // generator (T7 adds the remaining plugin here).
    check(registry.count() == 3,
          "the built-in list registers the Euclidean gate generator (T4), "
          "the scale-aware pitch generator (T5) and the probability gate "
          "generator (T6)");
    const sequencer::IGenerator* euclid = registry.find("builtin.euclidean");
    check(euclid != nullptr, "builtin.euclidean is reachable by id");
    check(euclid != nullptr &&
              euclid->capabilities().has(GeneratorCapability::GATE),
          "the registered Euclidean generator declares GATE");
    check(euclid != nullptr && euclid->paramDefs().count == 3,
          "the registered Euclidean generator exposes its parameters");
    const sequencer::IGenerator* pitch = registry.find("builtin.pitch");
    check(pitch != nullptr, "builtin.pitch is reachable by id");
    check(pitch != nullptr &&
              pitch->capabilities().has(GeneratorCapability::PITCH),
          "the registered pitch generator declares PITCH");
    const sequencer::IGenerator* probability =
        registry.find("builtin.probability");
    check(probability != nullptr, "builtin.probability is reachable by id");
    check(probability != nullptr &&
              probability->capabilities().has(GeneratorCapability::GATE),
          "the registered probability generator declares GATE");
    check(probability != nullptr && probability->paramDefs().count == 1,
          "the registered probability generator exposes its parameter");
}

}  // namespace

int main() {
    test_registration_and_lookup();
    test_capacity();
    test_capabilities();
    test_param_defs();
    test_generate_dispatch();
    test_rng_determinism();
    test_register_generators_builtin_list();

    if (failures == 0) {
        std::printf("test_generator_registry: all checks passed\n");
        return 0;
    }
    std::printf("test_generator_registry: %d check(s) failed\n", failures);
    return 1;
}
