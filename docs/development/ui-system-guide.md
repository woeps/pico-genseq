# UI System Guide

## Overview

The GenSeq UI system is designed to be responsive, modular, and easily extensible. It runs on Core 0 and provides a clean interface between hardware components and the sequencer logic. This guide covers the UI architecture, component system, and how to add new UI elements.

## UI Architecture

### Core Components

```
UIController
├── ViewManager          # Manages UI screens
├── HardwareLayer        # Hardware abstraction
│   ├── InputManager     # Buttons, encoders
│   └── OutputManager    # Display, LEDs
├── CommandGenerator     # Creates commands for sequencer
└── EventLoop           # Main UI update loop
```

### Main UI Loop

```cpp
// From src/ui/ui.cpp
void uiMainLoop() {
    while (true) {
        // 1. Read hardware inputs
        inputManager.update();
        
        // 2. Process events
        if (inputManager.hasEvent()) {
            InputEvent event = inputManager.getEvent();
            currentView->handleInput(event);
        }
        
        // 3. Update display
        if (shouldUpdateDisplay()) {
            currentView->render();
            display.update();
        }
        
        // 4. Update LEDs
        ledManager.update();
        
        // 5. Small delay to prevent busy-waiting
        sleep_ms(1);
    }
}
```

## View System

### View Interface

```cpp
// From src/ui/views/IView.h
class IView {
public:
    virtual ~IView() = default;
    
    // Called when view becomes active
    virtual void onEnter() = 0;
    
    // Called when view becomes inactive
    virtual void onExit() = 0;
    
    // Handle user input
    virtual void handleInput(const InputEvent& event) = 0;
    
    // Render the view
    virtual void render() = 0;
    
    // Update periodic (for animations, etc.)
    virtual void update() {}
};
```

### View Manager

```cpp
// From src/ui/navigation/ViewManager.cpp
class ViewManager {
private:
    std::vector<std::unique_ptr<IView>> views;
    IView* currentView = nullptr;
    ViewID currentViewID = ViewID::NONE;
    
public:
    void registerView(ViewID id, std::unique_ptr<IView> view) {
        views[id] = std::move(view);
    }
    
    void navigateTo(ViewID id) {
        if (currentView) {
            currentView->onExit();
        }
        
        currentViewID = id;
        currentView = views[id].get();
        currentView->onEnter();
    }
    
    void handleInput(const InputEvent& event) {
        if (currentView) {
            currentView->handleInput(event);
        }
    }
    
    void render() {
        if (currentView) {
            currentView->render();
        }
    }
};
```

### Example View Implementation

```cpp
// Main view showing pattern status
class MainView : public IView {
private:
    uint8_t selectedPattern = 0;
    bool showDetails = false;
    
public:
    void onEnter() override {
        // Initialize view state
        display.clear();
        updateDisplay();
    }
    
    void onExit() override {
        // Cleanup if needed
    }
    
    void handleInput(const InputEvent& event) override {
        switch (event.type) {
            case InputType::ENCODER_TURN:
                if (event.direction > 0) {
                    selectedPattern = (selectedPattern + 1) % MAX_PATTERNS;
                } else {
                    selectedPattern = (selectedPattern - 1 + MAX_PATTERNS) % MAX_PATTERNS;
                }
                updateDisplay();
                break;
                
            case InputType::BUTTON_PRESS:
                if (event.buttonId == BUTTON_ENTER) {
                    showDetails = !showDetails;
                    updateDisplay();
                } else if (event.buttonId == BUTTON_PLAY) {
                    sendCommand(CommandType::PLAY);
                }
                break;
        }
    }
    
    void render() override {
        display.setCursor(0, 0);
        display.printf("Pattern %d: %s", 
            selectedPattern,
            isPatternActive(selectedPattern) ? "ON " : "OFF");
            
        if (showDetails) {
            display.setCursor(0, 1);
            display.printf("BPM:%d Len:%d", 
                getBPM(),
                getPatternLength(selectedPattern));
        }
    }
    
private:
    void updateDisplay() {
        display.clear();
        render();
    }
};
```

