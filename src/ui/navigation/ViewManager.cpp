#include "ViewManager.h"

namespace ui {

ViewManager::ViewManager() : currentView(nullptr) {}

void ViewManager::pushView(std::unique_ptr<IView> view)
{
    // Deactivate current view if it exists
    if (currentView) {
        currentView->onExit();
    }

    // Push new view onto stack
    viewStack.push(std::move(view));
    currentView = viewStack.top().get();

    // Activate new view
    currentView->onEnter();
}

void ViewManager::popView()
{
    if (!viewStack.empty()) {
        // Deactivate current view
        currentView->onExit();

        // Pop current view
        viewStack.pop();

        // Activate previous view if it exists
        if (!viewStack.empty()) {
            currentView = viewStack.top().get();
            currentView->onEnter();
        } else {
            currentView = nullptr;
        }
    }
}

void ViewManager::replaceView(std::unique_ptr<IView> view)
{
    // Deactivate current view if it exists
    if (currentView) {
        currentView->onExit();
    }

    // Replace current view
    if (!viewStack.empty()) {
        viewStack.pop();
    }
    viewStack.push(std::move(view));
    currentView = viewStack.top().get();

    // Activate new view
    currentView->onEnter();
}

void ViewManager::update()
{
    if (currentView) {
        currentView->update();
    }
}

void ViewManager::handleEncoderChanged(int delta)
{
    if (currentView) {
        currentView->onEncoderChanged(delta);
    }
}

void ViewManager::handleButtonPressed(ButtonId button)
{
    if (currentView) {
        currentView->onButtonPressed(button);
    }
}

void ViewManager::handleButtonHeld(ButtonId button)
{
    if (currentView) {
        currentView->onButtonHeld(button);
    }
}

IView* ViewManager::getCurrentView() const
{
    return currentView;
}

} // namespace ui
