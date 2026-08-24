#pragma once

#include <cstdint>

namespace hardware::testing {

void resetMatrix();
uint32_t pixelAt(uint8_t x, uint8_t y);
const char* lastLabel();
int lastNumber();

}