## Hardware Abstraction Layer

### Input System

#### Button Interface

```cpp
// From src/ui/hardware/components/Button.h
class Button {
private:
    uint8_t pin;
    bool lastState = false;
    bool currentState = false;
    uint32_t lastDebounceTime = 0;
    static const uint32_t DEBOUNCE_MS = 50;
    
public:
    Button(uint8_t pin) : pin(pin) {}
    
    void init() {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
    }
    
    void update() {
        bool reading = !gpio_get(pin); // Inverted (pull-up)
        
        if (reading != lastState) {
            lastDebounceTime = time_us_32() / 1000;
        }
        
        if ((time_us_32() / 1000 - lastDebounceTime) > DEBOUNCE_MS) {
            if (reading != currentState) {
                currentState = reading;
                if (currentState) {
                    // Button pressed
                    onPress();
                }
            }
        }
        
        lastState = reading;
    }
    
    bool isPressed() const { return currentState; }
    
    std::function<void()> onPress;
};
```

#### Encoder Interface

```cpp
// From src/ui/hardware/components/Encoder.h
class Encoder {
private:
    int32_t position = 0;
    int32_t lastPosition = 0;
    bool buttonPressed = false;
    
public:
    void init() {
        // PIO handles encoder reading
        pio_add_program(pio, &encoder_program);
        uint32_t offset = pio_add_program(pio, &encoder_program);
        
        encoder_pio_init(pio, sm, offset, 
            ENCODER_PIN_A, ENCODER_PIN_B);
        
        // Setup button
        button.init();
    }
    
    void update() {
        // Read from PIO
        int32_t newPosition = encoder_get_count(pio, sm);
        
        if (newPosition != position) {
            lastPosition = position;
            position = newPosition;
            onTurn(position - lastPosition);
        }
        
        button.update();
        if (button.isPressed() && !buttonPressed) {
            buttonPressed = true;
            onButtonPress();
        } else if (!button.isPressed() && buttonPressed) {
            buttonPressed = false;
            onButtonRelease();
        }
    }
    
    int32_t getPosition() const { return position; }
    void reset() { position = 0; }
    
    std::function<void(int delta)> onTurn;
    std::function<void()> onButtonPress;
    std::function<void()> onButtonRelease;
};
```

### Output System

#### Display Interface

```cpp
// From src/ui/hardware/components/Display.h
class Display {
private:
    LCD_I2C lcd;
    
public:
    void init() {
        lcd.init(DISPLAY_I2C, DISPLAY_PIN_SDA, DISPLAY_PIN_SCL, 
                 DISPLAY_I2C_ADDR);
        lcd.backlight();
        clear();
    }
    
    void clear() {
        lcd.clear();
    }
    
    void setCursor(uint8_t col, uint8_t row) {
        lcd.setCursor(col, row);
    }
    
    void printf(const char* format, ...) {
        va_list args;
        va_start(args, format);
        lcd.vprintf(format, args);
        va_end(args);
    }
    
    void print(const char* text) {
        lcd.print(text);
    }
    
    void update() {
        // LCD updates are immediate
    }
};
```

#### LED Matrix Interface

```cpp
// From src/ui/hardware/components/LedMatrix.h
class LedMatrix {
private:
    std::vector<uint32_t> ledData;
    bool needsUpdate = false;
    
public:
    void init() {
        ledData.resize(LED_COUNT, 0);
        initWS2812();
    }
    
    void setLED(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
        if (index < LED_COUNT) {
            ledData[index] = (g << 16) | (r << 8) | b;
            needsUpdate = true;
        }
    }
    
    void update() {
        if (needsUpdate) {
            // Send via DMA
            dma_channel_configure(dma_chan,
                &dma_config,
                &led_data,          // Destination
                ledData.data(),     // Source
                LED_COUNT,          // Transfer count
                true                // Start immediately
            );
            needsUpdate = false;
        }
    }
    
    void allOff() {
        std::fill(ledData.begin(), ledData.end(), 0);
        needsUpdate = true;
    }
};
```

