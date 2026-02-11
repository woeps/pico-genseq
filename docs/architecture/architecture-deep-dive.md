# Architecture Deep Dive

## Overview

The GenSeq architecture leverages the RP2040's dual-core design to create a responsive real-time MIDI sequencer. This deep dive explores the technical decisions, data structures, and communication patterns that make it possible.

## Dual-Core Architecture

### Core Responsibilities

```
┌─────────────────┐    Commands     ┌─────────────────┐
│   Core 0        │ ──────────────► │   Core 1        │
│                 │                 │                 │
│ • UI Input      │                 │ • Sequencing    │
│ • Display       │                 │ • MIDI Output   │
│ • LEDs          │                 │ • Timing        │
│ • User Logic    │                 │ • Pattern Mgmt  │
│                 │                 │                 │
└─────────────────┘                 └─────────────────┘
```

### Why Dual-Core?

1. **Real-time Constraints**: MIDI timing must be precise (±1ms)
2. **UI Responsiveness**: User interactions should never affect timing
3. **Separation of Concerns**: Clean architecture boundaries
4. **Future Expansion**: Room for additional features

### Core Synchronization

The cores communicate through:
- **Mailbox**: Fast inter-core messaging
- **Command Queue**: Asynchronous command processing
- **Shared Memory**: Read-only data structures

## Data Architecture

### Musical Data Separation

The sequencer separates musical data into three distinct types:

```
Pattern
├── Pitch Set      (What notes to play)
├── Rhythm Set     (When to play them)
└── Play Mode      (How to play them)
```

#### Pitch Set
```cpp
class PitchSet {
    std::vector<uint8_t> pitches;    // MIDI note numbers
    std::vector<uint8_t> velocities; // Note velocities
    bool active;                     // Enable/disable
};
```

- Contains note information (C4, G4, etc.)
- Independent of timing
- Can be reused across patterns

#### Rhythm Set (Gate Set)
```cpp
class GateSet {
    std::vector<bool> gates;         // On/off for each step
    std::vector<uint8_t> lengths;    // Gate length percentage
    uint8_t length;                  // Number of steps
    bool active;                     // Enable/disable
};
```

- Defines timing and rhythm
- Euclidean algorithm for generation
- Gate length controls note duration

#### Pattern
```cpp
class Pattern {
    PitchSet* pitchSet;              // Reference to pitch data
    GateSet* gateSet;                // Reference to rhythm data
    PlayMode playMode;               // Playback algorithm
    uint8_t currentStep;             // Current position
    bool active;                     // Play this pattern?
};
```

- Combines pitch and rhythm sets
- Multiple patterns can share sets
- Each pattern has independent state

### Play Modes

Different algorithms for note selection:

1. **Forward**: Linear progression through steps
2. **Random**: Random note selection
3. **Euclidean**: Evenly spaced notes
4. **Reverse**: Backward progression

## Command System

### Command Structure

```cpp
enum class CommandType {
    PLAY,
    STOP,
    SET_BPM,
    SET_PATTERN_ACTIVE,
    MODIFY_PITCH_SET,
    MODIFY_GATE_SET,
    // ... more commands
};

struct Command {
    CommandType type;
    uint8_t data[4];  // Command-specific data
};
```

### Command Flow

```
UI Core (Core 0)              Sequencer Core (Core 1)
     │                              │
     ├─> User presses PLAY ─────────► │
     │                              │
     ├─> Create Command             │
     │    type = PLAY               │
     │                              │
     ├─> Send via Mailbox ──────────►│
     │                              │
     │                         Process Command
     │                              │
     │                         Start Sequencer
     │                              │
     │                         Send MIDI Clock
```

### Command Processing

The sequencer processes commands in its main loop:
```cpp
void sequencerLoop() {
    while (true) {
        // Check for commands
        if (multicore_fifo_rvalid()) {
            Command cmd = multicore_fifo_pop_blocking();
            processCommand(cmd);
        }
        
        // Update sequencing
        if (shouldTick()) {
            processTick();
        }
        
        // Send MIDI
        sendPendingMIDI();
    }
}
```

## Timing System

### Tick-Based Sequencing

The sequencer uses a tick-based timing system:

- **PPQN**: Pulses Per Quarter Note (typically 24)
- **BPM**: Beats Per Minute (user configurable)
- **Tick Interval**: 60,000,000 / (BPM * PPQN * 1000) microseconds

### Timing Precision

```cpp
// Timer interrupt for precise timing
void timerCallback() {
    sequencerTick();
    clearAlarm();
    setAlarm(nextTickTime);
}
```

