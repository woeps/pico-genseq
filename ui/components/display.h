#pragma once

#include <cstdint>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// Forward declaration of LCD_I2C class
class LCD_I2C;

namespace ui {

// HD44780 LCD Display with FC113 I2C controller
class Display {
public:
    Display(i2c_inst_t* i2c, uint8_t i2cAddr, uint8_t sdaPin, uint8_t sclPin);
    
    void init();
private:
    // Use LCD_I2C for all communication
    LCD_I2C* lcd;
};

} // namespace ui
