#include "PatternsView.h"
#include "../state/Reducer.h"
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

state::UIState PatternsView::handleEvent(const state::UIState& state, const events::Event& event)
{
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return state;
    }

    state::UIState newState = state;
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
        case combo(KeyId::ENTER):
            if (!isPressed) break;
            if (!isPattern) {
                state::addPattern(newState);
            } else if (state.selectedPatternSet == state::PatternSet::GATE) {
                state::beginGateSetEdit(newState);
                state::setCurrentView(newState, state::ViewId::GATE_SET);
            } else if (state.selectedPatternSet == state::PatternSet::PITCH) {
                state::beginPitchSetEdit(newState);
                state::setCurrentView(newState, state::ViewId::PITCH_SET);
            } else {
                state::setCurrentView(newState, state::ViewId::VELOCITY_SET);
            }
            break;
        case combo(KeyId::DELETE_KEY):
            if (isPressed && isPattern) state::removePattern(newState, state.selectedPattern);
            break;
        default: break;
    }

    return newState;
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
        for (uint8_t dy = 0; dy < SQUARE_SIZE; dy++) {
            for (uint8_t dx = 0; dx < SQUARE_SIZE; dx++) {
                ledMatrix.setPixel(x + dx, y + dy, color);
            }
        }

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
}

} // namespace ui
