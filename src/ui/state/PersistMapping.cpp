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

        p.pitchCount = pitch.count;
        if (p.pitchCount > persistence::MAX_SET_LEN) {
            p.pitchCount = persistence::MAX_SET_LEN;
        }
        p.pitchOrder = static_cast<uint8_t>(pitch.order);
        for (uint8_t j = 0; j < p.pitchCount; j++) {
            p.pitches[j] = pitch.pitches[j];
        }

        p.velocityCount = vel.count;
        if (p.velocityCount > persistence::MAX_SET_LEN) {
            p.velocityCount = persistence::MAX_SET_LEN;
        }
        p.velocityOrder = static_cast<uint8_t>(vel.order);
        for (uint8_t j = 0; j < p.velocityCount; j++) {
            p.velocities[j] = vel.velocities[j];
        }

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
        state.gateSetConfigs[i] = gate;

        PitchSetConfig pitch{};
        pitch.count = p.pitchCount > MAX_PITCHES ? MAX_PITCHES : p.pitchCount;
        pitch.order = static_cast<common::PlayingOrder>(p.pitchOrder);
        for (uint8_t j = 0; j < pitch.count; j++) {
            pitch.pitches[j] = p.pitches[j];
        }
        state.pitchSetConfigs[i] = pitch;

        VelocitySetConfig vel{};
        vel.count = p.velocityCount > MAX_VELOCITIES ? MAX_VELOCITIES : p.velocityCount;
        vel.order = static_cast<common::PlayingOrder>(p.velocityOrder);
        for (uint8_t j = 0; j < vel.count; j++) {
            vel.velocities[j] = p.velocities[j];
        }
        state.velocitySetConfigs[i] = vel;
    }

    state.activePatterns = activeMask;
    // Draft / dirty / selection / save-status fields are intentionally left at
    // their defaults (already zero/false from a fresh UIState).
}

} // namespace ui::state
