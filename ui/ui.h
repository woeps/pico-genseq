#pragma once

#include <cstdint>
#include <functional>
#include "pico/stdlib.h"
#include "../commands/command.h"
#include "components/button.h"
#include "components/encoder.h"
#include "components/display.h"
#include "components/led.h"

namespace ui
{

    // Main UI class
    class UI
    {
    public:
        // Constructor with pin assignments
        UI(
            uint8_t buttonEncoderPin,
            uint8_t buttonAPin,
            uint8_t encoderPinA,
            uint8_t encoderPinB,
            PIO encoderPio,
            uint encoderSm,
            i2c_inst_t* displayI2C,
            uint8_t eisplayI2CAddr,
            uint8_t displaySDAPin,
            uint8_t displaySCLPin,
            uint8_t ledPin);
        void init();
        void update();

    private:
        Button buttonEncoder;
        Button buttonA;
        Encoder encoder;
        Display display;
        Led led;

        bool playing;
        uint8_t bpm;
        uint32_t lastDisplayUpdate;

        // UI event handlers
        void onButtonEncoderPressed();
        void onButtonEncoderReleased();
        void onButtonEncoderHold();

        void onButtonAPressed();
        void onButtonAReleased();
        void onButtonAHold();

        void onEncoderValueChanged(int delta);

        void updateDisplay();
    };

    // Function to create the UI task for the first core
    void createUITask(
        uint8_t buttonEncoderPin,
        uint8_t buttonAPin,
        uint8_t encoderPinA,
        uint8_t encoderPinB,
        PIO encoderPio,
        uint encoderSm,
        i2c_inst_t* displayI2C,
        uint8_t displayI2CAddr,
        uint8_t displaySDAPin,
        uint8_t displaySCLPin,
        uint8_t ledPin);

} // namespace ui
