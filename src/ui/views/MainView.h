#pragma once

#include "../views/IView.h"
#include "../hardware/interfaces/IOutput.h"
#include <cstdint>

namespace ui {

// Main view state - contains all state for this view
struct MainViewState : public ViewState {
    uint8_t bpm;
    bool playing;
    
    MainViewState() : ViewState(), bpm(120), playing(false) {}
};

// Main view for the sequencer UI
class MainView : public IView {
public:
    MainView(hardware::IOutput& display, hardware::IOutput& led);

    // IView implementation
    ViewState* getState() override;
    void reduce(ViewEvent event) override;
    void onEnter() override;
    void onExit() override;
    void update() override;
    void onEncoderChanged(int delta) override;
    void onButtonPressed(ButtonId button) override;
    void onButtonHeld(ButtonId button) override;
    void render() override;

private:
    hardware::IOutput& display;
    hardware::IOutput& led;
    
    // View state (managed by reducer)
    MainViewState state;
    
    // Private reducer methods - each handles specific event types
    void reduceEncoderChanged(int delta);
    void reduceButtonPressed(ButtonId button);
    void reduceButtonHeld(ButtonId button);
};

} // namespace ui
