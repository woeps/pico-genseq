// generative-engine-expansion task 1.2 — host unit tests for the
// GenerationParameters value object.
//
// Validates Requirements 1.5, 2.3, 3.3 (the behavior-preserving-default
// contract) plus the design's single-byte inherit sentinel:
//   - a default-constructed value reports isDefault();
//   - each single non-default field independently flips isDefault() false;
//   - STEP_PROB_INHERIT is distinct from an explicit 100 and from 0, so the UI
//     can tell "inherit" apart from "explicit 100 percent" (Req 9.5).
//
// Standalone example (its own main(), no RapidCheck), builds host-side
// unconditionally and is excluded from the property-test glob.

#include "common/GenerationParameters.h"

#include <cstdint>
#include <cstdio>

using common::GenerationParameters;
using common::ScaleId;
using common::QuantizeMode;
using common::CouplingMode;
using common::STEP_PROB_INHERIT;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// A freshly constructed value is at every behavior-preserving default, so the
// emission lens will short-circuit to today's exact note-on (Req 1.5/2.3/3.3).
void test_default_is_default() {
    GenerationParameters gp;
    check(gp.isDefault(), "default-constructed GenerationParameters.isDefault()");

    // Spot-check the individual defaults the lens relies on.
    check(gp.scale == ScaleId::CHROMATIC, "default scale is CHROMATIC");
    check(gp.quantize == QuantizeMode::NEAREST, "default quantize is NEAREST");
    check(gp.patternProbability == 100, "default patternProbability is 100");
    check(gp.humanizeRange == 0, "default humanizeRange is 0");
    check(!gp.seedSet, "default seedSet is false");
    check(gp.coupling == CouplingMode::INDEPENDENT, "default coupling is INDEPENDENT");
    for (uint8_t i = 0; i < GenerationParameters::kMaxSteps; ++i) {
        check(gp.stepProbability[i] == STEP_PROB_INHERIT,
              "default per-step probability is INHERIT");
    }
}

// Each single non-default field, in isolation, must flip isDefault() false —
// otherwise the lens could skip a feature the performer actually enabled.
void test_each_field_flips_default() {
    {
        GenerationParameters gp; gp.scale = ScaleId::MAJOR;
        check(!gp.isDefault(), "non-CHROMATIC scale is not default");
    }
    {
        GenerationParameters gp; gp.scaleRoot = 2;
        check(!gp.isDefault(), "non-zero scaleRoot is not default");
    }
    {
        GenerationParameters gp; gp.quantize = QuantizeMode::SKIP;
        check(!gp.isDefault(), "non-NEAREST quantize is not default");
    }
    {
        GenerationParameters gp; gp.patternProbability = 99;
        check(!gp.isDefault(), "patternProbability < 100 is not default");
    }
    {
        GenerationParameters gp; gp.humanizeRange = 1;
        check(!gp.isDefault(), "non-zero humanizeRange is not default");
    }
    {
        GenerationParameters gp; gp.seedSet = true;
        check(!gp.isDefault(), "seedSet is not default");
    }
    {
        GenerationParameters gp; gp.coupling = CouplingMode::LOCKED;
        check(!gp.isDefault(), "LOCKED coupling is not default");
    }
    {
        GenerationParameters gp; gp.stepProbability[0] = 100;  // explicit 100, NOT inherit
        check(!gp.isDefault(), "an explicit per-step probability is not default");
    }
    {
        GenerationParameters gp; gp.stepProbability[GenerationParameters::kMaxSteps - 1] = 0;
        check(!gp.isDefault(), "an explicit per-step probability on the last step is not default");
    }
}

// The inherit sentinel must be unambiguous: not 0, not 100, so "inherit" and
// "explicit 100 percent" are distinguishable states (Req 9.5).
void test_inherit_sentinel_distinct() {
    check(STEP_PROB_INHERIT != 100, "INHERIT sentinel is distinct from explicit 100");
    check(STEP_PROB_INHERIT != 0, "INHERIT sentinel is distinct from explicit 0");
    check(STEP_PROB_INHERIT > 100, "INHERIT sentinel is outside the valid 0..100 percent range");

    // Setting a step to an explicit 100 and back to INHERIT round-trips the state.
    GenerationParameters gp;
    gp.stepProbability[3] = 100;
    check(!gp.isDefault(), "explicit 100 on a step leaves default");
    gp.stepProbability[3] = STEP_PROB_INHERIT;
    check(gp.isDefault(), "restoring the step to INHERIT returns to default");
}

}  // namespace

int main() {
    test_default_is_default();
    test_each_field_flips_default();
    test_inherit_sentinel_distinct();

    if (failures == 0) {
        std::printf("example_generation_parameters: all checks passed\n");
        return 0;
    }
    std::printf("example_generation_parameters: %d check(s) failed\n", failures);
    return 1;
}
