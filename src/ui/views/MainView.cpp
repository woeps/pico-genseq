#include "MainView.h"
#include "../../commands/command.h"
#include "pico/time.h"
#include <cstdio>
#include <algorithm>

namespace ui {

MainView::MainView(hardware::IOutput& display, hardware::IOutput& led) :
    display(display),
    led(led),
    bpm(120),
    playing(false),
    lastUpdateTime(0)
{}

void MainView::onEnter()
{
    printf("Entering Main View\n");
    render();
}

void MainView::onExit()
{
    printf("Exiting Main View\n");
}

void MainView::update()
{
    // Periodic updates if needed
    uint32_t currentTime = to_ms_since_boot(get_absolute_time());
    if (currentTime - lastUpdateTime > 1000) { // Update every second
        render();
        lastUpdateTime = currentTime;
    }
}

void MainView::onEncoderChanged(int delta)
{
    // Update BPM based on encoder movement
    bpm = std::max(40, std::min(255, bpm + delta));

    // Send BPM update to sequencer
    commands::sendCommand(commands::Command::BPM_SET, bpm);

    render();
}

void MainView::onButtonPressed(ButtonId button)
{
    if (button == ButtonId::ENCODER_BUTTON) {
        playing = !playing;

        if (playing) {
            commands::sendCommand(commands::Command::PLAY);
            led.setValue(2); // Blink LED
        } else {
            commands::sendCommand(commands::Command::STOP);
            led.setValue(0); // Turn off LED
        }

        render();
    }
}

void MainView::onButtonHeld(ButtonId button)
{
    if (button == ButtonId::ENCODER_BUTTON) {
        printf("Reset encoder value\n");
        // Could reset encoder to 120 BPM or some other action
    }
}

void MainView::render()
{
    // Set display to show main screen
    display.setValue(1); // Title mode

    // Additional rendering could be done here
    // For now, rely on display.setValue(1) to show the title
}

} // namespace ui