## Input Event System

### Event Types

```cpp
enum class InputType {
    BUTTON_PRESS,
    BUTTON_RELEASE,
    BUTTON_HOLD,
    ENCODER_TURN,
    ENCODER_PRESS,
    ENCODER_RELEASE
};

struct InputEvent {
    InputType type;
    union {
        struct {
            uint8_t buttonId;
        } button;
        struct {
            int8_t direction;  // -1 or 1
            int32_t position;
        } encoder;
    };
    uint32_t timestamp;
};
```

### Input Manager

```cpp
class InputManager {
private:
    std::vector<Button> buttons;
    Encoder encoder;
    std::queue<InputEvent> eventQueue;
    
public:
    void init() {
        // Initialize buttons
        for (uint8_t i = 0; i < BUTTON_COUNT; i++) {
            buttons.emplace_back(BUTTON_PINS[i]);
            buttons.back().init();
            buttons.back().onPress = [this, i]() {
                InputEvent event;
                event.type = InputType::BUTTON_PRESS;
                event.button.buttonId = i;
                event.timestamp = time_us_32();
                eventQueue.push(event);
            };
        }
        
        // Initialize encoder
        encoder.init();
        encoder.onTurn = [this](int delta) {
            InputEvent event;
            event.type = InputType::ENCODER_TURN;
            event.encoder.direction = delta > 0 ? 1 : -1;
            event.encoder.position = encoder.getPosition();
            event.timestamp = time_us_32();
            eventQueue.push(event);
        };
    }
    
    void update() {
        // Update all inputs
        for (auto& button : buttons) {
            button.update();
        }
        encoder.update();
    }
    
    bool hasEvent() const {
        return !eventQueue.empty();
    }
    
    InputEvent getEvent() {
        InputEvent event = eventQueue.front();
        eventQueue.pop();
        return event;
    }
};
```

## Adding New UI Elements

### 1. Adding a New Button

```cpp
// In hardware config
#define NEW_BUTTON_PIN 22

// In InputManager init
buttons.emplace_back(NEW_BUTTON_PIN);
buttons.back().onPress = []() {
    // Handle button press
    sendCommand(CommandType::NEW_ACTION);
};
```

### 2. Adding a New View

```cpp
// Create new view class
class SettingsView : public IView {
    // Implement required methods
};

// Register in main UI init
void initUI() {
    viewManager.registerView(ViewID::SETTINGS, 
        std::make_unique<SettingsView>());
    
    // Navigate to settings
    viewManager.navigateTo(ViewID::SETTINGS);
}
```

### 3. Adding New Hardware Component

```cpp
// Create interface
class ICustomHardware {
public:
    virtual void init() = 0;
    virtual void update() = 0;
    virtual int read() = 0;
};

// Implement component
class CustomSensor : public ICustomHardware {
    // Implementation
};

// Add to UI controller
class UIController {
private:
    CustomSensor customSensor;
    
public:
    void init() {
        customSensor.init();
        // ... other init
    }
    
    void update() {
        customSensor.update();
        // ... other updates
    }
};
```

## UI Patterns

### Menu System

```cpp
class MenuView : public IView {
private:
    struct MenuItem {
        const char* text;
        std::function<void()> action;
    };
    
    std::vector<MenuItem> menuItems;
    uint8_t selectedIndex = 0;
    
public:
    void addMenuItem(const char* text, std::function<void()> action) {
        menuItems.push_back({text, action});
    }
    
    void handleInput(const InputEvent& event) override {
        switch (event.type) {
            case InputType::ENCODER_TURN:
                selectedIndex = (selectedIndex + event.encoder.direction 
                    + menuItems.size()) % menuItems.size();
                updateDisplay();
                break;
                
            case InputType::BUTTON_PRESS:
                if (event.button.buttonId == BUTTON_ENTER) {
                    menuItems[selectedIndex].action();
                }
                break;
        }
    }
};
```

