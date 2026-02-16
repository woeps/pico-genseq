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
    for (int i = 0; i < BUTTON_COUNT; i++) {
        buttons[i] = std::make_unique<hardware::Button>(
            config.buttonPins[i], static_cast<ButtonId>(i));
    }
    pot = std::make_unique<hardware::Potentiometer>(config.potPin, PotId::POT_A);
    led = std::make_unique<hardware::Led>(config.ledPin);
    ledMatrix = std::make_unique<hardware::LedMatrix>(config.ledMatrixPin);

    // Create views (allocated once at initialization)
    initView = std::make_unique<InitView>(*led, *ledMatrix);
    settingsView = std::make_unique<SettingsView>(*led, *ledMatrix);

    // Initialize view array
    views[static_cast<size_t>(state::ViewId::INIT)] = initView.get();
    views[static_cast<size_t>(state::ViewId::SETTINGS)] = settingsView.get();

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
    for (auto& button : buttons) {
        button->update();
    }
    pot->update();
    led->update();
    ledMatrix->update();
}

} // namespace ui
