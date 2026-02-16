#include "InitView.h"
#include <cstdio>

namespace ui {

InitView::InitView(hardware::Led& led, hardware::LedMatrix& ledMatrix) :
    led(led),
    ledMatrix(ledMatrix)
{}

void InitView::onEnter()
{
    printf("Entering Main View\n");
    ledMatrix.clear();
}

void InitView::render(const state::UIState& state)
{
    // Update LED based on playing state
    if (state.playing) {
        led.blink(500, 500);
    } else {
        led.off();
    }

    ledMatrix.drawNumber(state.value, 0xFFFF0011);
    ledMatrix.drawLabel("tst", 0x0000FF33);

}

} // namespace ui
