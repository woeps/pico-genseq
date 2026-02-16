#include "MainView.h"
#include "../../commands/command.h"
#include "pico/time.h"
#include <cstdio>
#include <algorithm>

namespace ui {

MainView::MainView(hardware::IOutput& display, hardware::IOutput& led) :
    display(display),
    led(led),
    state()
{
    state.bpm = 120;
    state.playing = false;
}

ViewState* MainView::getState()
{
    return &state;
}

void MainView::reduce(ViewEvent event)
{
    switch (event.type) {
        case ViewEventType::ENCODER_CHANGED:
            reduceEncoderChanged(event.value);
            break;
        case ViewEventType::BUTTON_PRESSED:
            reduceButtonPressed(static_cast<ButtonId>(event.value));
            break;
        case ViewEventType::BUTTON_HELD:
            reduceButtonHeld(static_cast<ButtonId>(event.value));
            break;
        case ViewEventType::ENTER:
            state.isActive = true;
            break;
        case ViewEventType::EXIT:
            state.isActive = false;
            break;
        case ViewEventType::UPDATE:
            update();
            break;
    }
}

void MainView::reduceEncoderChanged(int delta)
{
    // Update BPM based on encoder movement
    state.bpm = std::max(40, std::min(255, state.bpm + delta));
    
    // Send BPM update to sequencer
    commands::sendCommand(commands::Command::BPM_SET, state.bpm);
    
    render();
}

void MainView::reduceButtonPressed(ButtonId button)
{
    if (button == ButtonId::ENCODER_BUTTON) {
        state.playing = !state.playing;

        if (state.playing) {
            commands::sendCommand(commands::Command::PLAY);
            led.setValue(2); // Blink LED
        } else {
            commands::sendCommand(commands::Command::STOP);
            led.setValue(0); // Turn off LED
        }

        render();
    }
}

void MainView::reduceButtonHeld(ButtonId button)
{
    if (button == ButtonId::ENCODER_BUTTON) {
        printf("Reset encoder value\n");
        // Could reset encoder to 120 BPM or some other action
    }
}

void MainView::onEnter()
{
    state.isActive = true;
    printf("Entering Main View\n");
    render();
}

void MainView::onExit()
{
    state.isActive = false;
    printf("Exiting Main View\n");
}

void MainView::update()
{
    // Periodic updates if needed
    uint32_t currentTime = to_ms_since_boot(get_absolute_time());
    if (currentTime - state.lastUpdateTime > 1000) { // Update every second
        render();
        state.lastUpdateTime = currentTime;
    }
}

void MainView::onEncoderChanged(int delta)
{
    // Forward to reducer
    ViewEvent event{ViewEventType::ENCODER_CHANGED, delta};
    reduce(event);
}

void MainView::onButtonPressed(ButtonId button)
{
    // Forward to reducer
    ViewEvent event{ViewEventType::BUTTON_PRESSED, static_cast<int>(button)};
    reduce(event);
}

void MainView::onButtonHeld(ButtonId button)
{
    // Forward to reducer
    ViewEvent event{ViewEventType::BUTTON_HELD, static_cast<int>(button)};
    reduce(event);
}

void MainView::render()
{
    // Set display to show main screen
    display.setValue(1); // Title mode

    // Additional rendering could be done here
    // For now, rely on display.setValue(1) to show the title
}

} // namespace ui
