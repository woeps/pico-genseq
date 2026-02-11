# Repository Structure

## Overview

The GenSeq project follows a well-organized structure that separates concerns between the user interface and sequencer logic, leveraging the dual-core architecture of the Raspberry Pi Pico (RP2040).

```
genseq/
├── src/                    # All source code
│   ├── sequencer/         # Core sequencing logic (runs on core 1)
│   ├── ui/                # User interface components (runs on core 0)
│   ├── commands/          # Inter-core communication
│   ├── generated/         # Auto-generated PIO headers
│   └── genseq.cpp         # Main entry point
├── docs/                  # Documentation
├── build/                 # Build output directory
├── case/                  # Hardware case designs
├── data/                  # Datasheet & Spec files
├── .vscode/               # VSCode configuration
├── .clangd                # clangd configuration
├── CMakeLists.txt         # CMake build configuration
├── pico_sdk_import.cmake  # Pico SDK import
└── README.md              # Project overview
```

## Architecture Philosophy

### Dual-Core Separation

The project strategically uses both cores of the RP2040:

- **Core 0**: User Interface (UI)
  - Handles all user input (buttons, encoders)
  - Manages display updates
  - Sends commands to sequencer
  - Responsive to user interactions
  
- **Core 1**: Sequencer Logic
  - Real-time MIDI sequencing
  - Pattern management
  - Timing-critical operations
  - Uninterrupted MIDI output

### Benefits of This Architecture

1. **Real-time Performance**: MIDI timing is never affected by UI operations
2. **Responsiveness**: UI remains smooth even during complex sequencing
3. **Modularity**: Clear separation allows independent development
4. **Scalability**: Easy to add new UI features without affecting timing
5. **Debugging**: Issues can be isolated to specific cores

### Potential Pitfalls

1. **Communication Overhead**: Inter-core communication adds latency
2. **Synchronization**: Must avoid race conditions when sharing data
3. **Memory Management**: Each core has its own stack, careful with shared memory
4. **Debugging Complexity**: Core-specific issues can be harder to trace

## Directory Deep Dive

### `/src/sequencer/`

The heart of the sequencing engine:

- `sequencer.h/cpp` - Main sequencer controller
- `pattern.h/cpp` - Pattern management and playback
- `gate_set.h/cpp` - Rhythm/gate timing data
- `pitch_set.h/cpp` - Note pitch data
- `velocity_set.h/cpp` - Note velocity data
- `midi_messages.h` - MIDI message definitions

### `/src/ui/`

User interface subsystem:

```
ui/
├── controllers/         # UI control logic
├── hardware/            # Hardware abstraction
│   ├── components/      # Individual hardware drivers
│   ├── driver/          # Low-level drivers
│   └── interfaces/      # Hardware interfaces
├── navigation/          # View management
└── views/               # UI screens
```

- `ui.h/cpp` - Main UI controller
- `controllers/` - High-level UI logic
- `hardware/` - Hardware abstraction layer (HAL)
- `navigation/` - View management system
- `views/` - Individual UI screens

### `/src/commands/`

Inter-core communication:

- `command.h/cpp` - Command message definitions
- Enables UI to send instructions to sequencer

### `/src/generated/`

Auto-generated files:

- PIO program headers (e.g., `ws2812.pio.h`)
- Generated at build time from `.pio` files

## Key Architectural Patterns

### 1. Command Pattern

The UI sends commands to the sequencer:
```cpp
// Example command flow
UI Core --[Play Command]--> Sequencer Core
```

### 2. Data Separation

Musical data is separated into:
- **Pitch Sets**: Note pitches and velocities
- **Rhythm Sets**: Timing and gate information
- **Patterns**: Combinations of pitch and rhythm sets

### 3. Hardware Abstraction

UI hardware is abstracted through interfaces:
- `IInput` - Input devices (encoders, buttons)
- `IOutput` - Output devices (LEDs, display)

## Development Considerations

### Adding New Features

1. **UI Features**: Add to `/src/ui/` namespace
2. **Sequencer Features**: Add to `/src/sequencer/` namespace
3. **New Commands**: Define in `/src/commands/`
4. **Hardware Support**: Add to `/src/ui/hardware/`

### Best Practices

1. Keep core-specific code isolated
2. Use commands for cross-core communication
3. Avoid shared mutable state between cores
4. Test timing-critical code on the sequencer core
5. Keep UI responsive, avoid blocking operations

### File Organization Guidelines

- Header files (`.h`) should contain interfaces and declarations
- Implementation files (`.cpp`) contain actual logic
- Keep related functionality in the same directory
- Use forward declarations to minimize include dependencies

## Build Artifacts

The build process creates:
- `genseq.uf2` - UF2 file for USB flashing
- `genseq.elf` - ELF binary for debugging
- `compile_commands.json` - For clangd language server
- Various `.bin` and `.hex` files

## Summary

This repository structure supports a clean, maintainable codebase that leverages the RP2040's dual-core architecture effectively. The separation of UI and sequencing concerns ensures real-time performance while maintaining code organization and extensibility.
