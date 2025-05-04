#include "ui.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include <cstring>
#include <cstdio>

namespace ui {

// UI implementation
UI::UI(uint8_t playStopButtonPin, 
       uint8_t encoderPinA, uint8_t encoderPinB,
       uint8_t displayRsPin, uint8_t displayEnablePin, 
       uint8_t displayD4Pin, uint8_t displayD5Pin, 
       uint8_t displayD6Pin, uint8_t displayD7Pin,
       uint8_t playLedPin)
    : playStopButton(playStopButtonPin, [this]() { onPlayStopButtonPressed(); }),
      bpmEncoder(encoderPinA, encoderPinB, [this](int delta) { onEncoderValueChanged(delta); }),
      display(displayRsPin, displayEnablePin, displayD4Pin, displayD5Pin, displayD6Pin, displayD7Pin),
      playLed(playLedPin),
      playing(false),
      bpm(120),
      lastDisplayUpdate(0) {}

void UI::init() {
    playStopButton.init();
    bpmEncoder.init();
    display.init();
    playLed.init();
    
    // Initialize display with default values
    updateDisplay();
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
    display.clear();
    display.setCursor(0, 0);
    display.print("GenSeq");
    
    display.setCursor(0, 1);
    display.print("BPM: ");
    display.print(bpm);
    
    display.setCursor(12, 1);
    display.print(playing ? "PLAY" : "STOP");
}

void createUITask(uint8_t playStopButtonPin, 
       uint8_t encoderPinA, uint8_t encoderPinB,
       uint8_t displayRsPin, uint8_t displayEnablePin, 
       uint8_t displayD4Pin, uint8_t displayD5Pin, 
       uint8_t displayD6Pin, uint8_t displayD7Pin,
       uint8_t playLedPin) {
    // Create and initialize UI with pin assignments
    UI ui(playStopButtonPin, 
          encoderPinA, encoderPinB,
          displayRsPin, displayEnablePin, 
          displayD4Pin, displayD5Pin, 
          displayD6Pin, displayD7Pin,
          playLedPin);
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