### MIDI Clock Generation

MIDI clock messages are sent at 24 PPQN:
```cpp
void sendMIDIClock() {
    sendMIDIMessage(0xF8); // MIDI Clock message
}
```

## UI Architecture

### Component Hierarchy

```
UIController
├── ViewManager
│   ├── MainView
│   ├── PatternView
│   └── SettingsView
├── Hardware Layer
│   ├── Button Matrix
│   ├── Encoder
│   ├── Display
│   └── LED Matrix
└── Command Generator
```

### Hardware Abstraction

The UI uses a clean hardware abstraction:

```cpp
// Interface for input devices
class IInput {
public:
    virtual void update() = 0;
    virtual bool hasChanged() = 0;
    virtual int getValue() = 0;
};

// Interface for output devices
class IOutput {
public:
    virtual void update() = 0;
    virtual void setValue(int value) = 0;
};
```

### View Management

The view system manages different UI screens:

```cpp
class ViewManager {
private:
    IView* currentView;
    std::vector<IView> views;
    
public:
    void navigateTo(ViewID id);
    void handleInput(InputEvent event);
    void render();
};
```

## Memory Management

### Core-Specific Memory

Each core has its own stack:
```cpp
// Core 0 stack (UI)
#define UI_STACK_SIZE 0x1000

// Core 1 stack (Sequencer)
#define SEQUENCER_STACK_SIZE 0x2000
```

### Shared Memory

Read-only data can be shared:
```cpp
// Flash-stored data accessible by both cores
const uint8_t* sharedData = (const uint8_t*)0x10000000;
```

### Dynamic Allocation

Minimal dynamic allocation to avoid fragmentation:
- Pre-allocated buffers
- Fixed-size arrays
- Pool allocators for frequent allocations

## Interrupt Handling

### Core 0 Interrupts

- **GPIO**: Button and encoder inputs
- **I2C**: Display communication
- **Timer**: UI refresh timing

### Core 1 Interrupts

- **Timer**: Sequencer tick
- **UART**: MIDI transmission
- **DMA**: Efficient data transfer

### PIO Programs

Custom PIO programs for hardware:

```pio
// Encoder reading PIO
program encoder_reader {
    set pins, 0
    set x, 0
    
    label loop:
    in pins, 2
    mov x, isr
    jmp x != y, changed
    jmp loop
    
    label changed:
    mov y, x
    irq 0
    jmp loop
}
```

## Performance Considerations

### Critical Sections

MIDI output must be uninterrupted:
```cpp
// Disable interrupts during critical MIDI operations
uint32_t interrupt_state = save_and_disable_interrupts();
sendMIDIMessage(msg);
restore_interrupts(interrupt_state);
```

### DMA Usage

DMA for efficient data transfer:
```cpp
// DMA for LED matrix updates
dma_channel_configure(dma_chan,
    &dma_config,
    &led_data,          // Destination
    &led_buffer,        // Source
    LED_COUNT,          // Transfer count
    true                // Start immediately
);
```

### Optimization Strategies

1. **Minimize Inter-Core Communication**: Batch commands
2. **Use Lookup Tables**: For common calculations
3. **Avoid Floating Point**: Use fixed-point arithmetic
4. **Cache Frequently Used Data**: Keep in fast memory

## Debugging Architecture

### Core-Specific Debugging

Each core can be debugged independently:
```bash
# Debug Core 0 (UI)
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "bp main.0"

# Debug Core 1 (Sequencer)
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "bp main.1"
```

### Logging

Different logging levels for each core:
```cpp
// UI logging
printf("UI: Button pressed %d\n", buttonId);

// Sequencer logging
printf("SEQ: Pattern %d tick %d\n", patternId, tick);
```

## Future Architecture Considerations

### Scalability

The architecture supports:
- Additional patterns
- New play modes
- More UI views
- Extra hardware components

### Extensibility

Adding new features:
1. UI features: Add to Core 0
2. Sequencer features: Add to Core 1
3. New commands: Extend command enum
4. New hardware: Add to HAL

### Performance Headroom

Current utilization:
- Core 0: ~30% (UI is lightweight)
- Core 1: ~60% (Sequencing is intensive)
- Memory: ~50% of RAM available

## Summary

The GenSeq architecture successfully separates UI and sequencing concerns across two cores, enabling real-time MIDI performance while maintaining responsive user interaction. The clean data architecture and command system provide a solid foundation for future enhancements.
