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
    UI(uint8_t playStopButtonPin, 
       uint8_t encoderPinA, uint8_t encoderPinB,
       uint8_t displayRsPin, uint8_t displayEnablePin, 
       uint8_t displayD4Pin, uint8_t displayD5Pin, 
       uint8_t displayD6Pin, uint8_t displayD7Pin,
       uint8_t playLedPin);
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
    void onEncoderValueChanged(int delta);
    
    
    void updateDisplay();
};

// Function to create the UI task for the first core
void createUITask(uint8_t playStopButtonPin, 
       uint8_t encoderPinA, uint8_t encoderPinB,
       uint8_t displayRsPin, uint8_t displayEnablePin, 
       uint8_t displayD4Pin, uint8_t displayD5Pin, 
       uint8_t displayD6Pin, uint8_t displayD7Pin,
       uint8_t playLedPin);

} // namespace ui
