# UI System Guide

## Overview

The GenSeq UI system is an event-driven architecture running on Core 0. It uses a **unidirectional data flow**: hardware emits events → a pure reducer computes new state → views render from state. Views never mutate state directly.

## UI Architecture

### Core Components

```
UIController               # Orchestrator: owns hardware + view
├── Button (×6)            # Concrete hardware, no interfaces
├── Potentiometer          # 10kΩ linear pot via ADC
├── Led
├── MainView               # Render-only view (views/)
└── StateManager (singleton)
    ├── reduce()           # Pure function (state/Reducer.h/cpp)
    └── UIState            # Central state struct (state/UIState.h)
```

### Data Flow

```
Hardware Input → Event → StateManager::dispatch()
                              ↓
                     reduce(state, event) → new state
                              ↓
                     listener callback → View::render(state)
                              ↓
                     commands::sendCommand() → Core 1 (from reducer)
```

### Main UI Loop

```cpp
// From src/ui/ui.cpp
void createUITask(...) {
    UI ui(...);
    ui.init();
    
    while (true) {
        ui.update();   // Polls hardware → emits events → reducer → render
        sleep_ms(1);
    }
}

// UIController::update() polls all hardware:
void UIController::update() {
    buttonEncoder->update();  // May dispatch events to StateManager
    buttonA->update();
    led->update();            // Handles blink timing
}
```

## Event System

### Event Structure

Events use a simple union-based struct (no inheritance, no heap allocation):

```cpp
// From src/ui/Event.h
enum class EventType : uint8_t {
    BUTTON_PRESSED,
    BUTTON_RELEASED,
    BUTTON_HELD,
    POT_CHANGED
};

struct Event {
    EventType type;
    uint32_t timestamp;
    
    union {
        struct { ButtonId id; } button;
        struct { PotId id; uint16_t value; } pot;
    } data;
    
    // Static factory methods
    static Event buttonPressed(ButtonId id);
    static Event potChanged(PotId id, uint16_t value);
    // ...
};
```

Hardware components create events via factory methods and dispatch them directly to `StateManager`:

```cpp
// From hardware/Button.cpp
ui::events::Event event = ui::events::Event::buttonPressed(buttonId);
ui::state::getStateManager().dispatch(event);
```

## Reducer Pattern

### Pure Reducer Function

All state transitions live in one place — the `reduce()` function. State-setter functions
abstract away mutating a state field and sending the corresponding command:

```cpp
// From src/ui/state/Reducer.h
UIState reduce(const UIState& state, const events::Event& event);

// State-setter functions: mutate state and send the corresponding command
void setBpm(UIState& state, int bpm);
void setPlaying(UIState& state, bool playing);
```

```cpp
// From src/ui/state/Reducer.cpp
void setBpm(UIState& state, int bpm) {
    state.bpm = std::max(40, std::min(255, bpm));
    commands::sendCommand(commands::Command::BPM_SET, state.bpm);
}

void setPlaying(UIState& state, bool playing) {
    state.playing = playing;
    if (playing) commands::sendCommand(commands::Command::PLAY);
    else         commands::sendCommand(commands::Command::STOP);
}

UIState reduce(const UIState& state, const events::Event& event) {
    UIState newState = state;
    
    switch (event.type) {
        case EventType::BUTTON_PRESSED:
            if (event.data.button.id == ButtonId::ENCODER_BUTTON) {
                setPlaying(newState, !state.playing);
            }
            break;
        // ...
    }
    return newState;
}
```

### State Manager

The `StateManager` is a thin wrapper that holds state, calls the reducer, and notifies listeners:

```cpp
// From src/ui/state/StateManager.h
class StateManager {
public:
    const UIState& getState() const;
    void dispatch(const events::Event& event);  // Calls reduce(), notifies listener
    void subscribe(StateChangeListener listener);
    
private:
    UIState currentState;
    StateChangeListener listener_;
};

extern StateManager& getStateManager();  // Singleton
```

### State Structure

```cpp
// From src/ui/state/UIState.h
struct UIState {
    ViewId currentView;
    uint8_t bpm;
    bool playing;
    
    UIState() : currentView(ViewId::MAIN), bpm(120), playing(false) {}
};
```

## View System

### View Interface

Views are render-only — they receive state and draw to hardware:

```cpp
// From src/ui/views/IView.h
class IView {
public:
    virtual ~IView() = default;
    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void render(const UIState& state) = 0;
};
```

### Example: MainView

