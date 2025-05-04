#include "display.h"
#include "hardware/gpio.h"
#include <cstdio>

namespace ui {

Display::Display(uint8_t rsPin, uint8_t enablePin, uint8_t d4Pin, uint8_t d5Pin, uint8_t d6Pin, uint8_t d7Pin)
    : rsPin(rsPin), enablePin(enablePin) {
    dataPins[0] = d4Pin;
    dataPins[1] = d5Pin;
    dataPins[2] = d6Pin;
    dataPins[3] = d7Pin;
}

void Display::init() {
    // Initialize all pins as outputs
    gpio_init(rsPin);
    gpio_set_dir(rsPin, GPIO_OUT);
    gpio_init(enablePin);
    gpio_set_dir(enablePin, GPIO_OUT);
    
    for (int i = 0; i < 4; i++) {
        gpio_init(dataPins[i]);
        gpio_set_dir(dataPins[i], GPIO_OUT);
    }
    
    // Wait for the LCD to power up
    sleep_ms(50);
    
    // Initialize in 4-bit mode
    // First, set to 8-bit mode (3 times to ensure it's in a known state)
    for (int i = 0; i < 3; i++) {
        write4Bits(0x03);
        sleep_ms(5);
    }
    
    // Set to 4-bit mode
    write4Bits(0x02);
    
    // Function set: 4-bit mode, 2 lines, 5x8 dots
    sendCommand(0x28);
    
    // Display control: display on, cursor off, blink off
    sendCommand(0x0C);
    
    // Entry mode set: increment, no shift
    sendCommand(0x06);
    
    // Clear display
    clear();
}

void Display::clear() {
    sendCommand(0x01);
    sleep_ms(2); // Clear command needs extra time
}

void Display::setCursor(uint8_t col, uint8_t row) {
    const uint8_t rowOffsets[] = { 0x00, 0x40, 0x14, 0x54 };
    sendCommand(0x80 | (col + rowOffsets[row]));
}

void Display::print(const char* text) {
    while (*text) {
        sendData(*text++);
    }
}

void Display::print(int value) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d", value);
    print(buffer);
}

void Display::sendCommand(uint8_t cmd) {
    gpio_put(rsPin, 0);
    write4Bits(cmd >> 4);
    write4Bits(cmd & 0x0F);
}

void Display::sendData(uint8_t data) {
    gpio_put(rsPin, 1);
    write4Bits(data >> 4);
    write4Bits(data & 0x0F);
}

void Display::write4Bits(uint8_t value) {
    for (int i = 0; i < 4; i++) {
        gpio_put(dataPins[i], (value >> i) & 0x01);
    }
    pulseEnable();
}

void Display::pulseEnable() {
    gpio_put(enablePin, 0);
    sleep_us(1);
    gpio_put(enablePin, 1);
    sleep_us(1);
    gpio_put(enablePin, 0);
    sleep_us(100); // Commands need time to process
}

} // namespace ui
