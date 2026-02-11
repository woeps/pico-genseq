# Development Workflow

## Overview

This guide outlines the recommended development workflow for the GenSeq project, including environment setup, coding practices, debugging procedures, and contribution guidelines.

## Initial Setup

### 1. Repository Setup

```bash
# Clone the repository
git clone https://github.com/woeps/pico-genseq.git
cd pico-genseq

# Create your feature branch
git checkout -b feature/your-feature-name

# Initialize submodules if any
git submodule update --init --recursive
```

### 2. Development Environment

#### VSCode Setup

1. Install required extensions:
   - Raspberry Pi Pico
   - C/C++ clangd
   - Cortex-Debug

2. Open the project:
   ```bash
   code .
   ```

3. Verify clangd is working:
   - Open a `.cpp` file
   - Check for code completion
   - Look for clangd status in status bar

#### Terminal Setup

```bash
# Add Pico SDK to PATH (add to ~/.bashrc)
export PICO_SDK_PATH=$HOME/.pico-sdk/sdk/2.1.1
export PICO_TOOLCHAIN_PATH=$HOME/.pico-sdk/toolchain/14_2_Rel1
export PATH=$PATH:$PICO_TOOLCHAIN_PATH/bin:$HOME/.pico-sdk/ninja/v1.12.1
```

### 3. First Build

```bash
# Create build directory
mkdir -p build
cd build

# Configure
cmake ..

# Build
ninja

# Should complete without errors
```

## Daily Development Workflow

### 1. Start of Day

```bash
# Pull latest changes
git checkout main
git pull origin main

# Update your branch
git checkout feature/your-feature
git rebase main

# Clean build
cd build
rm -rf *
cmake ..
ninja
```

### 2. Making Changes

#### Code Organization

Follow the directory structure:
```
src/
├── sequencer/     # Sequencer logic (Core 1)
├── ui/           # UI logic (Core 0)
├── commands/     # Inter-core communication
└── genseq.cpp    # Main entry point
```

#### Adding New Features

1. **UI Feature**:
   ```cpp
   // src/ui/views/NewView.h
   class NewView : public IView {
   public:
       void render() override;
       void handleInput(InputEvent event) override;
   };
   ```

2. **Sequencer Feature**:
   ```cpp
   // src/sequencer/new_feature.h
   namespace sequencer {
       class NewFeature {
       public:
           void update();
           bool isActive();
       };
   }
   ```

3. **New Command**:
   ```cpp
   // src/commands/command.h
   enum class CommandType {
       // ... existing commands
       NEW_FEATURE_TOGGLE,
   };
   ```

### 3. Build and Test

#### Incremental Build

```bash
# In build directory
ninja  # Only rebuilds changed files
```

#### Flash and Test

```bash
# Method 1: USB (quick testing)
cp genseq.uf2 /media/$(whoami)/RPI-RP2/

# Method 2: PicoProbe (development)
# Press F5 in VSCode
```

#### Test Procedure

1. **Unit Testing**:
   ```cpp
   // Simple test in main()
   #ifdef DEBUG
   testPitchSet();
   testGateSet();
   #endif
   ```

2. **Integration Testing**:
   - Test UI interaction
   - Verify MIDI output
   - Check timing accuracy

3. **Hardware Testing**:
   - Verify all buttons work
   - Check display updates
   - Test encoder response

### 4. Debugging

#### Serial Output

```cpp
// Enable debug output
printf("DEBUG: Pattern %d activated\n", patternId);

// Conditional debug
#ifdef DEBUG
printf("Sequencer tick: %d\n", tickCount);
#endif
```

#### Hardware Debugging

1. **PicoProbe Setup**:
   - Connect PicoProbe
   - Use VSCode debug configuration
   - Set breakpoints

2. **Common Debug Points**:
   ```cpp
   // In sequencer.cpp
   void sequencerTick() {
       // Break here to check timing
       if (shouldSendMIDI()) {
           sendMIDIMessage();
       }
   }
   
   // In ui.cpp
   void handleButtonPress() {
       // Break here to check input
       processButton(buttonId);
   }
   ```

#### Logic Analyzer

For timing-critical debugging:
- Monitor UART for MIDI
- Check I2C for display
- Verify GPIO timing

## Coding Standards

### 1. Code Style

#### C++ Guidelines

```cpp
// Naming conventions
class ClassName {          // PascalCase
public:
    void methodName();     // camelCase
private:
    int memberVariable;    // camelCase with underscore
    static const int CONSTANT_VALUE; // UPPER_SNAKE_CASE
};

// File organization
// header.h
#pragma once

#include <vector>

namespace sequencer {  // Use namespaces
    class MyClass {
        // Class definition
    };
}
```

#### C Guidelines

```c
// For C files (PIO drivers, etc.)
// snake_case for functions
#define CONSTANT_NAME "value"

struct struct_name {
    int member_name;
};

void function_name(struct struct_name* s);
```

### 2. Documentation

#### Header Documentation

```cpp
/**
 * @brief Manages MIDI pattern playback
 * 
 * The Pattern class combines pitch and rhythm sets to create
 * playable musical patterns. Multiple patterns can run
 * simultaneously.
 */
class Pattern {
public:
    /**
     * @brief Create a new pattern
     * @param pitchSet Pointer to pitch data
     * @param gateSet Pointer to rhythm data
     */
    Pattern(PitchSet* pitchSet, GateSet* gateSet);
};
```

#### Inline Documentation

