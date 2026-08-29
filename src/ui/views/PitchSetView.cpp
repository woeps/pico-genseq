#include "PitchSetView.h"
#include "../state/Reducer.h"
#include "../state/UIState.h"
#include "../Types.h"
#include <cstdio>

namespace ui {

namespace {

// MIDI note name tables (MIDI 60 = C4, sharps)
constexpr char NOTE_LETTERS[] = {'c', 'c', 'd', 'd', 'e', 'f', 'f', 'g', 'g', 'a', 'a', 'b'};
constexpr bool NOTE_SHARPS[]  = {false, true, false, true, false, false, true, false, true, false, true, false};

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

} // namespace

PitchSetView::PitchSetView(hardware::LedMatrix& ledMatrix)
    : ledMatrix(ledMatrix) {}

void PitchSetView::onEnter()
{
    printf("Entering Pitch Set View\n");
    ledMatrix.clear();
}

state::UIState PitchSetView::handleEvent(const state::UIState& state, const events::Event& event)
{
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return state;
    }

    state::UIState newState = state;
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
    return newState;
}

void PitchSetView::render(const state::UIState& state)
{
    ledMatrix.clear();
    const uint8_t field = state.selectedPitchSetField;

    if (field == state::PITCH_SET_FIELD_COUNT) {
        ledMatrix.drawLabel("Cnt", 0xFFFFEE00);
        ledMatrix.drawNumber(state.pitchSetDraft.count, 0xFFFF0011);
    } else if (field == state::PITCH_SET_FIELD_ORDER) {
        const uint8_t orderIdx = static_cast<uint8_t>(state.pitchSetDraft.order);
        ledMatrix.drawLabel(ORDER_LABELS[orderIdx], 0xFFFFEE00);
    } else {
        const uint8_t pitchIndex = field - state::PITCH_SET_FIELD_PITCH_BASE;
        if (pitchIndex < state.pitchSetDraft.count) {
            char noteName[4];
            midiToNoteName(state.pitchSetDraft.pitches[pitchIndex], noteName);
            ledMatrix.drawNote(noteName, 0xFFFF0011);
            char posLabel[4] = {'p', '\0', '\0', '\0'};
            if (pitchIndex < 10) {
                posLabel[1] = static_cast<char>('0' + pitchIndex);
            } else {
                posLabel[1] = '1';
                posLabel[2] = static_cast<char>('0' + pitchIndex - 10);
            }
            ledMatrix.drawLabel(posLabel, 0xFFFFEE00);
        }
    }
    if (state.pitchSetDirty) ledMatrix.setPixel(15, 0, 0x0000FF00);
}

} // namespace ui
