#include "UIController.h"
#include "state/Reducer.h"
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
    patternsView = std::make_unique<PatternsView>(*ledMatrix);
    gateSetView = std::make_unique<GateSetView>(*ledMatrix);
    pitchSetView = std::make_unique<PitchSetView>(*ledMatrix);
    velocitySetView = std::make_unique<VelocitySetView>(*ledMatrix);

    // Initialize view array
    views[static_cast<size_t>(state::ViewId::INIT)] = initView.get();
    views[static_cast<size_t>(state::ViewId::SETTINGS)] = settingsView.get();
    views[static_cast<size_t>(state::ViewId::PATTERNS)] = patternsView.get();
    views[static_cast<size_t>(state::ViewId::GATE_SET)] = gateSetView.get();
    views[static_cast<size_t>(state::ViewId::PITCH_SET)] = pitchSetView.get();
    views[static_cast<size_t>(state::ViewId::VELOCITY_SET)] = velocitySetView.get();

    // Register views with StateManager so it can look up active view during dispatch
    state::getStateManager().setViews(views);

    // Set initial view
    const state::UIState& initialState = state::getStateManager().getState();
    state::syncGateSet(initialState, 0);
    state::syncPitchSet(initialState, 0);
    state::syncVelocitySet(initialState, 0);
    onStateChanged(initialState);

    // Subscribe to state changes for view switching
    state::getStateManager().subscribe([this](const state::UIState& newState) {
        onStateChanged(newState);
    });

    // Input last: events must not arrive before the views are registered.
    keyboard = std::make_unique<hardware::UsbKeyboard>();
    keyboard->initialize();

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
    keyboard->update();
    led->update();
    ledMatrix->update();
}

} // namespace ui
