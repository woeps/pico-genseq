#pragma once

namespace ui {

// Button ID enum for identifying different buttons
enum class ButtonId {
    ENCODER_BUTTON,
    BUTTON_A
};

// View interface for UI screens/views
class IView {
public:
    virtual ~IView() = default;

    // Called when the view becomes active
    virtual void onEnter() = 0;

    // Called when the view becomes inactive
    virtual void onExit() = 0;

    // Called periodically to update the view
    virtual void update() = 0;

    // Called when encoder value changes
    virtual void onEncoderChanged(int delta) = 0;

    // Called when a button is pressed
    virtual void onButtonPressed(ButtonId button) = 0;

    // Called when a button is held
    virtual void onButtonHeld(ButtonId button) = 0;

    // Called to render/update the display
    virtual void render() = 0;
};

} // namespace ui