```cpp
// From src/ui/views/MainView.h
class MainView : public IView {
public:
    MainView(hardware::Display& display, hardware::Led& led);
    void onEnter() override;
    void render(const state::UIState& state) override;
    
private:
    hardware::Display& display;
    hardware::Led& led;
};

// From src/ui/views/MainView.cpp
void MainView::render(const state::UIState& state) {
    if (state.playing) led.blink(500, 500);
    else led.off();
    
    display.clear();
    display.print("GenSeq");
    display.setCursor(1, 0);
    // ... show BPM, playing status
}
```

### Wiring Views to State Changes

The `UIController` subscribes the view to state changes during initialization:

```cpp
// From src/ui/UIController.cpp
state::getStateManager().subscribe([this](const state::UIState& newState) {
    activeView->render(newState);
});
```

## Hardware Components

Hardware components are concrete classes — no abstract interfaces:

### Button

```cpp
// From src/ui/hardware/Button.h
class Button {
public:
    Button(uint8_t pin, ui::ButtonId buttonId);
    void update();  // Polls GPIO, dispatches events on press/release/hold
};
```

Features: 50ms debouncing, 1000ms hold detection.

### Potentiometer

```cpp
// From src/ui/hardware/Potentiometer.h
class Potentiometer {
public:
    Potentiometer(uint8_t pin, ui::PotId potId);
    void update();  // Reads ADC, dispatches POT_CHANGED on meaningful change
    uint16_t getValue() const;
};
```

10kΩ linear potentiometer on ADC0 (GPIO26). Threshold-based change detection (±8 LSBs) to filter noise.

### Display

```cpp
// From src/ui/hardware/Display.h
class Display {
public:
    Display(i2c_inst_t* i2c, uint8_t i2cAddr, uint8_t sdaPin, uint8_t sclPin);
    void clear();
    void setCursor(uint8_t line, uint8_t position);
    void print(const char* text);
};
```

20×4 I2C LCD via LCD_I2C driver.

### Led

```cpp
// From src/ui/hardware/Led.h
class Led {
public:
    Led(uint8_t pin);
    void update();  // Handles blink timing
    void on();
    void off();
    void blink(uint32_t onTime, uint32_t offTime);
};
```

## Adding New UI Elements

### 1. Adding a New Button

```cpp
// 1. Add ButtonId to src/ui/Types.h (shared types at ui root)
enum class ButtonId { ENCODER_BUTTON, BUTTON_A, NEW_BUTTON };

// 2. Add member to UIController.h
std::unique_ptr<hardware::Button> buttonNew;

// 3. Create in UIController::initialize()
buttonNew = std::make_unique<hardware::Button>(NEW_PIN, ButtonId::NEW_BUTTON);

// 4. Poll in UIController::update()
buttonNew->update();

// 5. Handle in state/Reducer.cpp
case EventType::BUTTON_PRESSED:
    if (event.data.button.id == ButtonId::NEW_BUTTON) { /* ... */ }
```

### 2. Adding a New View

```cpp
// 1. Create class implementing IView in views/
class SettingsView : public IView {
public:
    SettingsView(hardware::Display& display);
    void render(const state::UIState& state) override;
private:
    hardware::Display& display;
};

// 2. Add ViewId to state/UIState.h
enum class ViewId : uint8_t { MAIN, SETTINGS };

// 3. Handle view switching in state/Reducer.cpp
// 4. Swap activeView in UIController based on state.currentView
```

### 3. Adding New State

```cpp
// 1. Add field to state/UIState.h
struct UIState {
    uint8_t bpm;
    bool playing;
    uint8_t volume;  // New field
};

// 2. Handle in state/Reducer.cpp
case EventType::ENCODER_CHANGED:
    if (editingVolume) newState.volume = clamp(...);

// 3. Render in view
display.print("Vol: " + std::to_string(state.volume));
```

## Best Practices

- **State is the single source of truth** — views only render, reducer only computes
- **Keep the reducer pure** — side effects (commands) are acceptable but contained
- **Keep UI loop under 10ms** — avoid blocking operations
- **Use fixed-size buffers** — avoid dynamic allocation in the loop
- **Batch display updates** — minimize I2C traffic

## Troubleshooting

1. **Unresponsive UI**: Check for blocking operations in `update()` loop
2. **Display Flickering**: Reduce render frequency or implement dirty flags
3. **Input Lag**: Verify debounce timing
4. **State not updating**: Verify events reach `StateManager::dispatch()`, check reducer switch cases

## Summary

The GenSeq UI system provides:
- **Unidirectional data flow**: Events → Reducer → State → View
- **Pure reducer**: All state transitions in one function
- **Render-only views**: Views never mutate state
- **Concrete hardware types**: No unnecessary abstract interfaces
- **Flat file structure**: All UI files in `src/ui/`, hardware in `src/ui/hardware/`
