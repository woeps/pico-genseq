#include "VelocitySetView.h"
#include "../state/Reducer.h"
#include "../state/UIState.h"
#include "../Types.h"
#include <cstdio>

namespace ui {

namespace {

constexpr uint8_t ORDER_COUNT = 4;
constexpr char ORDER_LABELS[ORDER_COUNT][4] = {
    "Fwd",  // FORWARDS
    "Bwd",  // BACKWARDS
    "Pnd",  // PENDULUM
    "Rnd",  // RANDOM
};

} // namespace

VelocitySetView::VelocitySetView(hardware::LedMatrix& ledMatrix)
    : ledMatrix(ledMatrix) {}

void VelocitySetView::onEnter()
{
    printf("Entering Velocity Set View\n");
    ledMatrix.clear();
}

state::UIState VelocitySetView::handleEvent(const state::UIState& state, const events::Event& event)
{
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return state;
    }

    state::UIState newState = state;
    const bool isPressed = event.type == events::EventType::KEY_PRESSED;
    const bool coarse = (event.data.key.mods & mod::SHIFT) != 0;
    switch (combo(event.data.key.id, event.data.key.mods & ~mod::SHIFT)) {
        case combo(KeyId::LEFT):  state::moveVelocitySetField(newState, -1); break;
        case combo(KeyId::RIGHT): state::moveVelocitySetField(newState, 1); break;
        case combo(KeyId::UP):    state::adjustVelocitySetValue(newState, 1, coarse); break;
        case combo(KeyId::DOWN):  state::adjustVelocitySetValue(newState, -1, coarse); break;
        case combo(KeyId::ENTER):
            if (isPressed) state::commitVelocitySetEdit(newState);
            break;
        case combo(KeyId::ESCAPE):
            if (!isPressed) break;
            if (state.velocitySetDirty) {
                state::undoVelocitySetEdit(newState);
            } else {
                state::setCurrentView(newState, state::ViewId::PATTERNS);
            }
            break;
        default: break;
    }
    return newState;
}

void VelocitySetView::render(const state::UIState& state)
{
    ledMatrix.clear();
    const uint8_t field = state.selectedVelocitySetField;

    if (field == state::VELOCITY_SET_FIELD_COUNT) {
        ledMatrix.drawLabel("Cnt", 0xFFFFEE00);
        ledMatrix.drawNumber(state.velocitySetDraft.count, 0xFFFF0011);
    } else if (field == state::VELOCITY_SET_FIELD_ORDER) {
        const uint8_t orderIdx = static_cast<uint8_t>(state.velocitySetDraft.order);
        ledMatrix.drawLabel(ORDER_LABELS[orderIdx], 0xFFFFEE00);
    } else {
        const uint8_t velIndex = field - state::VELOCITY_SET_FIELD_VELOCITY_BASE;
        if (velIndex < state.velocitySetDraft.count) {
            ledMatrix.drawNumber(state.velocitySetDraft.velocities[velIndex], 0xFFFF0011);
            char posLabel[4] = {'v', '\0', '\0', '\0'};
            if (velIndex < 10) {
                posLabel[1] = static_cast<char>('0' + velIndex);
            } else {
                posLabel[1] = '1';
                posLabel[2] = static_cast<char>('0' + velIndex - 10);
            }
            ledMatrix.drawLabel(posLabel, 0xFFFFEE00);
        }
    }
    if (state.velocitySetDirty) ledMatrix.setPixel(15, 0, 0x0000FF00);
}

} // namespace ui
