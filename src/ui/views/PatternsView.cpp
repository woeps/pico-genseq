#include "PatternsView.h"
#include "../state/Reducer.h"
#include "../state/SetLibrary.h"
#include "../state/Instruments.h"
#include "../Types.h"
#include <cstdio>

namespace ui {

PatternsView::PatternsView(hardware::LedMatrix& ledMatrix)
    : ledMatrix(ledMatrix) {}

void PatternsView::onEnter()
{
    printf("Entering Patterns View\n");
    ledMatrix.clear();
}

void PatternsView::handleEvent(state::UIState& state, const events::Event& event)
{
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return;
    }

    state::UIState& newState = state;  // in-place: no 1.4KB copy
    const bool isPressed = event.type == events::EventType::KEY_PRESSED;
    const bool isPattern = state.selectedPattern < state.patternCount;
    const uint8_t maxPattern = state.patternCount < state::MAX_PATTERNS
        ? state.patternCount
        : state::MAX_PATTERNS - 1;

    switch (combo(event.data.key.id, event.data.key.mods)) {
        case combo(KeyId::UP):
            if (isPattern && state.selectedPatternSet == state::PatternSet::VELOCITY) {
                state::setSelectedPatternSet(newState, state::PatternSet::GATE);
            }
            break;
        case combo(KeyId::DOWN):
            if (isPattern && state.selectedPatternSet == state::PatternSet::GATE) {
                state::setSelectedPatternSet(newState, state::PatternSet::VELOCITY);
            }
            break;
        case combo(KeyId::LEFT):
            if (isPattern && state.selectedPatternSet == state::PatternSet::PITCH) {
                state::setSelectedPatternSet(newState, state::PatternSet::GATE);
            }
            break;
        case combo(KeyId::RIGHT):
            if (isPattern && state.selectedPatternSet == state::PatternSet::GATE) {
                state::setSelectedPatternSet(newState, state::PatternSet::PITCH);
            }
            break;
        case combo(KeyId::UP, mod::CTRL): {
            int index = state.selectedPattern - GRID_COLS;
            if (index < 0) index = 0;
            state::setSelectedPattern(newState, static_cast<uint8_t>(index));
            break;
        }
        case combo(KeyId::DOWN, mod::CTRL): {
            int index = state.selectedPattern + GRID_COLS;
            if (index > maxPattern) index = maxPattern;
            state::setSelectedPattern(newState, static_cast<uint8_t>(index));
            break;
        }
        case combo(KeyId::LEFT, mod::CTRL): {
            int index = state.selectedPattern - 1;
            if (index < 0) index = 0;
            state::setSelectedPattern(newState, static_cast<uint8_t>(index));
            break;
        }
        case combo(KeyId::RIGHT, mod::CTRL): {
            int index = state.selectedPattern + 1;
            if (index > maxPattern) index = maxPattern;
            state::setSelectedPattern(newState, static_cast<uint8_t>(index));
            break;
        }
        case combo(KeyId::LEFT, mod::SHIFT):
        case combo(KeyId::RIGHT, mod::SHIFT): {
            // Step the selected pattern's reference of the highlighted set type (R5.2).
            if (!isPattern) break;
            const state::SetType type =
                state.selectedPatternSet == state::PatternSet::GATE ? state::SetType::GATE
                : state.selectedPatternSet == state::PatternSet::PITCH ? state::SetType::NOTE
                                                                       : state::SetType::VELOCITY;
            state::stepSetRef(newState, state.selectedPattern, type,
                              event.data.key.id == KeyId::LEFT ? -1 : +1);
            break;
        }
        case combo(KeyId::ENTER):
            if (!isPressed) break;
            if (!isPattern) {
                state::addPattern(newState);
                break;
            }
            // Open the set the selected pattern references (R4.2).
            if (state.selectedPatternSet == state::PatternSet::GATE) {
                state::openSetView(newState, state::SetType::GATE);
            } else if (state.selectedPatternSet == state::PatternSet::PITCH) {
                state::openSetView(newState, state::SetType::NOTE);
            } else {
                state::openSetView(newState, state::SetType::VELOCITY);
            }
            break;
        case combo(KeyId::DELETE_KEY):
            if (isPressed && isPattern) state::removePattern(newState, state.selectedPattern);
            break;
        default: break;
    }

