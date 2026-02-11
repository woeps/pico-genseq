#pragma once

#include <cstdint>
#include "../interfaces/IActuator.h"

struct i2c_inst;
typedef struct i2c_inst i2c_inst_t;
class LCD_I2C;

namespace hardware {

class Display : public IActuator {
public:
    Display(i2c_inst_t* i2c, uint8_t i2cAddr, uint8_t sdaPin, uint8_t sclPin);

    void update() override;

    void setValue(int value) override;
    int getValue() const;

    void clear();
    void setCursor(uint8_t line, uint8_t position);
    void print(const char* text);
    void showSetting(const char* label, uint8_t* value);

private:
    void init();

    LCD_I2C* lcd;
    int currentMode;
};

} // namespace hardware
