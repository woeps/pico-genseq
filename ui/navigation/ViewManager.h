#pragma once

#include <memory>
#include <stack>
#include "../views/IView.h"

namespace ui {

// View manager for handling view navigation and lifecycle
class ViewManager {
public:
    ViewManager();

    // Push a new view onto the stack (current view becomes inactive)
    void pushView(std::unique_ptr<IView> view);

    // Pop the current view from the stack (previous view becomes active)
    void popView();

    // Replace the current view with a new one
    void replaceView(std::unique_ptr<IView> view);

    // Update the current view
    void update();

    // Handle input events and forward to current view
    void handleEncoderChanged(int delta);
    void handleButtonPressed(ButtonId button);
    void handleButtonHeld(ButtonId button);

    // Get the current active view
    IView* getCurrentView() const;

private:
    std::stack<std::unique_ptr<IView>> viewStack;
    IView* currentView;
};

} // namespace ui