    return;
}

void PatternsView::render(const state::UIState& state)
{
    ledMatrix.clear();
    ledMatrix.drawLabel("PTN", 0xFFFFEE00);

    const uint8_t slotsToShow = state.patternCount < state::MAX_PATTERNS
        ? state.patternCount + 1
        : state::MAX_PATTERNS;

    for (uint8_t i = 0; i < slotsToShow; i++) {
        const uint8_t col = i % GRID_COLS;
        const uint8_t row = i / GRID_COLS;
        const uint8_t x = GRID_START_X + col * (SQUARE_SIZE + GRID_GAP);
        const uint8_t y = GRID_START_Y + row * (SQUARE_SIZE + GRID_GAP);
        const bool isNewSlot = i == state.patternCount;

        if (isNewSlot) {
            for (uint8_t dx = 0; dx < SQUARE_SIZE; dx++) {
                ledMatrix.setPixel(x + dx, y + SQUARE_SIZE - 1, COLOR_NEW_SLOT);
            }
            if (i == state.selectedPattern) {
                ledMatrix.setPixel(x + SQUARE_SIZE - 1, y + SQUARE_SIZE - 1, COLOR_SELECTED);
            }
            continue;
        }

        const uint32_t color = state.activePatterns & (1 << i)
            ? COLOR_ACTIVE
            : COLOR_INACTIVE;
        // A pattern in the selected Instrument is a full cell. A pattern that is not keeps only
        // its top-left pixel, and a pattern in no Instrument at all adds a magenta top-right
        // pixel (it is silent).
        const bool member = (state.instruments[state.selectedInstrument].members >> i) & 1u;
        const bool unassigned = state::instrumentsOf(state, i) == 0;
        for (uint8_t dy = 0; dy < SQUARE_SIZE; dy++) {
            for (uint8_t dx = 0; dx < SQUARE_SIZE; dx++) {
                const bool lit = member || (dx == 0 && dy == 0);
                ledMatrix.setPixel(x + dx, y + dy, lit ? color : 0);
            }
        }
        if (!member && unassigned) ledMatrix.setPixel(x + SQUARE_SIZE - 1, y, COLOR_UNASSIGNED);
        if (i != state.selectedPattern) continue;

        uint8_t selectedX = x;
        uint8_t selectedY = y;
        if (state.selectedPatternSet == state::PatternSet::PITCH) {
            selectedX++;
        } else if (state.selectedPatternSet == state::PatternSet::VELOCITY) {
            selectedY++;
        }
        ledMatrix.setPixel(selectedX, selectedY, COLOR_SELECTED);
    }

    // Set references of the selected pattern (R5.1): rows 13/14/15 are the gate / note /
    // velocity maps, one pixel per slot. Occupied slots are dim; the slot the pattern
    // references is bright.
    if (state.selectedPattern < state.patternCount) {
        const state::PatternRefs& refs = state.patternRefs[state.selectedPattern];
        const state::SetType types[3] = {state::SetType::GATE, state::SetType::NOTE,
                                         state::SetType::VELOCITY};
        const uint8_t refSlot[3] = {refs.gate, refs.note, refs.velocity};
        for (uint8_t t = 0; t < 3; ++t) {
            const uint16_t occupied = state::occupiedMask(state, types[t]);
            for (uint8_t slot = 0; slot < state::SET_LIBRARY_SIZE; ++slot) {
                if (!((occupied >> slot) & 1u)) continue;
                ledMatrix.setPixel(slot, REF_ROW_BASE + t,
                                   slot == refSlot[t] ? COLOR_SELECTED : COLOR_SLOT_OCCUPIED);
            }
        }
    }
}

} // namespace ui
