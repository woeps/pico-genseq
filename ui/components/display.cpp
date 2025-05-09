#include "display.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include <cstdio>
#include <string>
#include "../driver/LCD_I2C.hpp"

namespace ui {

    Display::Display(
        i2c_inst_t* i2c,
        uint8_t i2cAddr,
        uint8_t sdaPin,
        uint8_t sclPin)
    {
        // Set up I2C pins and initialize
        uint32_t i2cspeed = LCD_I2C_Setup(i2c, sdaPin, sclPin, 100 * 1000);

        // Create the LCD_I2C instance
        lcd = new LCD_I2C(i2cAddr, 20, 4, i2c); // 20x4 LCD display

        init();
    }

    void Display::init() {
        // Set initial display settings
        lcd->display();
        lcd->noCursor();
        lcd->noBlink();

        // Clear the display
        lcd->clear();

        // Turn on backlight
        lcd->backlight();

        // Write text
        lcd->writeString("GenSeq");
    }

} // namespace ui
