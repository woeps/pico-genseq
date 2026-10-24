// Built-in generator registration (generative-engine-expansion task 2,
// FR1, AC1).
//
// This translation unit is the ONLY place built-in generators are listed:
// a new generator plugin is added by implementing IGenerator in a header
// under src/sequencer/generators/ and registered here — no core sequencer
// file (sequencer.h / sequencer.cpp) is touched (AC1).
//
// Registered built-ins:
//   - EuclideanGateGenerator     (T4) — "builtin.euclidean", GATE capability
//   - ScaleAwarePitchGenerator   (T5) — "builtin.pitch", PITCH capability
//   - ProbabilityGateGenerator   (T6) — "builtin.probability", GATE capability
//   - NoteDurationGenerator      (T7) — "builtin.duration", DURATION capability
//
// All four FR2 built-in plugins are registered: a new generator plugin is
// added by implementing IGenerator in a header under
// src/sequencer/generators/ and registered here — no core sequencer file
// (sequencer.h / sequencer.cpp) is touched (AC1).
//
// The instances have static storage duration (no heap); each is registered
// once with its factory configuration. Repeated registerGenerators() calls
// stay safe through GeneratorRegistry::add's duplicate guard.

#include "../GeneratorRegistry.h"
#include "EuclideanGateGenerator.h"
#include "NoteDurationGenerator.h"
#include "ProbabilityGateGenerator.h"
#include "ScaleAwarePitchGenerator.h"

namespace sequencer {

namespace {

// The registered built-in plugins: static storage duration, factory
// configuration (the Euclidean generator defaults to 16 steps, 4 pulses,
// no rotation — the GateSetConfig factory values; the scale-aware pitch
// generator defaults to the two-octave note span 48..72; the probability
// gate generator defaults to probability 64 — roughly half density; the
// note duration generator defaults to a constant one-beat note — base 24
// ticks, window 1..48, no variation).
EuclideanGateGenerator euclideanGateGenerator;
ScaleAwarePitchGenerator scaleAwarePitchGenerator;
ProbabilityGateGenerator probabilityGateGenerator;
NoteDurationGenerator noteDurationGenerator;

}  // namespace

void registerGenerators(GeneratorRegistry& registry) {
    registry.add(&euclideanGateGenerator);
    registry.add(&scaleAwarePitchGenerator);
    registry.add(&probabilityGateGenerator);
    registry.add(&noteDurationGenerator);
}

}  // namespace sequencer
