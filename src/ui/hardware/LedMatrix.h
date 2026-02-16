#pragma once

#include <cstdint>

namespace hardware {

class LedMatrix {
public:
    static constexpr uint16_t NUM_PIXELS = 256;
    static constexpr uint8_t WIDTH = 16;
    static constexpr uint8_t HEIGHT = 16;

    LedMatrix(uint8_t pin);

    void update();

    void clear();
    void setPixel(uint8_t x, uint8_t y, uint32_t color);
    void fill(uint32_t color);
    void drawNumber(int number, uint32_t color);
    void drawLabel(const char (&text)[4], uint32_t color);
    void drawNote(const char (&noteStr)[3], uint32_t color);

private:
    uint8_t pin;
    uint32_t buffer[NUM_PIXELS];
    bool dirty;
};

} // namespace hardware