### Confirmation Dialog

```cpp
class ConfirmDialog : public IView {
private:
    const char* message;
    std::function<void()> onConfirm;
    std::function<void()> onCancel;
    bool confirmed = false;
    
public:
    ConfirmDialog(const char* msg, 
                  std::function<void()> confirm,
                  std::function<void()> cancel = nullptr)
        : message(msg), onConfirm(confirm), onCancel(cancel) {}
    
    void handleInput(const InputEvent& event) override {
        if (event.type == InputType::BUTTON_PRESS) {
            if (event.button.buttonId == BUTTON_ENTER) {
                confirmed = true;
                if (onConfirm) onConfirm();
                viewManager.navigateTo(previousView);
            } else if (event.button.buttonId == BUTTON_BACK) {
                if (onCancel) onCancel();
                viewManager.navigateTo(previousView);
            }
        }
    }
    
    void render() override {
        display.clear();
        display.setCursor(0, 0);
        display.print(message);
        display.setCursor(0, 1);
        display.print(confirmed ? "Yes" : "No");
    }
};
```

## Performance Optimization

### Display Update Optimization

```cpp
class OptimizedDisplay {
private:
    char lastLine1[17] = {0};
    char lastLine2[17] = {0};
    
public:
    void printf(uint8_t line, const char* format, ...) {
        char buffer[17];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        
        // Only update if changed
        if (line == 0 && strcmp(buffer, lastLine1) != 0) {
            display.setCursor(0, 0);
            display.print(buffer);
            strcpy(lastLine1, buffer);
        } else if (line == 1 && strcmp(buffer, lastLine2) != 0) {
            display.setCursor(0, 1);
            display.print(buffer);
            strcpy(lastLine2, buffer);
        }
    }
};
```

### Input Debouncing

```cpp
class AdvancedDebouncer {
private:
    static const uint8_t HISTORY_SIZE = 8;
    bool history[HISTORY_SIZE] = {false};
    uint8_t index = 0;
    
public:
    bool update(bool rawInput) {
        history[index] = rawInput;
        index = (index + 1) % HISTORY_SIZE;
        
        // Return true if all recent readings are the same
        bool first = history[0];
        for (uint8_t i = 1; i < HISTORY_SIZE; i++) {
            if (history[i] != first) return false;
        }
        return first;
    }
};
```

## Best Practices

### 1. Responsive UI

- Keep UI loop under 10ms
- Use non-blocking operations
- Batch display updates
- Minimize string operations

### 2. Memory Management

- Avoid dynamic allocation in UI loop
- Use fixed-size buffers
- Reuse objects when possible
- Pre-allocate common resources

### 3. User Experience

- Provide immediate feedback
- Use consistent navigation
- Show clear status indicators
- Implement undo where possible

### 4. Code Organization

- Separate view logic from hardware
- Use state machines for complex UI
- Keep event handlers short
- Document UI flow

## Troubleshooting

### Common UI Issues

1. **Unresponsive UI**:
   - Check for blocking operations
   - Verify input debouncing
   - Monitor CPU usage

2. **Display Flickering**:
   - Reduce update frequency
   - Implement dirty flag system
   - Check I2C timing

3. **Input Lag**:
   - Optimize debouncing
   - Check interrupt priorities
   - Reduce processing in input handlers

## Summary

The GenSeq UI system provides:
- Modular, component-based architecture
- Efficient hardware abstraction
- Responsive user experience
- Easy extensibility
- Clean separation of concerns

The system is designed to make adding new UI elements straightforward while maintaining performance and reliability.