```cpp
// Update the sequencer state
// This must be called every 24 PPQN ticks
void sequencer::update() {
    // Check for new commands from UI
    if (hasPendingCommands()) {
        processCommands();
    }
    
    // Advance pattern position
    currentStep = (currentStep + 1) % patternLength;
}
```

### 3. Error Handling

```cpp
// Use assertions for debugging
#include <cassert>

void setPatternLength(uint8_t length) {
    assert(length > 0 && length <= MAX_PATTERN_LENGTH);
    patternLength = length;
}

// Return error codes
enum class ErrorCode {
    SUCCESS,
    INVALID_PATTERN,
    MEMORY_ERROR,
};

ErrorCode loadPattern(uint8_t id) {
    if (id >= MAX_PATTERNS) {
        return ErrorCode::INVALID_PATTERN;
    }
    // ... load pattern
    return ErrorCode::SUCCESS;
}
```

## Git Workflow

### 1. Branch Strategy

```
main                 ← Production-ready code
├── develop          ← Integration branch
├── feature/ui-redesign
├── feature/euclidean-rhythm
└── hotfix/midi-timing
```

### 2. Commit Guidelines

#### Commit Message Format

```
type(scope): brief description

Detailed explanation if needed.

Closes #123
```

Types:
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation
- `style`: Code style
- `refactor`: Refactoring
- `test`: Testing
- `chore`: Maintenance

#### Examples

```
feat(sequencer): add euclidean rhythm generator

Implements Bjorklund's algorithm for generating
euclidean rhythm patterns. Users can set number
of pulses and total steps.

Closes #45

fix(ui): correct encoder direction

Encoder was reading backwards due to swapped
phase A and B pins in hardware config.
```

### 3. Pull Request Process

1. **Create PR**:
   - Push feature branch
   - Create pull request on GitHub
   - Request review from maintainer

2. **PR Checklist**:
   - [ ] Code builds without warnings
   - [ ] All tests pass
   - [ ] Documentation updated
   - [ ] Hardware tested
   - [ ] No regressions

3. **Review Process**:
   - Address reviewer comments
   - Update code as needed
   - Maintain clean commit history

## Testing Strategy

### 1. Unit Testing

```cpp
// Simple test framework
#define TEST_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            printf("FAIL: %s:%d\n", __FILE__, __LINE__); \
            return false; \
        } \
    } while(0)

bool testPitchSet() {
    PitchSet ps;
    ps.addNote(60, 127);  // C4, max velocity
    TEST_ASSERT(ps.getNoteCount() == 1);
    TEST_ASSERT(ps.getNote(0) == 60);
    return true;
}
```

### 2. Integration Testing

```cpp
// Test inter-core communication
void testCommandSystem() {
    // Send command from UI core
    Command cmd = {CommandType::PLAY, 0};
    multicore_fifo_push_blocking(cmd.raw);
    
    // Check if sequencer receives
    sleep_ms(10);
    TEST_ASSERT(sequencer.isPlaying());
}
```

### 3. Hardware Testing

Create test hardware setup:
- Test all button combinations
- Verify display characters
- Check MIDI output with monitor

## Performance Optimization

### 1. Profiling

```cpp
// Simple timing measurement
uint32_t start = time_us_32();
functionToProfile();
uint32_t duration = time_us_32() - start;
printf("Function took %d microseconds\n", duration);
```

### 2. Optimization Targets

- **MIDI Timing**: Must be < 1ms jitter
- **UI Response**: < 50ms latency
- **Memory Usage**: < 128KB RAM
- **CPU Usage**: < 80% per core

### 3. Common Optimizations

```cpp
// Use fixed-point instead of float
#define FIXED_POINT_SCALE 1000
int32_t bpmFixed = 120 * FIXED_POINT_SCALE;

// Use lookup tables
static const uint16_t sineTable[256] = { ... };

// Minimize inter-core communication
batchCommands();
```

## Release Process

### 1. Version Bumping

Update version in:
- `CMakeLists.txt`
- `README.md`
- Create git tag

### 2. Release Build

```bash
# Clean release build
rm -rf build
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
ninja

# Create release package
tar -czf genseq-v1.0.0.tar.gz genseq.uf2 README.md
```

### 3. Documentation

- Update changelog
- Review documentation
- Create release notes

## Troubleshooting

### Common Development Issues

1. **Build Fails**:
   ```bash
   # Clean and rebuild
   rm -rf build
   mkdir build && cd build
   cmake .. && ninja
   ```

2. **clangd Not Working**:
   - Check `.clangd` file
   - Verify `compile_commands.json`
   - Restart VSCode

3. **Debug Not Connecting**:
   - Check PicoProbe wiring
   - Verify OpenOCD version
   - Try external OpenOCD

4. **Hardware Not Responding**:
   - Check power supply
   - Verify pin assignments
   - Test with multimeter

## Best Practices Summary

1. **Code Organization**: Keep core-specific code separate
2. **Testing**: Test early and often
3. **Documentation**: Document as you code
4. **Version Control**: Clean commit history
5. **Performance**: Profile before optimizing
6. **Hardware**: Protect against static discharge
7. **Collaboration**: Review code before merging

## Resources

- [Pico SDK Documentation](https://raspberrypi.github.io/pico-sdk-doxygen/)
- [RP2040 Datasheet](https://datasheets.raspberrypi.org/rp2040/rp2040-datasheet.pdf)
- [MIDI Specification](https://www.midi.org/specifications)
- [CMake Documentation](https://cmake.org/documentation/)
