#include "Reducer.h"
#include "SetLibrary.h"
#include "../../commands/command.h"
#include "../../common/gate_set.h"
#include "../../common/pitch_set.h"
#include <algorithm>
#include <cstdio>

namespace ui::state {

namespace {

// Gen-params senders (defined further down, used by the restore helpers).
void sendPatternGenParams(const UIState& state, uint8_t patternIndex);
void previewGateGenParams(const UIState& s);
void previewPitchGenParams(const UIState& s);
void previewVelocityGenParams(const UIState& s);
void sendPitchSetConfig(uint8_t patternIndex, const PitchSetConfig& config);
void sendVelocitySetConfig(uint8_t patternIndex, const VelocitySetConfig& config);
void sendGenParamsFrom(uint8_t patternIndex, const PitchSetConfig& pitch,
                       const GateSetConfig& gate, const VelocitySetConfig& vel);

constexpr GateSetProperty EUCLIDEAN_PROPERTIES[] = {
    GateSetProperty::ALGORITHM,
    GateSetProperty::STEPS,
    GateSetProperty::PULSES,
    GateSetProperty::ROTATION,
    GateSetProperty::NOTE_LENGTH,
    GateSetProperty::LENGTH,
    GateSetProperty::PROBABILITY,  // R2/R10 per-Pattern probability
    GateSetProperty::STEP_GRID,  // R9 per-step grid, last so it sits after the generator
};

uint8_t noteLengthTicks(NoteLength noteLength) {
    switch (noteLength) {
        case NoteLength::THIRTY_SECOND: return 3;
        case NoteLength::SIXTEENTH: return 6;
        case NoteLength::EIGHTH: return 12;
        case NoteLength::QUARTER: return 24;
        case NoteLength::HALF: return 48;
        case NoteLength::WHOLE: return 96;
    }
    return 6;
}

// R9.3: regenerate the per-step GATE states from the Euclidean generator (the
// "optional seed"), PRESERVING per-step probability values. Called when a
// generator property (STEPS/PULSES/ROTATION) changes. Probability is indexed by
// step position, so it is left untouched across a regenerate; steps newly in
// range keep whatever probability they held (INHERIT by default).
void regenerateGateStates(GateSetConfig& config) {
    // Build the programmed-step pulse pattern (same Bjorklund distribution
    // createEuclidean uses), then store it as per-step gate states.
    const uint8_t steps = config.steps > MAX_GRID_STEPS ? MAX_GRID_STEPS : config.steps;
    for (uint8_t i = 0; i < MAX_GRID_STEPS; ++i) config.gateStates[i] = 0;
    if (steps == 0) return;
    if (config.pulses >= config.steps) {
        for (uint8_t i = 0; i < steps; ++i) config.gateStates[i] = 1;
        return;
    }
    if (config.pulses == 0) return;
    int bucket = 0;
    for (uint8_t i = 0; i < config.steps; ++i) {
        bucket += config.pulses;
        if (bucket >= config.steps) {
            bucket -= config.steps;
            const uint8_t pos = (i + config.rotation) % config.steps;
            if (pos < MAX_GRID_STEPS) config.gateStates[pos] = 1;
        }
    }
}

// Expand the explicit per-step gate states (config.gateStates, R9) into the
// tick-level boolean gate array the sequencer consumes. This is the manual-edit-
// aware replacement for calling createEuclidean directly: the generator seeds
// gateStates (regenerateGateStates), manual toggles edit them, and THIS builds
// the wire gate array from whatever gateStates now holds. The high-tick gate-
// length shaping matches createEuclidean so a generator-only config is
// byte-identical to the old output (R9.7).
std::vector<bool> expandGateStates(const GateSetConfig& config) {
    const uint8_t steps = config.steps;
    const uint8_t stepLen = noteLengthTicks(config.noteLength);
    if (steps == 0 || stepLen == 0) return {};
    const uint32_t patternLength = static_cast<uint32_t>(steps) * stepLen;
    std::vector<bool> expanded(patternLength, false);

    // Collect the tick positions of the active programmed steps.
    std::vector<uint32_t> pulsePositions;
    for (uint8_t s = 0; s < steps && s < MAX_GRID_STEPS; ++s) {
        if (config.gateStates[s]) pulsePositions.push_back(static_cast<uint32_t>(s) * stepLen);
    }
    const uint8_t gateLength = std::min<uint8_t>(config.gateLength, 100);
    for (size_t i = 0; i < pulsePositions.size(); ++i) {
        const uint32_t start = pulsePositions[i];
        const uint32_t next = pulsePositions[(i + 1) % pulsePositions.size()];
        const uint32_t interval = next > start ? next - start : patternLength - start + next;
        const uint32_t scaled = (interval * gateLength + 99) / 100;
        const uint32_t highTicks = std::max<uint32_t>(1, std::min<uint32_t>(interval - 1, scaled));
        for (uint32_t tick = 0; tick < highTicks; ++tick) {
            expanded[(start + tick) % patternLength] = true;
        }
    }
    return expanded;
}

void sendGateSet(uint8_t patternIndex, const GateSetConfig& config) {
    switch (config.algorithm) {
        case GateAlgorithm::EUCLIDEAN: {
            commands::sendGateSet(patternIndex, expandGateStates(config));
            break;
        }
    }
    // NOTE (task 9/10.4): the gate ARRAY is sent here so manual gate edits take
    // effect immediately. The per-step/per-Pattern probability and stepLength are
    // transmitted separately via sendPatternGenParams() on commit (GEN_PARAMS_SET
    // FIFO transport, task 10.4) — not on every scrub of a draft value.
}

void clampEuclidean(GateSetConfig& config) {
    config.gateLength = std::min<uint8_t>(config.gateLength, 100);
    config.steps = std::clamp<uint8_t>(config.steps, 1, 64);
    config.pulses = std::min(config.pulses, config.steps);
    config.rotation = std::min(config.rotation, static_cast<uint8_t>(config.steps - 1));
}

// --- Set-library fan-out ----------------------------------------------------
// core1 keeps one material copy per pattern, so an edit to a set is sent to every
// pattern that references it. When more than one command is sent they are framed
// in one BATCH_BEGIN/BATCH_END so core1 never ticks with some users updated and
// others not (R8.2).
class BatchScope {
public:
    explicit BatchScope(uint16_t commandCount) {
        if (commandCount > 1 && commandCount <= commands::MAX_BATCH_COMMANDS) {
            commands::sendCommand(commands::Command::BATCH_BEGIN,
                                  static_cast<uint8_t>(commandCount));
            open_ = true;
        }
    }
    ~BatchScope() {
        if (open_) commands::sendCommand(commands::Command::BATCH_END);
    }
    BatchScope(const BatchScope&) = delete;
    BatchScope& operator=(const BatchScope&) = delete;

private:
    bool open_ = false;
};

bool slotOccupied(const UIState& s, SetType type, uint8_t slot) {
    return slot < SET_LIBRARY_SIZE && ((occupiedMask(s, type) >> slot) & 1u);
}

// Generation Parameters for every user pattern of the slot. A non-null draft pointer
// replaces that stream's committed config (live preview); null uses the committed one.
void sendGenParamsToUsers(const UIState& s, SetType type, uint8_t slot,
                          const GateSetConfig* gateDraft, const PitchSetConfig* pitchDraft,
                          const VelocitySetConfig* velocityDraft) {
    const uint16_t users = usersOf(s, type, slot);
    for (uint8_t p = 0; p < s.patternCount; ++p) {
        if (!((users >> p) & 1u)) continue;
        sendGenParamsFrom(p, pitchDraft ? *pitchDraft : s.pitchSetOf(p),
                          gateDraft ? *gateDraft : s.gateSetOf(p),
                          velocityDraft ? *velocityDraft : s.velocitySetOf(p));
    }
}

void sendGateToUsers(const UIState& s, uint8_t slot, const GateSetConfig& config) {
    const uint16_t users = usersOf(s, SetType::GATE, slot);
    for (uint8_t p = 0; p < s.patternCount; ++p) {
        if ((users >> p) & 1u) sendGateSet(p, config);
    }
}

void sendPitchToUsers(const UIState& s, uint8_t slot, const PitchSetConfig& config) {
    const uint16_t users = usersOf(s, SetType::NOTE, slot);
    for (uint8_t p = 0; p < s.patternCount; ++p) {
        if ((users >> p) & 1u) sendPitchSetConfig(p, config);
    }
}

void sendVelocityToUsers(const UIState& s, uint8_t slot, const VelocitySetConfig& config) {
    const uint16_t users = usersOf(s, SetType::VELOCITY, slot);
    for (uint8_t p = 0; p < s.patternCount; ++p) {
        if ((users >> p) & 1u) sendVelocitySetConfig(p, config);
    }
}

uint16_t userCount(const UIState& s, SetType type, uint8_t slot) {
    return popcount16(usersOf(s, type, slot));
}

void restoreGateSetDraft(UIState& state) {
    const uint8_t slot = state.selectedGateSlot;
    if (!state.gateSetDirty || !slotOccupied(state, SetType::GATE, slot)) return;
    state.gateSetDraft = state.gateLib.slots[slot];
    state.gateSetDirty = false;
    BatchScope batch(2 * userCount(state, SetType::GATE, slot));
    sendGateToUsers(state, slot, state.gateSetDraft);
    // drop the previewed draft gen-params
    sendGenParamsToUsers(state, SetType::GATE, slot, nullptr, nullptr, nullptr);
}

void sendPitchSetConfig(uint8_t patternIndex, const PitchSetConfig& config) {
    std::vector<uint8_t> pitches(config.pitches.begin(),
                                 config.pitches.begin() + config.count);
    commands::sendPitchSet(patternIndex, config.count, config.order, pitches);
    // NOTE (task 8/10.4): the scale fields (scale/scaleRoot/quantize) are carried
    // to core1 by sendPatternGenParams() on commit (GEN_PARAMS_SET transport),
    // not here — this function sends only the pitch set itself.
}


void sendVelocitySetConfig(uint8_t patternIndex, const VelocitySetConfig& config) {
    std::vector<uint8_t> velocities(config.velocities.begin(),
                                    config.velocities.begin() + config.count);
    commands::sendVelocitySet(patternIndex, config.count, config.order, velocities);
    // NOTE (task 10/10.4): humanize/seed/coupling are carried to core1 by
    // sendPatternGenParams() on commit (GEN_PARAMS_SET transport), not here.
}

// task 10.4: assemble the full GenerationParameters for a pattern from its three
// per-stream configs (scale lives in PitchSet, probability+stepLength in GateSet,
// humanize/seed/coupling in VelocitySet) and transmit them to core1 as one
// GEN_PARAMS_SET command. Called after any gen-param edit so the engine's view
// stays in sync with Working State through the real cross-core path.
void sendGenParamsFrom(uint8_t patternIndex, const PitchSetConfig& pitch,
                       const GateSetConfig& gate, const VelocitySetConfig& vel) {
    common::GenerationParameters gp;
    gp.scale = pitch.scale;
    gp.scaleRoot = pitch.scaleRoot;
    gp.quantize = pitch.quantize;
    gp.patternProbability = gate.patternProbability;
    gp.stepLength = noteLengthTicks(gate.noteLength);
    const uint8_t n = gate.steps < common::GenerationParameters::kMaxSteps
                          ? gate.steps : common::GenerationParameters::kMaxSteps;
    for (uint8_t i = 0; i < common::GenerationParameters::kMaxSteps; ++i) {
        gp.stepProbability[i] =
            (i < n) ? gate.stepProbability[i] : common::STEP_PROB_INHERIT;
    }
    gp.humanizeRange = vel.humanizeRange;
    gp.seedSet = vel.seedSet;
    gp.seed = vel.seed;
    gp.coupling = vel.coupling;

    commands::sendGenParams(patternIndex, gp);
}

// Committed gen-params (commit, ESC/undo, leaving an editor, boot restore).
void sendPatternGenParams(const UIState& state, uint8_t patternIndex) {
    if (patternIndex >= state.patternCount) return;
    sendGenParamsFrom(patternIndex, state.pitchSetOf(patternIndex),
                      state.gateSetOf(patternIndex),
                      state.velocitySetOf(patternIndex));
}

// Live preview while editing: the one stream being edited contributes its DRAFT,
// the other two their committed configs. Gen-param fields then sound as soon as
// they change, exactly like gate/pitch/velocity material edits already do; ENTER
// keeps them, ESC sends the committed values back.
void previewGateGenParams(const UIState& s) {
    const uint8_t slot = s.selectedGateSlot;
    BatchScope batch(userCount(s, SetType::GATE, slot));
    sendGenParamsToUsers(s, SetType::GATE, slot, &s.gateSetDraft, nullptr, nullptr);
}
void previewPitchGenParams(const UIState& s) {
    const uint8_t slot = s.selectedNoteSlot;
    BatchScope batch(userCount(s, SetType::NOTE, slot));
    sendGenParamsToUsers(s, SetType::NOTE, slot, nullptr, &s.pitchSetDraft, nullptr);
}
void previewVelocityGenParams(const UIState& s) {
    const uint8_t slot = s.selectedVelocitySlot;
    BatchScope batch(userCount(s, SetType::VELOCITY, slot));
    sendGenParamsToUsers(s, SetType::VELOCITY, slot, nullptr, nullptr, &s.velocitySetDraft);
}

void restorePitchSetDraft(UIState& state) {
    const uint8_t slot = state.selectedNoteSlot;
    if (!state.pitchSetDirty || !slotOccupied(state, SetType::NOTE, slot)) return;
    state.pitchSetDraft = state.noteLib.slots[slot];
    state.pitchSetDirty = false;
    BatchScope batch(2 * userCount(state, SetType::NOTE, slot));
    sendPitchToUsers(state, slot, state.pitchSetDraft);
    sendGenParamsToUsers(state, SetType::NOTE, slot, nullptr, nullptr, nullptr);
}

void restoreVelocitySetDraft(UIState& state) {
    const uint8_t slot = state.selectedVelocitySlot;
    if (!state.velocitySetDirty || !slotOccupied(state, SetType::VELOCITY, slot)) return;
    state.velocitySetDraft = state.velocityLib.slots[slot];
    state.velocitySetDirty = false;
    BatchScope batch(2 * userCount(state, SetType::VELOCITY, slot));
    sendVelocityToUsers(state, slot, state.velocitySetDraft);
    sendGenParamsToUsers(state, SetType::VELOCITY, slot, nullptr, nullptr, nullptr);
}

}

void setBpm(UIState& state, int bpm) {
    state.bpm = std::max(40, std::min(255, bpm));
    commands::sendCommand(commands::Command::BPM_SET, state.bpm);
}

void setPlaying(UIState& state, bool playing) {
    state.playing = playing;
    if (playing) {
        commands::sendCommand(commands::Command::PLAY);
    } else {
        commands::sendCommand(commands::Command::STOP);
    }
}

void setValue(UIState& state, int value) {
    state.value = std::max(0, std::min(99, value));
}

void setCurrentView(UIState& state, ViewId viewId) {
    state.gotoPicker.active = false;  // a view change always closes the go-to picker
    if (state.currentView == ViewId::GATE_SET && viewId != ViewId::GATE_SET) {
        restoreGateSetDraft(state);
    }
    if (state.currentView == ViewId::PITCH_SET && viewId != ViewId::PITCH_SET) {
        restorePitchSetDraft(state);
    }
    if (state.currentView == ViewId::VELOCITY_SET && viewId != ViewId::VELOCITY_SET) {
        restoreVelocitySetDraft(state);
    }
    state.currentView = viewId;
}

void beginGateSetEdit(UIState& state) {
    if (!slotOccupied(state, SetType::GATE, state.selectedGateSlot)) return;
    state.gateSetDraft = state.gateLib.slots[state.selectedGateSlot];
    // Seed the per-step gate states from the generator if they were never set
    // (all-zero but the generator would produce pulses) — e.g. a config created
    // before the R9 grid existed, or a fresh default. A config with manual edits
    // already has non-zero gateStates and is left as-is.
    bool anyGate = false;
    for (uint8_t i = 0; i < MAX_GRID_STEPS; ++i) {
        if (state.gateSetDraft.gateStates[i]) { anyGate = true; break; }
    }
    if (!anyGate && state.gateSetDraft.pulses > 0) {
        regenerateGateStates(state.gateSetDraft);
    }
    state.selectedGateSetProperty = GateSetProperty::ALGORITHM;
    state.selectedGateStep = 0;
    state.gateSetDirty = false;
}

void moveGateSetProperty(UIState& state, int direction) {
    const GateSetProperty* properties = nullptr;
    size_t propertyCount = 0;
    switch (state.gateSetDraft.algorithm) {
        case GateAlgorithm::EUCLIDEAN:
            properties = EUCLIDEAN_PROPERTIES;
            propertyCount = sizeof(EUCLIDEAN_PROPERTIES) / sizeof(EUCLIDEAN_PROPERTIES[0]);
            break;
    }

    // Within the STEP_GRID, LEFT/RIGHT walks the step cursor across the
    // programmed steps (R9). Moving right past the last step clamps; moving left
    // past step 0 LEAVES the grid back to the previous generator property.
    if (state.selectedGateSetProperty == GateSetProperty::STEP_GRID) {
        const uint8_t gridSteps =
            state.gateSetDraft.steps < MAX_GRID_STEPS ? state.gateSetDraft.steps : MAX_GRID_STEPS;
        const int nextStep = static_cast<int>(state.selectedGateStep) + direction;
        if (nextStep < 0) {
            // Exit the grid to the property just before STEP_GRID.
            if (propertyCount >= 2) state.selectedGateSetProperty = properties[propertyCount - 2];
            return;
        }
        state.selectedGateStep = static_cast<uint8_t>(
            std::clamp(nextStep, 0, static_cast<int>(gridSteps) - 1));
        return;
    }

    size_t index = 0;
    while (index < propertyCount && properties[index] != state.selectedGateSetProperty) index++;
    if (index == propertyCount) index = 0;
    const int next = std::clamp(static_cast<int>(index) + direction, 0,
                                static_cast<int>(propertyCount) - 1);
    state.selectedGateSetProperty = properties[next];
    // Entering the grid lands on step 0.
    if (state.selectedGateSetProperty == GateSetProperty::STEP_GRID) {
        state.selectedGateStep = 0;
    }
}

void adjustGateSetValue(UIState& state, int delta, bool coarse) {
    if (!slotOccupied(state, SetType::GATE, state.selectedGateSlot) || delta == 0) return;
    // Coarse step multiplies the step by 10 for numeric fields. NOTE_LENGTH is
    // an enum stepped one position at a time, so it ignores the coarse flag.
    const int step = coarse && state.selectedGateSetProperty != GateSetProperty::NOTE_LENGTH
                         ? delta * 10
                         : delta;
    GateSetConfig next = state.gateSetDraft;

    switch (next.algorithm) {
        case GateAlgorithm::EUCLIDEAN:
            switch (state.selectedGateSetProperty) {
                case GateSetProperty::ALGORITHM: return;
                case GateSetProperty::STEPS:
                    next.steps = std::clamp(static_cast<int>(next.steps) + step, 1, 64);
                    break;
                case GateSetProperty::PULSES:
                    next.pulses = std::clamp(static_cast<int>(next.pulses) + step, 0,
                                             static_cast<int>(next.steps));
                    break;
                case GateSetProperty::ROTATION:
                    next.rotation = std::clamp(static_cast<int>(next.rotation) + step, 0,
                                               static_cast<int>(next.steps) - 1);
                    break;
                case GateSetProperty::NOTE_LENGTH:
                    next.noteLength = static_cast<NoteLength>(std::clamp(
                        static_cast<int>(next.noteLength) + delta,
                        static_cast<int>(NoteLength::THIRTY_SECOND),
                        static_cast<int>(NoteLength::WHOLE)));
                    break;
                case GateSetProperty::LENGTH:
                    next.gateLength = std::clamp(static_cast<int>(next.gateLength) + step, 0, 100);
                    break;
                case GateSetProperty::PROBABILITY:
                    // Per-Pattern Step Probability (R2/R10), 0..100; 100 == today.
                    next.patternProbability = static_cast<uint8_t>(
                        std::clamp(static_cast<int>(next.patternProbability) + step, 0, 100));
                    break;
                case GateSetProperty::STEP_GRID: {
                    // UP/DOWN cycles the SELECTED step's probability through the
                    // ladder INHERIT -> 0 -> 10 -> ... -> 100 (R9.4). INHERIT is a
                    // distinct state below 0 (R9.5). Coarse has no effect (discrete
                    // ladder). Gate TOGGLE is a separate action (toggleGateStep).
                    const uint8_t s = state.selectedGateStep;
                    if (s >= MAX_GRID_STEPS) return;
                    const uint8_t cur = next.stepProbability[s];
                    // Represent the ladder as an index: -1 == INHERIT, 0..10 == 0..100%.
                    int idx;
                    if (cur == STEP_PROB_INHERIT) idx = -1;
                    else idx = std::clamp(static_cast<int>(cur) / 10, 0, 10);
                    idx = std::clamp(idx + delta, -1, 10);
                    next.stepProbability[s] =
                        (idx < 0) ? STEP_PROB_INHERIT : static_cast<uint8_t>(idx * 10);
                    break;
                }
            }
            // Generator properties reseed the per-step GATE states (R9.3), but
            // PRESERVE per-step probability (handled by regenerateGateStates,
            // which touches only gateStates). NOTE_LENGTH/LENGTH/STEP_GRID do not
            // change which steps pulse, so they do not regenerate.
            if (state.selectedGateSetProperty == GateSetProperty::STEPS ||
                state.selectedGateSetProperty == GateSetProperty::PULSES ||
                state.selectedGateSetProperty == GateSetProperty::ROTATION) {
                clampEuclidean(next);
                regenerateGateStates(next);
            } else {
                clampEuclidean(next);
            }
            break;
    }

    if (next == state.gateSetDraft) return;
    state.gateSetDraft = next;
    state.gateSetDirty = next != state.gateLib.slots[state.selectedGateSlot];
    const GateSetProperty prop = state.selectedGateSetProperty;
    // Probability edits (per-Pattern PRB, per-step grid probability) do not change
    // the gate array. Resending it would rebuild core1's GateSet at step 0 and
    // fire that step's note on every scrub (bench report 2026-11-20), so only a
    // gate-structure change sends the array.
    if (prop != GateSetProperty::PROBABILITY && prop != GateSetProperty::STEP_GRID) {
        BatchScope batch(userCount(state, SetType::GATE, state.selectedGateSlot));
        sendGateToUsers(state, state.selectedGateSlot, next);
    }
    // Fields that feed the gen-params (probabilities; STEPS bounds the per-step
    // array; NOTE_LENGTH sets stepLength) preview live instead of on ENTER only.
    if (prop == GateSetProperty::PROBABILITY || prop == GateSetProperty::STEP_GRID ||
        prop == GateSetProperty::STEPS || prop == GateSetProperty::NOTE_LENGTH) {
        previewGateGenParams(state);
    }
}

// R9.2: toggle the SELECTED programmed step's gate on/off (manual editing),
// layered on top of the generator seed. Routed through the same draft/dirty/send
// path as every other GateSet edit.
void toggleGateStep(UIState& state) {
    if (!slotOccupied(state, SetType::GATE, state.selectedGateSlot)) return;
    if (state.selectedGateSetProperty != GateSetProperty::STEP_GRID) return;
    const uint8_t s = state.selectedGateStep;
    if (s >= MAX_GRID_STEPS) return;
    GateSetConfig next = state.gateSetDraft;
    next.gateStates[s] = next.gateStates[s] ? 0 : 1;
    state.gateSetDraft = next;
    state.gateSetDirty = next != state.gateLib.slots[state.selectedGateSlot];
    BatchScope batch(userCount(state, SetType::GATE, state.selectedGateSlot));
    sendGateToUsers(state, state.selectedGateSlot, next);
}

void commitGateSetEdit(UIState& state) {
    const uint8_t slot = state.selectedGateSlot;
    if (!slotOccupied(state, SetType::GATE, slot)) return;
    state.gateLib.slots[slot] = state.gateSetDraft;
    state.gateSetDirty = false;
    // task 10.4: transmit the full gen-params of every pattern using this set on commit
    // (gate carries per-Pattern/per-step probability + stepLength).
    BatchScope batch(userCount(state, SetType::GATE, slot));
    sendGenParamsToUsers(state, SetType::GATE, slot, nullptr, nullptr, nullptr);
}

void undoGateSetEdit(UIState& state) {
    restoreGateSetDraft(state);
}

void syncGateSet(const UIState& state, uint8_t index) {
    if (index < state.patternCount) sendGateSet(index, state.gateSetOf(index));
}

void beginPitchSetEdit(UIState& state) {
    if (!slotOccupied(state, SetType::NOTE, state.selectedNoteSlot)) return;
    state.pitchSetDraft = state.noteLib.slots[state.selectedNoteSlot];
    state.selectedPitchSetField = PITCH_SET_FIELD_COUNT;
    state.pitchSetDirty = false;
}

void movePitchSetField(UIState& state, int direction) {
    const uint8_t lastField = PITCH_SET_FIELD_PITCH_BASE + state.pitchSetDraft.count - 1;
    int next = static_cast<int>(state.selectedPitchSetField) + direction;
    next = std::clamp(next, static_cast<int>(PITCH_SET_FIELD_SCALE),
                      static_cast<int>(lastField));
    state.selectedPitchSetField = static_cast<uint8_t>(next);
}

void adjustPitchSetValue(UIState& state, int delta, bool coarse) {
    if (!slotOccupied(state, SetType::NOTE, state.selectedNoteSlot) || delta == 0) return;
    PitchSetConfig next = state.pitchSetDraft;

    if (state.selectedPitchSetField == PITCH_SET_FIELD_SCALE) {
        // SCALE cycles the enum one position at a time (coarse has no effect).
        // CHROMATIC (0) is the behavior-preserving default / clear-to-off.
        constexpr int kScaleCount = 3;  // CHROMATIC, MAJOR, MINOR_NATURAL
        const int s = std::clamp(static_cast<int>(next.scale) + delta, 0, kScaleCount - 1);
        next.scale = static_cast<common::ScaleId>(s);
    } else if (state.selectedPitchSetField == PITCH_SET_FIELD_ROOT) {
        // ROOT wraps 0..11; coarse has no effect (12 values only).
        int r = (static_cast<int>(next.scaleRoot) + delta) % 12;
        if (r < 0) r += 12;
        next.scaleRoot = static_cast<uint8_t>(r);
    } else if (state.selectedPitchSetField == PITCH_SET_FIELD_QUANTIZE) {
        // QUANTIZE cycles NEAREST/DOWN/UP/SKIP; NEAREST (0) is the default.
        constexpr int kQuantCount = 4;
        const int q = std::clamp(static_cast<int>(next.quantize) + delta, 0, kQuantCount - 1);
        next.quantize = static_cast<common::QuantizeMode>(q);
    } else if (state.selectedPitchSetField == PITCH_SET_FIELD_COUNT) {
        const int step = coarse ? delta * 10 : delta;
        next.count = std::clamp(static_cast<int>(next.count) + step, 1,
                                static_cast<int>(MAX_PITCHES));
        if (next.count > state.pitchSetDraft.count) {
            const uint8_t fillPitch = state.pitchSetDraft.pitches[state.pitchSetDraft.count - 1];
            for (uint8_t i = state.pitchSetDraft.count; i < next.count; i++) {
                next.pitches[i] = fillPitch;
            }
        }
    } else if (state.selectedPitchSetField == PITCH_SET_FIELD_ORDER) {
        // ORDER is an enum stepped one position at a time; coarse has no effect.
        const int orderInt = static_cast<int>(next.order) + delta;
        next.order = static_cast<common::PlayingOrder>(
            std::clamp(orderInt,
                       static_cast<int>(common::PlayingOrder::FORWARDS),
                       static_cast<int>(common::PlayingOrder::RANDOM)));
    } else {
        // Coarse shifts by a full octave (12 semitones).
        const int step = coarse ? delta * 12 : delta;
        const uint8_t pitchIndex = state.selectedPitchSetField - PITCH_SET_FIELD_PITCH_BASE;
        if (pitchIndex >= next.count) return;
        next.pitches[pitchIndex] = static_cast<uint8_t>(
            std::clamp(static_cast<int>(next.pitches[pitchIndex]) + step, 0, 127));
    }

    if (next == state.pitchSetDraft) return;
    state.pitchSetDraft = next;
    state.pitchSetDirty = next != state.noteLib.slots[state.selectedNoteSlot];
    if (state.selectedPitchSetField <= PITCH_SET_FIELD_QUANTIZE) {
        previewPitchGenParams(state);  // scale/root/quantize: audible immediately
    } else {
        BatchScope batch(userCount(state, SetType::NOTE, state.selectedNoteSlot));
        sendPitchToUsers(state, state.selectedNoteSlot, next);
    }
}

void commitPitchSetEdit(UIState& state) {
    const uint8_t slot = state.selectedNoteSlot;
    if (!slotOccupied(state, SetType::NOTE, slot)) return;
    state.noteLib.slots[slot] = state.pitchSetDraft;
    state.pitchSetDirty = false;
    // task 10.4: pitch carries the scale constraint; transmit gen-params on commit.
    BatchScope batch(userCount(state, SetType::NOTE, slot));
    sendGenParamsToUsers(state, SetType::NOTE, slot, nullptr, nullptr, nullptr);
}

void undoPitchSetEdit(UIState& state) {
    restorePitchSetDraft(state);
}

void syncPitchSet(const UIState& state, uint8_t index) {
    if (index < state.patternCount) sendPitchSetConfig(index, state.pitchSetOf(index));
}

void beginVelocitySetEdit(UIState& state) {
    if (!slotOccupied(state, SetType::VELOCITY, state.selectedVelocitySlot)) return;
    state.velocitySetDraft = state.velocityLib.slots[state.selectedVelocitySlot];
    state.selectedVelocitySetField = VELOCITY_SET_FIELD_COUNT;
    state.velocitySetDirty = false;
}

void moveVelocitySetField(UIState& state, int direction) {
    const uint8_t lastField = VELOCITY_SET_FIELD_VELOCITY_BASE + state.velocitySetDraft.count - 1;
    int next = static_cast<int>(state.selectedVelocitySetField) + direction;
    next = std::clamp(next, static_cast<int>(VELOCITY_SET_FIELD_HUMANIZE),
                      static_cast<int>(lastField));
    state.selectedVelocitySetField = static_cast<uint8_t>(next);
}

void adjustVelocitySetValue(UIState& state, int delta, bool coarse) {
    if (!slotOccupied(state, SetType::VELOCITY, state.selectedVelocitySlot) || delta == 0) return;
    VelocitySetConfig next = state.velocitySetDraft;

    if (state.selectedVelocitySetField == VELOCITY_SET_FIELD_HUMANIZE) {
        // HUMANIZE range 0..64; 0 == off (behavior-preserving, clear-to-default).
        const int step = coarse ? delta * 10 : delta;
        next.humanizeRange = static_cast<uint8_t>(
            std::clamp(static_cast<int>(next.humanizeRange) + step, 0, 64));
    } else if (state.selectedVelocitySetField == VELOCITY_SET_FIELD_SEED) {
        // SEED: a single control that spans "no seed" (seedSet=false) then an
        // explicit seed 0,1,2,... Going below 0 clears to no-seed (R10.3 / R4).
        // coarse steps by 10. The displayed/stored seed is the low byte space
        // here; the full 32-bit seed is set via the host/transport path, but the
        // UI exposes a simple incrementing seed sufficient to make a Pattern
        // deterministic and to clear it.
        const int step = coarse ? delta * 10 : delta;
        if (!next.seedSet) {
            if (delta > 0) { next.seedSet = true; next.seed = 0; }
            // delta < 0 while already no-seed: stay cleared.
        } else {
            const long s = static_cast<long>(next.seed) + step;
            if (s < 0) { next.seedSet = false; next.seed = 0; }  // clear to no-seed
            else next.seed = static_cast<uint32_t>(s);
        }
    } else if (state.selectedVelocitySetField == VELOCITY_SET_FIELD_COUPLING) {
        // COUPLING cycles INDEPENDENT/LOCKED; INDEPENDENT (0) is the default.
        constexpr int kCouplingCount = 2;
        const int c = std::clamp(static_cast<int>(next.coupling) + delta, 0, kCouplingCount - 1);
        next.coupling = static_cast<common::CouplingMode>(c);
    } else if (state.selectedVelocitySetField == VELOCITY_SET_FIELD_COUNT) {
        const int step = coarse ? delta * 10 : delta;
        next.count = static_cast<uint8_t>(std::clamp(
            static_cast<int>(next.count) + step, 1, static_cast<int>(MAX_VELOCITIES)));
        if (next.count > state.velocitySetDraft.count) {
            const uint8_t fillVel = state.velocitySetDraft.velocities[state.velocitySetDraft.count - 1];
            for (uint8_t i = state.velocitySetDraft.count; i < next.count; i++) {
                next.velocities[i] = fillVel;
            }
        }
    } else if (state.selectedVelocitySetField == VELOCITY_SET_FIELD_ORDER) {
        // ORDER is an enum stepped one position at a time; coarse has no effect.
        const int orderInt = static_cast<int>(next.order) + delta;
        next.order = static_cast<common::PlayingOrder>(
            std::clamp(orderInt,
                       static_cast<int>(common::PlayingOrder::FORWARDS),
                       static_cast<int>(common::PlayingOrder::RANDOM)));
    } else {
        const int step = coarse ? delta * 10 : delta;
        const uint8_t velIndex = state.selectedVelocitySetField - VELOCITY_SET_FIELD_VELOCITY_BASE;
        if (velIndex >= next.count) return;
        next.velocities[velIndex] = static_cast<uint8_t>(
            std::clamp(static_cast<int>(next.velocities[velIndex]) + step, 0, 127));
    }

    if (next == state.velocitySetDraft) return;
    state.velocitySetDraft = next;
    state.velocitySetDirty = next != state.velocityLib.slots[state.selectedVelocitySlot];
    if (state.selectedVelocitySetField <= VELOCITY_SET_FIELD_COUPLING) {
        previewVelocityGenParams(state);  // humanize/seed/coupling: audible immediately
    } else {
        BatchScope batch(userCount(state, SetType::VELOCITY, state.selectedVelocitySlot));
        sendVelocityToUsers(state, state.selectedVelocitySlot, next);
    }
}

void commitVelocitySetEdit(UIState& state) {
    const uint8_t slot = state.selectedVelocitySlot;
    if (!slotOccupied(state, SetType::VELOCITY, slot)) return;
    state.velocityLib.slots[slot] = state.velocitySetDraft;
    state.velocitySetDirty = false;
    // task 10.4: velocity carries humanize/seed/coupling; transmit on commit.
    BatchScope batch(userCount(state, SetType::VELOCITY, slot));
    sendGenParamsToUsers(state, SetType::VELOCITY, slot, nullptr, nullptr, nullptr);
}

void undoVelocitySetEdit(UIState& state) {
    restoreVelocitySetDraft(state);
}

void syncVelocitySet(const UIState& state, uint8_t index) {
    if (index < state.patternCount) sendVelocitySetConfig(index, state.velocitySetOf(index));
}

void setSelectedPattern(UIState& state, uint8_t index) {
    state.selectedPattern = std::min(index, static_cast<uint8_t>(state.patternCount));
}

void setSelectedPatternSet(UIState& state, PatternSet patternSet) {
    state.selectedPatternSet = patternSet;
}

void addPattern(UIState& state) {
    if (state.patternCount >= MAX_PATTERNS) return;
    const uint8_t newPattern = state.patternCount;
    // A new pattern references the lowest occupied slot of each library (R2.4).
    state.patternRefs[newPattern] = PatternRefs{lowestOccupied(state, SetType::GATE),
                                                lowestOccupied(state, SetType::NOTE),
                                                lowestOccupied(state, SetType::VELOCITY)};
    state.patternCount++;
    state.activePatterns |= (1 << newPattern);
    state.selectedPattern = newPattern;
    commands::sendCommand(commands::Command::PATTERN_ADD);
    syncGateSet(state, newPattern);
    syncPitchSet(state, newPattern);
    syncVelocitySet(state, newPattern);
    // The new pattern may reference sets carrying non-default gen-params (scale, humanize,
    // probability); core1 starts it on defaults, so send the referenced ones (R9.3).
    sendPatternGenParams(state, newPattern);
}

void removePattern(UIState& state, uint8_t index) {
    if (state.patternCount <= 1 || index >= state.patternCount) return;

    const uint16_t lowerMask = index == 0 ? 0 : static_cast<uint16_t>((1u << index) - 1);
    const uint16_t lower = state.activePatterns & lowerMask;
    const uint16_t upper = state.activePatterns >> (index + 1);
    state.activePatterns = lower | static_cast<uint16_t>(upper << index);
    // Removing a pattern drops only its references; the libraries are untouched (R2.5).
    for (uint8_t i = index; i + 1 < state.patternCount; i++) {
        state.patternRefs[i] = state.patternRefs[i + 1];
    }
    state.patternRefs[state.patternCount - 1] = PatternRefs{};
    state.patternCount--;
    if (state.selectedPattern > index) state.selectedPattern--;
    state.selectedPattern = std::min(state.selectedPattern,
                                     static_cast<uint8_t>(state.patternCount - 1));
    commands::sendCommand(commands::Command::PATTERN_REMOVE, index);
}

void togglePatternActive(UIState& state, uint8_t index) {
    if (index >= state.patternCount) return;
    state.activePatterns ^= (1 << index);
    if (state.activePatterns & (1 << index)) {
        commands::sendCommand(commands::Command::PATTERN_ACTIVATE, index);
    } else {
        commands::sendCommand(commands::Command::PATTERN_DEACTIVATE, index);
    }
}

namespace {

uint8_t& selectedSlotRef(UIState& s, SetType type) {
    switch (type) {
        case SetType::GATE: return s.selectedGateSlot;
        case SetType::NOTE: return s.selectedNoteSlot;
        default:            return s.selectedVelocitySlot;
    }
}

bool draftDirty(const UIState& s, SetType type) {
    switch (type) {
        case SetType::GATE: return s.gateSetDirty;
        case SetType::NOTE: return s.pitchSetDirty;
        default:            return s.velocitySetDirty;
    }
}

void discardDraft(UIState& s, SetType type) {
    switch (type) {
        case SetType::GATE:     restoreGateSetDraft(s); break;
        case SetType::NOTE:     restorePitchSetDraft(s); break;
        default:                restoreVelocitySetDraft(s); break;
    }
}

void beginEdit(UIState& s, SetType type) {
    switch (type) {
        case SetType::GATE:     beginGateSetEdit(s); break;
        case SetType::NOTE:     beginPitchSetEdit(s); break;
        default:                beginVelocitySetEdit(s); break;
    }
}

}  // namespace

void selectSlotsOfSelectedPattern(UIState& state) {
    if (state.selectedPattern >= state.patternCount) return;
    const PatternRefs& refs = state.patternRefs[state.selectedPattern];
    state.selectedGateSlot = refs.gate;
    state.selectedNoteSlot = refs.note;
    state.selectedVelocitySlot = refs.velocity;
}

void selectSetSlot(UIState& state, SetType type, uint8_t slot) {
    if (!slotOccupied(state, type, slot)) return;
    discardDraft(state, type);  // R4.6: leaving a slot drops its uncommitted draft
    selectedSlotRef(state, type) = slot;
    beginEdit(state, type);
    state.setNotice = SetNotice::NONE;
}

void stepSetSlot(UIState& state, SetType type, int direction) {
    selectSetSlot(state, type, nextOccupied(state, type, selectedSlotRef(state, type), direction));
}

void createSetInView(UIState& state, SetType type) {
    uint8_t slot = 0;
    // Check before discarding so a refused create keeps the draft.
    if (occupiedMask(state, type) == 0xFFFF) {
        state.setNotice = SetNotice::LIBRARY_FULL;
        return;
    }
    discardDraft(state, type);
    if (!createSet(state, type, slot)) return;
    selectedSlotRef(state, type) = slot;
    beginEdit(state, type);
    state.setNotice = SetNotice::NONE;
}

void copySetInView(UIState& state, SetType type) {
    uint8_t slot = 0;
    const uint8_t src = selectedSlotRef(state, type);
    if (occupiedMask(state, type) == 0xFFFF) {
        state.setNotice = SetNotice::LIBRARY_FULL;
        return;
    }
    // Copy the committed set; an uncommitted draft is discarded first (R4.6).
    discardDraft(state, type);
    if (!copySet(state, type, src, slot)) return;
    selectedSlotRef(state, type) = slot;
    beginEdit(state, type);
    state.setNotice = SetNotice::NONE;
}

void deleteSetInView(UIState& state, SetType type) {
    const uint8_t slot = selectedSlotRef(state, type);
    if (!slotOccupied(state, type, slot)) return;
    if (usersOf(state, type, slot) != 0) {
        state.setNotice = SetNotice::IN_USE;
        return;
    }
    if (popcount16(occupiedMask(state, type)) <= 1) {
        state.setNotice = SetNotice::LAST_SET;
        return;
    }
    const uint8_t target = nextOccupied(state, type, slot, +1);
    discardDraft(state, type);  // no users, so nothing is sent
    if (!deleteSet(state, type, slot)) return;
    selectedSlotRef(state, type) = target;
    beginEdit(state, type);
    state.setNotice = SetNotice::NONE;
}

void assignSetRef(UIState& state, uint8_t pattern, SetType type, uint8_t slot) {
    if (pattern >= state.patternCount) return;
    if (!assignRef(state, pattern, type, slot)) {
        return;  // free or out-of-range slot: the previous reference is kept (R2.3)
    }
    // One atomic update for this pattern: the new set's material plus its gen-params, which
    // combine all three of its sets (R5.3, R8.3).
    BatchScope batch(2);
    switch (type) {
        case SetType::GATE:     sendGateSet(pattern, state.gateSetOf(pattern)); break;
        case SetType::NOTE:     sendPitchSetConfig(pattern, state.pitchSetOf(pattern)); break;
        default:                sendVelocitySetConfig(pattern, state.velocitySetOf(pattern)); break;
    }
    sendPatternGenParams(state, pattern);
}

void stepSetRef(UIState& state, uint8_t pattern, SetType type, int direction) {
    if (pattern >= state.patternCount) return;
    const PatternRefs& r = state.patternRefs[pattern];
    const uint8_t cur = type == SetType::GATE ? r.gate : type == SetType::NOTE ? r.note : r.velocity;
    assignSetRef(state, pattern, type, nextOccupied(state, type, cur, direction));
}

namespace {

void jumpToPattern(UIState& state, uint8_t pattern) {
    state.gotoPicker.active = false;
    state.selectedPattern = pattern;
    // Leaving a set view discards its uncommitted draft (setCurrentView restores it).
    setCurrentView(state, ViewId::PATTERNS);
}

}  // namespace

void openGotoPattern(UIState& state, SetType type) {
    uint8_t slot = state.selectedGateSlot;
    if (type == SetType::NOTE) slot = state.selectedNoteSlot;
    else if (type == SetType::VELOCITY) slot = state.selectedVelocitySlot;

    const uint16_t users = usersOf(state, type, slot);
    state.setNotice = SetNotice::NONE;
    if (users == 0) {
        state.setNotice = SetNotice::UNUSED;  // R10.6
        return;
    }
    if (popcount16(users) == 1) {
        for (uint8_t p = 0; p < MAX_PATTERNS; ++p) {
            if ((users >> p) & 1u) { jumpToPattern(state, p); return; }  // R10.7
        }
    }
    uint8_t cursor = state.selectedPattern;
    if (!((users >> cursor) & 1u)) {
        cursor = 0;
        while (!((users >> cursor) & 1u)) ++cursor;  // lowest candidate (users != 0)
    }
    state.gotoPicker.active = true;
    state.gotoPicker.type = type;
    state.gotoPicker.candidates = users;
    state.gotoPicker.cursor = cursor;
}

void gotoPickerMove(UIState& state, int direction) {
    GotoPicker& g = state.gotoPicker;
    if (!g.active || g.candidates == 0) return;
    const int step = direction < 0 ? -1 : 1;
    int p = g.cursor;
    for (uint8_t i = 0; i < MAX_PATTERNS; ++i) {
        p = (p + step + MAX_PATTERNS) % MAX_PATTERNS;
        if ((g.candidates >> p) & 1u) { g.cursor = static_cast<uint8_t>(p); return; }
    }
}

void gotoPickerConfirm(UIState& state) {
    if (!state.gotoPicker.active) return;
    jumpToPattern(state, state.gotoPicker.cursor);
}

void gotoPickerCancel(UIState& state) {
    state.gotoPicker.active = false;
}

void restoreSequencerFromState(const UIState& state) {
    // core1 launches with a single default pattern (index 0). Grow its pattern
    // vector to match the restored count using the same command the UI uses
    // when the user adds a pattern.
    for (uint8_t i = 1; i < state.patternCount; i++) {
        commands::sendCommand(commands::Command::PATTERN_ADD);
    }

    // Push each pattern's durable config down the existing sync path. This
    // regenerates the euclidean gate vector and sets pitch/velocity sets on
    // core1, exactly as UIController::initialize seeds pattern 0 today.
    for (uint8_t i = 0; i < state.patternCount; i++) {
        syncGateSet(state, i);
        syncPitchSet(state, i);
        syncVelocitySet(state, i);
        // Generation Parameters ride their own GEN_PARAMS_SET command (task
        // 10.4), not the per-stream sync above. Without this, a boot load left
        // core1 running DEFAULT gen-params while Working State (and the UI)
        // showed the saved ones -- a saved seed/probability/humanize/scale never
        // took effect after reboot (task 11.3 silicon finding; R4.3, R6.5).
        sendPatternGenParams(state, i);
    }

    // Align core1's active mask with the restored activePatterns bitmask. Every
    // pattern defaults to active on core1 after PATTERN_ADD, so explicitly
    // activate/deactivate each index to match the saved state.
    for (uint8_t i = 0; i < state.patternCount; i++) {
        if ((state.activePatterns >> i) & 0x1) {
            commands::sendCommand(commands::Command::PATTERN_ACTIVATE, i);
        } else {
            commands::sendCommand(commands::Command::PATTERN_DEACTIVATE, i);
        }
    }

    commands::sendCommand(commands::Command::BPM_SET, state.bpm);
}

namespace {

constexpr uint8_t F1_USAGE  = static_cast<uint8_t>(KeyId::F1);
constexpr uint8_t F12_USAGE = static_cast<uint8_t>(KeyId::F12);

// F1 -> INIT, F2 -> SETTINGS. Keys past the end of this table are reserved
// for future views: consumed, traced, but without effect.
constexpr ViewId FUNCTION_KEY_VIEWS[] = { ViewId::INIT, ViewId::SETTINGS, ViewId::PATTERNS };

bool isFunctionKey(KeyId id) {
    const uint8_t usage = static_cast<uint8_t>(id);
    return usage >= F1_USAGE && usage <= F12_USAGE;
}

// Reserved by key, independent of modifiers, so shift+F1 is swallowed too
// and modified function keys stay available for future global bindings.
bool isReserved(KeyId id) {
    return id == KeyId::SPACE || isFunctionKey(id);
}

// Recognize the global Save_Combo (Ctrl+S) explicitly. Matched on a key press
// with the CTRL modifier bit set (other modifiers are ignored). Req 1.1.
bool isSaveCombo(const events::Event& e) {
    return e.type == events::EventType::KEY_PRESSED &&
           e.data.key.id == KeyId::S &&
           (e.data.key.mods & mod::CTRL) != 0;
}

// Pure intent recorder: flags a save request for the core0 UI loop to drain.
// No flash access, no blocking - the reducer never performs the save itself.
void requestSave(UIState& state) {
    state.saveRequested = true;
}

void applyGlobal(UIState& state, KeyId id) {
    if (id == KeyId::SPACE) {
        setPlaying(state, !state.playing);
        return;
    }

    const size_t index = static_cast<size_t>(static_cast<uint8_t>(id) - F1_USAGE);
    if (index < (sizeof(FUNCTION_KEY_VIEWS) / sizeof(FUNCTION_KEY_VIEWS[0]))) {
        setCurrentView(state, FUNCTION_KEY_VIEWS[index]);
    } else {
        printf("F%u pressed - no view bound\n", static_cast<unsigned>(index + 1));
    }
}

} // namespace

void reduce(UIState& state, const events::Event& event, ui::IView* activeView) {
    if (isSaveCombo(event)) {
        requestSave(state);         // record intent; UI loop performs the save
        return;                     // consumed globally; never routed to a view (Req 1.2)
    }

    if (isReserved(event.data.key.id)) {
        if (event.type == events::EventType::KEY_PRESSED &&
            event.data.key.mods == mod::NONE) {
            applyGlobal(state, event.data.key.id);
        }
        return;    // consumed either way - never reaches a view
    }

    // While the go-to picker is open it owns the keyboard: move, confirm, cancel; every other
    // key is consumed so the set underneath cannot change (R10, design section 4).
    if (state.gotoPicker.active) {
        const bool pressed = event.type == events::EventType::KEY_PRESSED;
        const bool movable = pressed || event.type == events::EventType::KEY_HELD;
        switch (combo(event.data.key.id, event.data.key.mods)) {
            case combo(KeyId::LEFT):
            case combo(KeyId::UP):    if (movable) gotoPickerMove(state, -1); break;
            case combo(KeyId::RIGHT):
            case combo(KeyId::DOWN):  if (movable) gotoPickerMove(state, +1); break;
            case combo(KeyId::ENTER): if (pressed) gotoPickerConfirm(state); break;
            case combo(KeyId::ESCAPE): if (pressed) gotoPickerCancel(state); break;
            default: break;
        }
        return;
    }

    // Ctrl+G in a set view: go to a pattern that uses the selected set (R10.1).
    if (event.type == events::EventType::KEY_PRESSED &&
        combo(event.data.key.id, event.data.key.mods) == combo(KeyId::G, mod::CTRL)) {
        switch (state.currentView) {
            case ViewId::GATE_SET:     openGotoPattern(state, SetType::GATE); return;
            case ViewId::PITCH_SET:    openGotoPattern(state, SetType::NOTE); return;
            case ViewId::VELOCITY_SET: openGotoPattern(state, SetType::VELOCITY); return;
            default: break;
        }
    }

    if (activeView) {
        activeView->handleEvent(state, event);
    }
}

} // namespace ui::state
