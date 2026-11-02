#include "PersistMapping.h"

namespace ui::state {

namespace {

// Convention (documented, no UI field exists for these today):
//   midiChannel      -> 1 for every pattern. This matches common::Pattern's
//                       default midiChannel (1) that the sequencer uses today;
//                       UIState carries no per-pattern channel, so we persist
//                       the current firmware default. The schema reserves the
//                       field so a future UI can edit it without a format bump.
//   midiClockEnabled -> 1. MIDI clock is enabled by the current firmware
//                       convention and UIState has no field for it.
constexpr uint8_t DEFAULT_MIDI_CHANNEL = 1;
constexpr uint8_t DEFAULT_MIDI_CLOCK_ENABLED = 1;

} // namespace

persistence::PersistableConfig toPersistableConfig(const UIState& state) {
    persistence::PersistableConfig cfg{};

    uint8_t count = state.patternCount;
    if (count > persistence::MAX_PATTERNS) count = persistence::MAX_PATTERNS;
    cfg.patternCount = count;
    cfg.bpm = state.bpm;
    cfg.midiClockEnabled = DEFAULT_MIDI_CLOCK_ENABLED;

    for (uint8_t i = 0; i < count; i++) {
        persistence::PatternConfig& p = cfg.patterns[i];
        const GateSetConfig& gate = state.gateSetConfigs[i];
        const PitchSetConfig& pitch = state.pitchSetConfigs[i];
        const VelocitySetConfig& vel = state.velocitySetConfigs[i];

        p.active = static_cast<uint8_t>((state.activePatterns >> i) & 0x1);

        p.gateAlgorithm = static_cast<uint8_t>(gate.algorithm);
        p.gateSteps = gate.steps;
        p.gatePulses = gate.pulses;
        p.gateRotation = gate.rotation;
        p.gateNoteLength = static_cast<uint8_t>(gate.noteLength);
        p.gateLength = gate.gateLength;

        // R9: explicit per-step gate states + per-step/per-Pattern probability.
        const uint8_t gridSteps = gate.steps < MAX_GRID_STEPS ? gate.steps : MAX_GRID_STEPS;
        p.gateStepCount = gridSteps;
        for (uint8_t j = 0; j < gridSteps; ++j) p.gateStates[j] = gate.gateStates[j];
        p.genStepProbCount = gridSteps;
        for (uint8_t j = 0; j < gridSteps; ++j) p.genStepProbability[j] = gate.stepProbability[j];
        p.genPatternProbability = gate.patternProbability;

        p.pitchCount = pitch.count;
        if (p.pitchCount > persistence::MAX_SET_LEN) {
            p.pitchCount = persistence::MAX_SET_LEN;
        }
        p.pitchOrder = static_cast<uint8_t>(pitch.order);
        for (uint8_t j = 0; j < p.pitchCount; j++) {
            p.pitches[j] = pitch.pitches[j];
        }

        // Scale constraint (generative-engine-expansion R8/R10): persist the UI
        // scale fields into the Generation Parameters block. Other gen-params
        // (probability/humanize/seed/coupling) are threaded in by their own
        // views in tasks 9/10.
        p.genScale = static_cast<uint8_t>(pitch.scale);
        p.genScaleRoot = pitch.scaleRoot;
        p.genQuantize = static_cast<uint8_t>(pitch.quantize);

        p.velocityCount = vel.count;
        if (p.velocityCount > persistence::MAX_SET_LEN) {
            p.velocityCount = persistence::MAX_SET_LEN;
        }
        p.velocityOrder = static_cast<uint8_t>(vel.order);
        for (uint8_t j = 0; j < p.velocityCount; j++) {
            p.velocities[j] = vel.velocities[j];
        }

        // Pattern-scope Generation Parameters hosted in the VelocitySet view
        // (R3/R4/R5): humanize, seed, coupling.
        p.genHumanizeRange = vel.humanizeRange;
        p.genSeedSet = vel.seedSet ? 1 : 0;
        p.genSeed = vel.seed;
        p.genCoupling = static_cast<uint8_t>(vel.coupling);

        p.midiChannel = DEFAULT_MIDI_CHANNEL;
    }

    return cfg;
}

void applyPersistableConfig(UIState& state,
                            const persistence::PersistableConfig& cfg) {
    uint8_t count = cfg.patternCount;
    if (count > MAX_PATTERNS) count = MAX_PATTERNS;
    if (count > persistence::MAX_PATTERNS) count = persistence::MAX_PATTERNS;
    // Keep at least one pattern so the UI always has a selectable pattern.
    if (count == 0) count = 1;

    state.patternCount = count;
    state.bpm = static_cast<uint8_t>(cfg.bpm);

    uint16_t activeMask = 0;
    for (uint8_t i = 0; i < count; i++) {
        const persistence::PatternConfig& p = cfg.patterns[i];

        if (p.active) activeMask |= static_cast<uint16_t>(1u << i);

        GateSetConfig gate{};
        gate.algorithm = static_cast<GateAlgorithm>(p.gateAlgorithm);
        gate.steps = p.gateSteps;
        gate.pulses = p.gatePulses;
        gate.rotation = p.gateRotation;
        gate.noteLength = static_cast<NoteLength>(p.gateNoteLength);
        gate.gateLength = p.gateLength;
        // R9: restore explicit gate states + probability. A v1 record has
        // gateStepCount==0: SEED gateStates from the Euclidean generator so the
        // restored pattern keeps its rhythm (expandGateStates of a seeded config
        // == createEuclidean output). A v2 record carries explicit gateStates
        // (which may include manual edits) and is restored verbatim.
        if (p.gateStepCount == 0) {
            // Bjorklund distribution, mirroring regenerateGateStates / createEuclidean.
            const uint8_t steps = gate.steps < MAX_GRID_STEPS ? gate.steps : MAX_GRID_STEPS;
            for (uint8_t j = 0; j < MAX_GRID_STEPS; ++j) gate.gateStates[j] = 0;
            if (gate.pulses >= gate.steps) {
                for (uint8_t j = 0; j < steps; ++j) gate.gateStates[j] = 1;
            } else if (gate.pulses > 0) {
                int bucket = 0;
                for (uint8_t j = 0; j < gate.steps; ++j) {
                    bucket += gate.pulses;
                    if (bucket >= gate.steps) {
                        bucket -= gate.steps;
                        const uint8_t pos = (j + gate.rotation) % gate.steps;
                        if (pos < MAX_GRID_STEPS) gate.gateStates[pos] = 1;
                    }
                }
            }
        } else {
            for (uint8_t j = 0; j < p.gateStepCount && j < MAX_GRID_STEPS; ++j) {
                gate.gateStates[j] = p.gateStates[j];
            }
        }
        for (uint8_t j = 0; j < p.genStepProbCount && j < MAX_GRID_STEPS; ++j) {
            gate.stepProbability[j] = p.genStepProbability[j];
        }
        gate.patternProbability = p.genPatternProbability;
        state.gateSetConfigs[i] = gate;

        PitchSetConfig pitch{};
        pitch.count = p.pitchCount > MAX_PITCHES ? MAX_PITCHES : p.pitchCount;
        pitch.order = static_cast<common::PlayingOrder>(p.pitchOrder);
        for (uint8_t j = 0; j < pitch.count; j++) {
            pitch.pitches[j] = p.pitches[j];
        }
        // Scale constraint (R8/R10): restore from the Generation Parameters
        // block. A v1 record carries defaults (CHROMATIC/0/NEAREST), so a
        // pre-milestone save loads with no scale constraint (behavior-preserving).
        pitch.scale = static_cast<common::ScaleId>(p.genScale);
        pitch.scaleRoot = p.genScaleRoot;
        pitch.quantize = static_cast<common::QuantizeMode>(p.genQuantize);
        state.pitchSetConfigs[i] = pitch;

        VelocitySetConfig vel{};
        vel.count = p.velocityCount > MAX_VELOCITIES ? MAX_VELOCITIES : p.velocityCount;
        vel.order = static_cast<common::PlayingOrder>(p.velocityOrder);
        for (uint8_t j = 0; j < vel.count; j++) {
            vel.velocities[j] = p.velocities[j];
        }
        // Pattern-scope gen-params (R3/R4/R5). v1 records carry defaults
        // (0 / false / INDEPENDENT) — behavior-preserving.
        vel.humanizeRange = p.genHumanizeRange;
        vel.seedSet = p.genSeedSet != 0;
        vel.seed = p.genSeed;
        vel.coupling = static_cast<common::CouplingMode>(p.genCoupling);
        state.velocitySetConfigs[i] = vel;
    }

    state.activePatterns = activeMask;
    // Draft / dirty / selection / save-status fields are intentionally left at
    // their defaults (already zero/false from a fresh UIState).
}

} // namespace ui::state
