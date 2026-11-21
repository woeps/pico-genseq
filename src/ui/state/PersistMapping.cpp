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
static_assert(SET_LIBRARY_SIZE == persistence::SET_LIBRARY_SIZE, "library sizes must match");

} // namespace

namespace {

void toGateRecord(const GateSetConfig& gate, persistence::GateSetRecord& g) {
    g.algorithm = static_cast<uint8_t>(gate.algorithm);
    g.steps = gate.steps;
    g.pulses = gate.pulses;
    g.rotation = gate.rotation;
    g.noteLength = static_cast<uint8_t>(gate.noteLength);
    g.gateLength = gate.gateLength;

    // R9: explicit per-step gate states + per-step/per-Pattern probability.
    const uint8_t gridSteps = gate.steps < MAX_GRID_STEPS ? gate.steps : MAX_GRID_STEPS;
    g.gateStepCount = gridSteps;
    for (uint8_t j = 0; j < gridSteps; ++j) g.gateStates[j] = gate.gateStates[j];
    g.stepProbCount = gridSteps;
    for (uint8_t j = 0; j < gridSteps; ++j) g.stepProbability[j] = gate.stepProbability[j];
    g.patternProbability = gate.patternProbability;
}

void toNoteRecord(const PitchSetConfig& pitch, persistence::NoteSetRecord& n) {
    n.count = pitch.count > persistence::MAX_SET_LEN ? persistence::MAX_SET_LEN : pitch.count;
    n.order = static_cast<uint8_t>(pitch.order);
    for (uint8_t j = 0; j < n.count; j++) n.pitches[j] = pitch.pitches[j];
    // Scale constraint (generative-engine-expansion R8/R10) lives with the note set.
    n.scale = static_cast<uint8_t>(pitch.scale);
    n.scaleRoot = pitch.scaleRoot;
    n.quantize = static_cast<uint8_t>(pitch.quantize);
}

void toVelocityRecord(const VelocitySetConfig& vel, persistence::VelocitySetRecord& v) {
    v.count = vel.count > persistence::MAX_SET_LEN ? persistence::MAX_SET_LEN : vel.count;
    v.order = static_cast<uint8_t>(vel.order);
    for (uint8_t j = 0; j < v.count; j++) v.velocities[j] = vel.velocities[j];
    // humanize / seed / coupling (R3/R4/R5) live with the velocity set.
    v.humanizeRange = vel.humanizeRange;
    v.seedSet = vel.seedSet ? 1 : 0;
    v.seed = vel.seed;
    v.coupling = static_cast<uint8_t>(vel.coupling);
}

GateSetConfig fromGateRecord(const persistence::GateSetRecord& g) {
    GateSetConfig gate{};
    gate.algorithm = static_cast<GateAlgorithm>(g.algorithm);
    gate.steps = g.steps;
    gate.pulses = g.pulses;
    gate.rotation = g.rotation;
    gate.noteLength = static_cast<NoteLength>(g.noteLength);
    gate.gateLength = g.gateLength;
    // R9: restore explicit gate states + probability. A record without explicit states
    // (gateStepCount == 0, e.g. migrated from v1) is seeded from the Euclidean generator so
    // the restored set keeps its rhythm (expandGateStates of a seeded config ==
    // createEuclidean output). Otherwise the states (which may include manual edits) are
    // restored verbatim.
    if (g.gateStepCount == 0) {
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
        for (uint8_t j = 0; j < g.gateStepCount && j < MAX_GRID_STEPS; ++j) {
            gate.gateStates[j] = g.gateStates[j];
        }
    }
    for (uint8_t j = 0; j < g.stepProbCount && j < MAX_GRID_STEPS; ++j) {
        gate.stepProbability[j] = g.stepProbability[j];
    }
    gate.patternProbability = g.patternProbability;
    return gate;
}

PitchSetConfig fromNoteRecord(const persistence::NoteSetRecord& n) {
    PitchSetConfig pitch{};
    pitch.count = n.count > MAX_PITCHES ? MAX_PITCHES : n.count;
    pitch.order = static_cast<common::PlayingOrder>(n.order);
    for (uint8_t j = 0; j < pitch.count; j++) pitch.pitches[j] = n.pitches[j];
    // A legacy record carries defaults (CHROMATIC/0/NEAREST): no scale constraint.
    pitch.scale = static_cast<common::ScaleId>(n.scale);
    pitch.scaleRoot = n.scaleRoot;
    pitch.quantize = static_cast<common::QuantizeMode>(n.quantize);
    return pitch;
}

VelocitySetConfig fromVelocityRecord(const persistence::VelocitySetRecord& v) {
    VelocitySetConfig vel{};
    vel.count = v.count > MAX_VELOCITIES ? MAX_VELOCITIES : v.count;
    vel.order = static_cast<common::PlayingOrder>(v.order);
    for (uint8_t j = 0; j < vel.count; j++) vel.velocities[j] = v.velocities[j];
    vel.humanizeRange = v.humanizeRange;
    vel.seedSet = v.seedSet != 0;
    vel.seed = v.seed;
    vel.coupling = static_cast<common::CouplingMode>(v.coupling);
    return vel;
}

uint8_t lowestSet(uint16_t mask) {
    for (uint8_t i = 0; i < SET_LIBRARY_SIZE; ++i) {
        if ((mask >> i) & 1u) return i;
    }
    return 0;
}

// A reference to a free or out-of-range slot (only reachable from a hand-built config; the
// codec rejects such records) falls back to the library's lowest occupied slot.
uint8_t validRef(uint16_t mask, uint8_t ref) {
    return (ref < SET_LIBRARY_SIZE && ((mask >> ref) & 1u)) ? ref : lowestSet(mask);
}

}  // namespace

