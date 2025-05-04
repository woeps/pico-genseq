#pragma once

#include <cstdint>
#include "pico/stdlib.h"

namespace ui {

// HD44780 LCD Display class
class Display {
public:
    Display(uint8_t rsPin, uint8_t enablePin, uint8_t d4Pin, uint8_t d5Pin, uint8_t d6Pin, uint8_t d7Pin);
    void init();
    void clear();
    void setCursor(uint8_t col, uint8_t row);
    void print(const char* text);
    void print(int value);

private:
    uint8_t rsPin;
    uint8_t enablePin;
    uint8_t dataPins[4];
    
    void sendCommand(uint8_t cmd);
    void sendData(uint8_t data);
    void write4Bits(uint8_t value);
    void pulseEnable();
};

} // namespace ui
