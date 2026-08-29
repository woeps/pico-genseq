#include "GateSetView.h"
#include "../state/Reducer.h"
#include "../Types.h"
#include <cstdio>

namespace ui {

namespace {

constexpr uint8_t NOTE_LENGTH_DENOMINATORS[] = {32, 16, 8, 4, 2, 1};

}

GateSetView::GateSetView(hardware::LedMatrix& ledMatrix)
    : ledMatrix(ledMatrix) {}

void GateSetView::onEnter()
{
    printf("Entering Gate Set View\n");
    ledMatrix.clear();
}

state::UIState GateSetView::handleEvent(const state::UIState& state, const events::Event& event)
{
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return state;
    }

    state::UIState newState = state;
    const bool isPressed = event.type == events::EventType::KEY_PRESSED;
    const bool coarse = (event.data.key.mods & mod::SHIFT) != 0;
    switch (combo(event.data.key.id, event.data.key.mods & ~mod::SHIFT)) {
        case combo(KeyId::LEFT):  state::moveGateSetProperty(newState, -1); break;
        case combo(KeyId::RIGHT): state::moveGateSetProperty(newState, 1); break;
        case combo(KeyId::UP):    state::adjustGateSetValue(newState, 1, coarse); break;
        case combo(KeyId::DOWN):  state::adjustGateSetValue(newState, -1, coarse); break;
        case combo(KeyId::ENTER):
            if (isPressed) state::commitGateSetEdit(newState);
            break;
        case combo(KeyId::ESCAPE):
            if (!isPressed) break;
            if (state.gateSetDirty) {
                state::undoGateSetEdit(newState);
            } else {
                state::setCurrentView(newState, state::ViewId::PATTERNS);
            }
            break;
        default: break;
    }
    return newState;
}

void GateSetView::render(const state::UIState& state)
{
    ledMatrix.clear();
    switch (state.selectedGateSetProperty) {
        case state::GateSetProperty::ALGORITHM:
            ledMatrix.drawLabel("EUC", 0xFFFFEE00);
            break;
        case state::GateSetProperty::STEPS:
            ledMatrix.drawLabel("StP", 0xFFFFEE00);
            ledMatrix.drawNumber(state.gateSetDraft.steps, 0xFFFF0011);
            break;
        case state::GateSetProperty::PULSES:
            ledMatrix.drawLabel("PLS", 0xFFFFEE00);
            ledMatrix.drawNumber(state.gateSetDraft.pulses, 0xFFFF0011);
            break;
        case state::GateSetProperty::ROTATION:
            ledMatrix.drawLabel("rot", 0xFFFFEE00);
            ledMatrix.drawNumber(state.gateSetDraft.rotation, 0xFFFF0011);
            break;
        case state::GateSetProperty::NOTE_LENGTH:
            ledMatrix.drawLabel("nLn", 0xFFFFEE00);
            ledMatrix.drawNumber(
                NOTE_LENGTH_DENOMINATORS[static_cast<uint8_t>(state.gateSetDraft.noteLength)],
                0xFFFF0011);
            break;
        case state::GateSetProperty::LENGTH:
            ledMatrix.drawLabel("LEn", 0xFFFFEE00);
            ledMatrix.drawNumber(state.gateSetDraft.gateLength, 0xFFFF0011);
            break;
    }
    if (state.gateSetDirty) ledMatrix.setPixel(15, 0, 0x0000FF00);
}

}