void toPersistableConfig(const UIState& state, persistence::PersistableConfig& cfg) {
    static const persistence::PersistableConfig kDefault{};
    cfg = kDefault;  // reset in place: no 2 KB temporary on the caller's stack

    uint8_t count = state.patternCount;
    if (count > persistence::MAX_PATTERNS) count = persistence::MAX_PATTERNS;
    cfg.patternCount = count;
    cfg.bpm = state.bpm;
    cfg.midiClockEnabled = DEFAULT_MIDI_CLOCK_ENABLED;

    cfg.gateOccupied = state.gateLib.occupied;
    cfg.noteOccupied = state.noteLib.occupied;
    cfg.velocityOccupied = state.velocityLib.occupied;
    for (uint8_t s = 0; s < SET_LIBRARY_SIZE; ++s) {
        if ((state.gateLib.occupied >> s) & 1u) toGateRecord(state.gateLib.slots[s], cfg.gateSets[s]);
        if ((state.noteLib.occupied >> s) & 1u) toNoteRecord(state.noteLib.slots[s], cfg.noteSets[s]);
        if ((state.velocityLib.occupied >> s) & 1u) {
            toVelocityRecord(state.velocityLib.slots[s], cfg.velocitySets[s]);
        }
    }

    for (uint8_t i = 0; i < count; i++) {
        persistence::PatternConfig& p = cfg.patterns[i];
        p.active = static_cast<uint8_t>((state.activePatterns >> i) & 0x1);
        p.midiChannel = DEFAULT_MIDI_CHANNEL;
        p.gateRef = state.patternRefs[i].gate;
        p.noteRef = state.patternRefs[i].note;
        p.velocityRef = state.patternRefs[i].velocity;
    }
}

persistence::PersistableConfig toPersistableConfig(const UIState& state) {
    persistence::PersistableConfig cfg{};
    toPersistableConfig(state, cfg);
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

    // Libraries: every library keeps at least one set (default slot 0 if the mask is empty).
    const uint16_t gateMask = cfg.gateOccupied ? cfg.gateOccupied : 0x0001;
    const uint16_t noteMask = cfg.noteOccupied ? cfg.noteOccupied : 0x0001;
    const uint16_t velocityMask = cfg.velocityOccupied ? cfg.velocityOccupied : 0x0001;
    state.gateLib.occupied = gateMask;
    state.noteLib.occupied = noteMask;
    state.velocityLib.occupied = velocityMask;
    for (uint8_t s = 0; s < SET_LIBRARY_SIZE; ++s) {
        state.gateLib.slots[s] =
            ((gateMask >> s) & 1u) ? fromGateRecord(cfg.gateSets[s]) : GateSetConfig{};
        state.noteLib.slots[s] =
            ((noteMask >> s) & 1u) ? fromNoteRecord(cfg.noteSets[s]) : PitchSetConfig{};
        state.velocityLib.slots[s] = ((velocityMask >> s) & 1u)
                                         ? fromVelocityRecord(cfg.velocitySets[s])
                                         : VelocitySetConfig{};
    }

    uint16_t activeMask = 0;
    for (uint8_t i = 0; i < count; i++) {
        // cfg.patterns[i] is only meaningful for i < cfg.patternCount; a config with no
        // patterns still gets one pattern on the default set 0 of each library.
        const persistence::PatternConfig p =
            i < cfg.patternCount ? cfg.patterns[i] : persistence::PatternConfig{};
        if (p.active) activeMask |= static_cast<uint16_t>(1u << i);
        state.patternRefs[i] = PatternRefs{validRef(gateMask, p.gateRef),
                                           validRef(noteMask, p.noteRef),
                                           validRef(velocityMask, p.velocityRef)};
    }
    // Patterns beyond the restored count carry no references.
    for (uint8_t i = count; i < MAX_PATTERNS; i++) state.patternRefs[i] = PatternRefs{};

    // Selection follows the first pattern's references so the set views open on real sets.
    state.selectedGateSlot = state.patternRefs[0].gate;
    state.selectedNoteSlot = state.patternRefs[0].note;
    state.selectedVelocitySlot = state.patternRefs[0].velocity;

    state.activePatterns = activeMask;
    // Draft / dirty / selection / save-status fields are intentionally left at
    // their defaults (already zero/false from a fresh UIState).
}

} // namespace ui::state
