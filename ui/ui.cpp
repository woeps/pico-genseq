#include "ui.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include <cstring>
#include <cstdio>

namespace ui {

// UI implementation
UI::UI(
    uint8_t playStopButtonPin, 
    uint8_t encoderPinA,
    uint8_t encoderPinB,
    i2c_inst_t* displayI2C,
    uint8_t displayI2CAddr,
    uint8_t displaySDAPin,
    uint8_t displaySCLPin,
    uint8_t playLedPin
) : 
    playStopButton(playStopButtonPin, [this]() { onPlayStopButtonPressed(); }),
    bpmEncoder(encoderPinA, encoderPinB, [this](int delta) { onEncoderValueChanged(delta); }),
    display(displayI2C, displayI2CAddr, displaySDAPin, displaySCLPin),
    playLed(playLedPin),
    playing(false),
    bpm(120),
    lastDisplayUpdate(0)
{ }

void UI::init() {
    playStopButton.init();
    bpmEncoder.init();
    playLed.init();
    
    // Initialize display with default values
    updateDisplay();

    printf("test led start\n");
    playLed.on();
    sleep_ms(200);
    playLed.off();
    printf("test led end\n");
}

void UI::update() {
    playStopButton.update();
    playLed.update();

    // Update display periodically
    uint32_t currentTime = to_ms_since_boot(get_absolute_time());
    if (currentTime - lastDisplayUpdate > 100) { // Update display every 100ms
        updateDisplay();
        lastDisplayUpdate = currentTime;
    }
}

void UI::onPlayStopButtonPressed() {
    playing = !playing;
    
    if (playing) {
        commands::sendCommand(commands::Command::PLAY);
        playLed.blink(100, 900); // Blink at rate proportional to BPM
    } else {
        commands::sendCommand(commands::Command::STOP);
        playLed.off();
    }
}

void UI::onEncoderValueChanged(int delta) {
    // Update BPM based on encoder movement
    bpm = std::max(40, std::min(300, static_cast<int>(bpm) + delta));
    
    // Send BPM update to sequencer
    commands::sendCommand(commands::Command::SET_BPM, bpm);
    
    // Update display
    updateDisplay();
}

void UI::updateDisplay() {
    // TODO
}

void createUITask(
    uint8_t playStopButtonPin, 
    uint8_t encoderPinA,
    uint8_t encoderPinB,
    i2c_inst_t* displayI2C,
    uint8_t displayI2CAddr,
    uint8_t displaySDAPin,
    uint8_t displaySCLPin,
    uint8_t playLedPin
) {
    printf("constructing UI\n");
    // Create and initialize UI with pin assignments
    UI ui(
            playStopButtonPin, 
            encoderPinA,
            encoderPinB,
            displayI2C,
            displayI2CAddr,
            displaySDAPin,
            displaySCLPin,
            playLedPin
        );
    printf("initializing UI\n");
    ui.init();
    
    // Main UI loop
    while (true) {
        // Update UI components
        ui.update();
        
        // Small delay to prevent tight looping
        sleep_ms(1);
    }
}

} // namespace ui
