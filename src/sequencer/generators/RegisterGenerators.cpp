// Built-in generator registration (generative-engine-expansion task 2,
// FR1, AC1).
//
// This translation unit is the ONLY place built-in generators are listed:
// a new generator plugin is added by implementing IGenerator in a header
// under src/sequencer/generators/ and registered here — no core sequencer
// file (sequencer.h / sequencer.cpp) is touched (AC1).
//
// It currently registers NOTHING, on purpose. Tasks T4-T7 add the built-in
// plugins:
//   - EuclideanGateGenerator   (T4)
//   - ScaleAwarePitchGenerator (T5)
//   - ProbabilityGateGenerator (T6)
//   - NoteDurationGenerator    (T7)
// until then this list stays empty.

#include "../GeneratorRegistry.h"

namespace sequencer {

void registerGenerators(GeneratorRegistry& registry) {
    // No built-in generators yet (T4-T7 populate this list).
    (void)registry;
}

}  // namespace sequencer
