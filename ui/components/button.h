#pragma once

#include <cstdint>
#include <functional>
#include "pico/stdlib.h"

namespace ui {

    // Button class for handling button inputs
    class Button {
    public:
        using Callback = std::function<void(void)>;

        /// Constructor for a button object.
        ///
        /// \param pin GPIO pin number the button is connected to
        /// \param pressCallback Callback to be called when the button is pressed
        /// \param releaseCallback Callback to be called when the button is released
        /// \param holdCallback Callback to be called when the button is held for holdTimeMs ms
        /// \param holdTimeMs Time in ms to wait before calling the hold callback
        Button(uint8_t pin, Callback pressCallback = nullptr, Callback releaseCallback = nullptr, Callback holdCallback = nullptr, uint32_t holdTimeMs = 1000);

        /// Updates the button state and calls the corresponding callback if necessary.
        ///
        /// Call this function periodically to update the button state.
        void update();

    private:
        uint8_t pin;
        bool pressed;
        Callback pressCallback;
        Callback releaseCallback;
        Callback holdCallback;
        uint32_t holdTimeMs;
        uint32_t pressStartTime;
        bool holdTriggered;
        uint32_t lastDebounceTime;
        bool lastState;
    };

} // namespace ui
