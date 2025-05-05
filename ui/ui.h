#pragma once

#include <cstdint>
#include <functional>
#include "pico/stdlib.h"
#include "../commands/command.h"
#include "button.h"
#include "encoder.h"
#include "display.h"
#include "led.h"

namespace ui {

// Main UI class
class UI {
public:
    // Constructor with pin assignments
    UI(
       uint8_t playStopButtonPin, 
       uint8_t encoderPinA,
       uint8_t encoderPinB,
       i2c_inst_t* displayI2C,
       uint8_t eisplayI2CAddr,
       uint8_t displaySDAPin,
       uint8_t displaySCLPin,
       uint8_t playLedPin
    );
    void init();
    void update();
    
private:
    Button playStopButton;
    Encoder bpmEncoder;
    Display display;
    Led playLed;
    
    bool playing;
    uint16_t bpm;
    uint32_t lastDisplayUpdate;

    // UI event handlers
    void onPlayStopButtonPressed();
    void onPlayStopButtonReleased();
    void onEncoderValueChanged(int delta);
    void onPlayStopButtonHold();
    
    void updateDisplay();
};

// Function to create the UI task for the first core
void createUITask(
    uint8_t playStopButtonPin, 
    uint8_t encoderPinA,
    uint8_t encoderPinB,
    i2c_inst_t* displayI2C,
    uint8_t displayI2CAddr,
    uint8_t displaySDAPin,
    uint8_t displaySCLPin,
    uint8_t playLedPin
);

} // namespace ui
