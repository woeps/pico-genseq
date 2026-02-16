#include "Display.h"
#include "hardware/i2c.h"
#include <cstdio>
#include <string>
#include "driver/LCD_I2C.hpp"

namespace hardware {

Display::Display(i2c_inst_t* i2c, uint8_t i2cAddr, uint8_t sdaPin, uint8_t sclPin)
{
    // Set up I2C pins and initialize
    uint32_t i2cspeed = LCD_I2C_Setup(i2c, sdaPin, sclPin, 100 * 1000);

    // Create the LCD_I2C instance
    lcd = new LCD_I2C(i2cAddr, 20, 4, i2c); // 20x4 LCD display

    init();
}

void Display::init()
{
    // Set initial display settings
    lcd->display();
    lcd->noCursor();
    lcd->noBlink();

    // Clear the display
    lcd->clear();

    // Turn on backlight
    lcd->backlight();

    // Write initial text
    lcd->writeString("GenSeq");
    lcd->writeChar(0b11111111);
}

void Display::update()
{
    // Display doesn't need periodic updates in this implementation
}


void Display::clear()
{
    lcd->clear();
}

void Display::setCursor(uint8_t line, uint8_t position)
{
    // Note: LCD_I2C uses different parameter order (position, line)
    lcd->setCursor(position, line, false);
}

void Display::print(const char* text)
{
    lcd->writeString(text);
}

void Display::showSetting(const char* label, uint8_t* value)
{
    lcd->clear();
    std::string text = std::string(label) + ": " + std::to_string(*value);
    lcd->writeString(text.c_str());
}

} // namespace hardware
