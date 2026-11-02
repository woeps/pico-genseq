#include "PitchSetView.h"
#include "../state/Reducer.h"
#include "../state/UIState.h"
#include "../Types.h"
#include "../../common/Scale.h"
#include <cstdio>

namespace ui {

namespace {

// MIDI note name tables (MIDI 60 = C4, sharps)
constexpr char NOTE_LETTERS[] = {'c', 'c', 'd', 'd', 'e', 'f', 'f', 'g', 'g', 'a', 'a', 'b'};
constexpr bool NOTE_SHARPS[]  = {false, true, false, true, false, false, true, false, true, false, true, false};

// Pitch-class names for the scale ROOT field (0..11).
constexpr char ROOT_LETTERS[] = {'c', 'c', 'd', 'd', 'e', 'f', 'f', 'g', 'g', 'a', 'a', 'b'};
constexpr bool ROOT_SHARPS[]  = {false, true, false, true, false, false, true, false, true, false, true, false};

void midiToNoteName(uint8_t midi, char (&out)[4]) {
    const int octave = (midi / 12) - 1;  // MIDI 60 -> octave 4, range -1..9
    const uint8_t noteIndex = midi % 12;
    out[0] = NOTE_LETTERS[noteIndex];
    out[2] = NOTE_SHARPS[noteIndex] ? '#' : ' ';
    if (octave < 0) {
        out[1] = '1';
        out[3] = '-';
    } else {
        out[1] = static_cast<char>('0' + octave);
        out[3] = ' ';
    }
}

constexpr uint8_t ORDER_COUNT = 4;
constexpr char ORDER_LABELS[ORDER_COUNT][4] = {
    "Fwd",  // FORWARDS
    "Bwd",  // BACKWARDS
    "Pnd",  // PENDULUM
    "Rnd",  // RANDOM
};

constexpr char SCALE_LABELS[3][4] = {
    "cro",  // CHROMATIC (no constraint)
    "maj",  // MAJOR
    "min",  // MINOR_NATURAL
};

constexpr char QUANTIZE_LABELS[4][4] = {
    "nea",  // NEAREST
    "dwn",  // DOWN
    "up ",  // UP
    "skp",  // SKIP
};

} // namespace

PitchSetView::PitchSetView(hardware::LedMatrix& ledMatrix)
    : ledMatrix(ledMatrix) {}

void PitchSetView::onEnter()
{
    printf("Entering Pitch Set View\n");
    ledMatrix.clear();
}

void PitchSetView::handleEvent(state::UIState& state, const events::Event& event)
{
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return;
    }

    state::UIState& newState = state;  // in-place: no 1.4KB copy
    const bool isPressed = event.type == events::EventType::KEY_PRESSED;
    const bool coarse = (event.data.key.mods & mod::SHIFT) != 0;
    switch (combo(event.data.key.id, event.data.key.mods & ~mod::SHIFT)) {
        case combo(KeyId::LEFT):  state::movePitchSetField(newState, -1); break;
        case combo(KeyId::RIGHT): state::movePitchSetField(newState, 1); break;
        case combo(KeyId::UP):    state::adjustPitchSetValue(newState, 1, coarse); break;
        case combo(KeyId::DOWN):  state::adjustPitchSetValue(newState, -1, coarse); break;
        case combo(KeyId::ENTER):
            if (isPressed) state::commitPitchSetEdit(newState);
            break;
        case combo(KeyId::ESCAPE):
            if (!isPressed) break;
            if (state.pitchSetDirty) {
                state::undoPitchSetEdit(newState);
            } else {
                state::setCurrentView(newState, state::ViewId::PATTERNS);
            }
            break;
        default: break;
    }
    return;
}

void PitchSetView::render(const state::UIState& state)
{
    ledMatrix.clear();
    const uint8_t field = state.selectedPitchSetField;
    const auto& draft = state.pitchSetDraft;

    if (field == state::PITCH_SET_FIELD_SCALE) {
        ledMatrix.drawLabel("Scl", 0xFFFFEE00);
        ledMatrix.drawLabel(SCALE_LABELS[static_cast<uint8_t>(draft.scale)], 0xFFFF0011);
    } else if (field == state::PITCH_SET_FIELD_ROOT) {
        ledMatrix.drawLabel("rut", 0xFFFFEE00);
        // Root pitch-class name (e.g. "c " / "c#").
        char rootName[4] = {ROOT_LETTERS[draft.scaleRoot],
                            ROOT_SHARPS[draft.scaleRoot] ? '#' : ' ', '\0', '\0'};
        ledMatrix.drawNote(rootName, 0xFFFF0011);
    } else if (field == state::PITCH_SET_FIELD_QUANTIZE) {
        ledMatrix.drawLabel("qnt", 0xFFFFEE00);
        ledMatrix.drawLabel(QUANTIZE_LABELS[static_cast<uint8_t>(draft.quantize)], 0xFFFF0011);
    } else if (field == state::PITCH_SET_FIELD_COUNT) {
        ledMatrix.drawLabel("Cnt", 0xFFFFEE00);
        ledMatrix.drawNumber(draft.count, 0xFFFF0011);
    } else if (field == state::PITCH_SET_FIELD_ORDER) {
        const uint8_t orderIdx = static_cast<uint8_t>(draft.order);
        ledMatrix.drawLabel(ORDER_LABELS[orderIdx], 0xFFFFEE00);
    } else {
        const uint8_t pitchIndex = field - state::PITCH_SET_FIELD_PITCH_BASE;
        if (pitchIndex < draft.count) {
            char noteName[4];
            midiToNoteName(draft.pitches[pitchIndex], noteName);
            ledMatrix.drawNote(noteName, 0xFFFF0011);
            char posLabel[4] = {'p', '\0', '\0', '\0'};
            if (pitchIndex < 10) {
                posLabel[1] = static_cast<char>('0' + pitchIndex);
            } else {
                posLabel[1] = '1';
                posLabel[2] = static_cast<char>('0' + pitchIndex - 10);
            }
            ledMatrix.drawLabel(posLabel, 0xFFFFEE00);

            // Read-only scale-membership cue (R8): while a pitch position is
            // selected AND a scale is active, light a dedicated pixel showing
            // whether the displayed pitch is in scale. Computed via the SAME
            // common::scale library the sequencer uses at emission (R8.4), so the
            // cue and the audible result never disagree. The pixel at (14,0) is
            // distinct from the dirty-draft pixel at (15,0) (R8.2). When no scale
            // is active the render is byte-identical to before (R8.5): no cue.
            if (draft.scale != common::ScaleId::CHROMATIC) {
                const uint16_t mask =
                    common::scale::membershipMask(draft.scale, draft.scaleRoot);
                const bool inScale = common::scale::inScale(mask, draft.pitches[pitchIndex]);
                // Green = in scale, red = out of scale.
                ledMatrix.setPixel(14, 0, inScale ? 0x0000FF00 : 0x00FF0000);
            }
        }
    }
    if (state.pitchSetDirty) ledMatrix.setPixel(15, 0, 0x0000FF00);
}

} // namespace ui
