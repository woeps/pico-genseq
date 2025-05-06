#include "ui.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/i2c.h"
#include <cstring>
#include <cstdio>

namespace ui {

// UI implementation
UI::UI(
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
    uint8_t ledPin
) : 
    buttonEncoder(buttonEncoderPin, [this]() { onButtonEncoderPressed(); }, [this]() { onButtonEncoderReleased(); }, [this]() { onButtonEncoderHold(); }, 1000),
    buttonA(buttonAPin, [this]() { onButtonAPressed(); }, [this]() { onButtonAReleased(); }, [this]() { onButtonAHold(); }, 1000),
    encoder(encoderPinA, encoderPinB, encoderPio, encoderSm, [this](int delta) { onEncoderValueChanged(delta); }),
    display(displayI2C, displayI2CAddr, displaySDAPin, displaySCLPin),
    led(ledPin),
    playing(false),
    bpm(120),
    lastDisplayUpdate(0)
{ }

void UI::init() {
    updateDisplay();

    printf("test led start\n");
    led.on();
    sleep_ms(200);
    led.off();
    printf("test led end\n");
}

void UI::update() {
    buttonEncoder.update();
    buttonA.update();
    led.update();
    encoder.update();

    // Update display periodically
    uint32_t currentTime = to_ms_since_boot(get_absolute_time());
    if (currentTime - lastDisplayUpdate > 100) { // Update display every 100ms
        updateDisplay();
        lastDisplayUpdate = currentTime;
    }
}

void UI::onButtonEncoderPressed() {
    printf("pressed encoder button\n");
    playing = !playing;
    
    if (playing) {
        commands::sendCommand(commands::Command::PLAY);
        led.blink(100, 900); // Blink at rate proportional to BPM
    } else {
        commands::sendCommand(commands::Command::STOP);
        led.off();
    }
}

void UI::onButtonEncoderReleased() {
    printf("released encoder button\n");
}

void UI::onButtonEncoderHold() {
    printf("hold encoder button: set value to 0\n");
    encoder.setValue(0);
}

void UI::onButtonAPressed() {
    printf("pressed A button\n");
}

void UI::onButtonAReleased() {
    printf("released A button\n");
}

void UI::onButtonAHold() {
    printf("hold A button\n");
}

void UI::onEncoderValueChanged(int delta) {
    printf("delta: %d\n", delta);
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
    uint8_t ledPin
) {
    printf("constructing UI\n");
    // Create and initialize UI with pin assignments
    UI ui(
            buttonEncoderPin, 
            buttonAPin,
            encoderPinA,
            encoderPinB,
            encoderPio,
            encoderSm,
            displayI2C,
            displayI2CAddr,
            displaySDAPin,
            displaySCLPin,
            ledPin
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
