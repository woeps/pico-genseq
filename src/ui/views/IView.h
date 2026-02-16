#pragma once

#include <cstdint>

namespace ui {

// Button ID enum for identifying different buttons
enum class ButtonId {
    ENCODER_BUTTON,
    BUTTON_A
};

// View events that can be dispatched to views
enum class ViewEventType {
    ENCODER_CHANGED,
    BUTTON_PRESSED,
    BUTTON_HELD,
    ENTER,
    EXIT,
    UPDATE
};

// View event structure
struct ViewEvent {
    ViewEventType type;
    int value;  // For encoder delta or button id
};

// Base view state structure
struct ViewState {
    bool isActive;
    uint32_t lastUpdateTime;
    
    ViewState() : isActive(false), lastUpdateTime(0) {}
    virtual ~ViewState() = default;
};

// View interface for UI screens/views
class IView {
public:
    virtual ~IView() = default;

    // Get the current view state
    virtual ViewState* getState() = 0;
    
    // Reduce function - takes current state and event, returns new state
    // Each view implements its own reducer logic
    virtual void reduce(ViewEvent event) = 0;

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
