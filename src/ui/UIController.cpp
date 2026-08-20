#include "UIController.h"
#include "state/StateManager.h"
#include <cstdio>

namespace ui {

UIController::UIController(const HardwareConfig& config)
    : config(config), activeView(nullptr) {}

UIController::~UIController() = default;

void UIController::initialize()
{
    printf("Initializing UI Controller...\n");

    // Create hardware
    led = std::make_unique<hardware::Led>(config.ledPin);
    ledMatrix = std::make_unique<hardware::LedMatrix>(config.ledMatrixPin);

    // Create views (allocated once at initialization)
    initView = std::make_unique<InitView>(*led, *ledMatrix);
    settingsView = std::make_unique<SettingsView>(*led, *ledMatrix);

    // Initialize view array
    views[static_cast<size_t>(state::ViewId::INIT)] = initView.get();
    views[static_cast<size_t>(state::ViewId::SETTINGS)] = settingsView.get();

    // Register views with StateManager so it can look up active view during dispatch
    state::getStateManager().setViews(views);

    // Set initial view
    const state::UIState& initialState = state::getStateManager().getState();
    onStateChanged(initialState);

    // Subscribe to state changes for view switching
    state::getStateManager().subscribe([this](const state::UIState& newState) {
        onStateChanged(newState);
    });

    printf("UI Controller initialized\n");
}

void UIController::onStateChanged(const state::UIState& newState)
{
    IView* newView = views[static_cast<size_t>(newState.currentView)];
    
    if (newView != activeView) {
        if (activeView != nullptr) {
            activeView->onExit();
        }
        activeView = newView;
        activeView->onEnter();
    }
    
    activeView->render(newState);
}

void UIController::update()
{
    led->update();
    ledMatrix->update();
}

} // namespace ui
