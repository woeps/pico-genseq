# Troubleshooting FAQ

## Overview

This guide covers common issues encountered while developing, building, and running the GenSeq MIDI sequencer. Problems are categorized by area: build issues, runtime problems, hardware failures, and performance concerns.

## Build Issues

### SDK Not Found

**Error**: `CMake Error: Could not find a package configuration file provided by "Pico"`

**Solutions**:
```bash
# Set environment variable
export PICO_SDK_PATH=$HOME/.pico-sdk/sdk/2.1.1

# Or add to ~/.bashrc
echo 'export PICO_SDK_PATH=$HOME/.pico-sdk/sdk/2.1.1' >> ~/.bashrc
source ~/.bashrc

# Verify installation
ls $PICO_SDK_PATH/pico_sdk_init.cmake
```

### Toolchain Not Found

**Error**: `arm-none-eabi-gcc: No such file or directory`

**Solutions**:
```bash
# Set toolchain path
export PICO_TOOLCHAIN_PATH=$HOME/.pico-sdk/toolchain/14_2_Rel1
export PATH=$PATH:$PICO_TOOLCHAIN_PATH/bin

# Verify installation
arm-none-eabi-gcc --version

# Reinstall if missing
cd ~/.pico-sdk
wget https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz
tar -xf arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz
```

### CMake Configuration Fails

**Error**: `CMake Error: Could NOT find Ninja`

**Solutions**:
```bash
# Use system ninja
sudo apt install ninja-build

# Or use Pico SDK version
export PATH=$PATH:$HOME/.pico-sdk/ninja/v1.12.1

# Force CMake to find ninja
cmake -GNinja ..
```

### Compilation Errors

**Error**: `fatal error: 'pico/stdlib.h' file not found`

**Solutions**:
1. Check CMakeLists.txt for `pico_sdk_init()` call
2. Verify include directories:
   ```cmake
   target_include_directories(genseq PRIVATE
       ${CMAKE_CURRENT_LIST_DIR}/src
   )
   ```
3. Clean rebuild:
   ```bash
   rm -rf build
   mkdir build && cd build
   cmake .. && ninja
   ```

### Linker Errors

**Error**: `undefined reference to 'uart_init'`

**Solutions**:
```cmake
# Add missing libraries to CMakeLists.txt
target_link_libraries(genseq
    pico_stdlib
    hardware_uart  # Add missing hardware library
    # ... other libraries
)
```

## Runtime Issues

### Device Not Responding

**Symptoms**: No MIDI output, no display, no LED activity

**Debug Steps**:
1. Check power supply:
   ```bash
   # Verify voltage at 3V3 pin (should be 3.3V)
   # Check USB cable is data-capable
   ```

2. Check if firmware flashed:
   ```bash
   # Hold BOOTSEL, connect USB
   # Check if RPI-RP2 drive appears
   ls /media/$(whoami)/RPI-RP2/
   ```

3. Enable debug output:
   ```cpp
   // In genseq.cpp
   pico_enable_stdio_usb(genseq, 1);  // Enable USB stdio
   
   // Add debug prints
   printf("GenSeq started on core %d\n", get_core_num());
   ```

### MIDI Not Working

**Symptoms**: No MIDI output, stuck notes, wrong notes

**Debug Steps**:
1. Verify UART configuration:
   ```cpp
   // Check pin assignments
   #define MIDI_UART_PIN_TX 8
   #define MIDI_UART_PIN_RX 9
   
   // Verify baud rate
   uart_init(MIDI_UART, 31250);
   ```

2. Test with MIDI monitor:
   ```bash
   # Connect to USB MIDI adapter
   # Use software like MIDI-OX or amidi
   amidi -l  # List MIDI devices
   amidi -p hw:2,0 -d  # Monitor MIDI
   ```

3. Check MIDI wiring:
   ```
   Pico TX (GPIO8) ── 220Ω ── MIDI Pin 5
   Pico RX (GPIO9) ── 220Ω ── MIDI Pin 4
   GND ───────────────────── MIDI Pin 2
   ```

### Display Issues

**Symptoms**: No display, garbled text, random characters

**Debug Steps**:
1. Check I2C connection:
   ```cpp
   // Scan for I2C devices
   void scan_i2c() {
       for (uint8_t addr = 0x08; addr < 0x78; addr++) {
           if (i2c_write_blocking(i2c0, addr, NULL, 0, false) >= 0) {
               printf("Found I2C device at 0x%02X\n", addr);
           }
       }
   }
   ```

2. Verify I2C address:
   ```cpp
   // Default is 0x27, but can be 0x3F
   #define DISPLAY_I2C_ADDR 0x27
   ```

3. Check pull-up resistors:
   - SDA and SCL need 4.7kΩ pull-ups to 3.3V
   - Some modules have built-in pull-ups

### Encoder Problems

**Symptoms**: Jittery movement, wrong direction, no response

**Debug Steps**:
1. Check encoder wiring:
   ```
   Encoder Pin A → GPIO14
   Encoder Pin B → GPIO15
   Encoder Button → GPIO20
   ```

2. Add hardware debouncing:
   ```
   Add 0.1µF capacitors between:
   - Pin A and GND
   - Pin B and GND
   ```

3. Verify PIO program:
   ```cpp
   // Check PIO is initialized
   pio_sm_init(pio, sm, offset, &encoder_program_default_config(
       pio_gpio_setup(pio, ENCODER_PIN_A, ENCODER_PIN_B)
   ));
   ```

## Hardware Issues

### Button Matrix Not Working

**Symptoms**: Some buttons not responding, multiple buttons detected

**Debug Steps**:
1. Check matrix wiring:
   ```
   Rows: GPIO22, GPIO21, GPIO20, GPIO19
   Cols: GPIO18, GPIO17, GPIO16, GPIO15
   ```

2. Test individual buttons:
   ```cpp
   // Direct GPIO test
   void testButton(uint8_t pin) {
       gpio_init(pin);
       gpio_set_dir(pin, GPIO_IN);
       gpio_pull_up(pin);
       
       while (true) {
           printf("Pin %d: %d\n", pin, !gpio_get(pin));
           sleep_ms(100);
       }
   }
   ```

3. Check for shorts:
   - Verify no solder bridges
   - Check continuity with multimeter

### LED Strip Issues

**Symptoms**: LEDs not lighting, wrong colors, flickering

**Debug Steps**:
1. Check power supply:
   - WS2812B needs 5V, not 3.3V
   - Each LED can draw up to 60mA

2. Verify data line:
   ```
   Pico GPIO23 → LED Data In
   Add 330Ω resistor in series
   Add 1000µF capacitor across 5V/GND
   ```

3. Test with simple pattern:
   ```cpp
   void testLEDs() {
       for (int i = 0; i < LED_COUNT; i++) {
           setLED(i, 255, 0, 0);  // Red
           updateLEDs();
           sleep_ms(100);
           setLED(i, 0, 0, 0);
           updateLEDs();
       }
   }
   ```

### Power Issues

**Symptoms**: Random reboots, brownouts, erratic behavior

**Debug Steps**:
1. Measure current draw:
   - Pico: ~40mA
   - Display: ~20mA
   - LEDs: up to 2A (all white)
   
2. Check voltage levels:
   ```
   5V rail: Should be stable under load
   3V3 rail: Should be 3.3V ±5%
   ```

3. Add decoupling capacitors:
   ```
   100µF near power input
   10µF near Pico
   0.1µF near each IC
   ```

## Performance Issues

### MIDI Timing Jitter

**Symptoms**: Inconsistent timing, rhythmic errors

**Debug Steps**:
1. Check timer configuration:
   ```cpp
   // Verify timer priority
   hardware_alarm_set_callback(MIDI_TIMER_ALARM, midiClockCallback);
   
   // Check interval calculation
   uint32_t interval = 2500000 / bpm;  // microseconds
   ```

2. Measure actual timing:
   ```cpp
   // Add timing debug
   static uint32_t lastTime = 0;
   uint32_t now = time_us_32();
   printf("Tick interval: %d us\n", now - lastTime);
   lastTime = now;
   ```

3. Optimize interrupt handling:
   ```cpp
   // Keep ISRs short
   void midiClockCallback() {
       // Just set flag, process in main loop
       midiClockFlag = true;
   }
   ```

### UI Lag

**Symptoms**: Slow response to input, delayed display updates

**Debug Steps**:
1. Profile UI loop:
   ```cpp
   // Measure loop time
   uint32_t start = time_us_32();
   uiMainLoopIteration();
   uint32_t duration = time_us_32() - start;
   printf("UI loop: %d us\n", duration);
   ```

2. Optimize display updates:
   ```cpp
   // Only update when needed
   if (displayDirty) {
       updateDisplay();
       displayDirty = false;
   }
   ```

3. Reduce blocking operations:
   ```cpp
   // Use non-blocking delays
   uint32_t lastUpdate = 0;
   if (time_us_32() - lastUpdate > UPDATE_INTERVAL) {
       // Update display
       lastUpdate = time_us_32();
   }
   ```

## Debugging Techniques

### Serial Debug Output

```cpp
// Enable different debug levels
#define DEBUG_NONE    0
#define DEBUG_ERROR   1
#define DEBUG_INFO    2
#define DEBUG_VERBOSE 3

#define DEBUG_LEVEL DEBUG_INFO

#define DEBUG_PRINT(level, fmt, ...) \
    do { if (level <= DEBUG_LEVEL) \
        printf("[%lu] " fmt "\n", time_us_32(), ##__VA_ARGS__); \
    } while(0)

// Usage
DEBUG_PRINT(DEBUG_INFO, "Pattern %d started", patternId);
DEBUG_PRINT(DEBUG_ERROR, "Failed to send MIDI: %d", error);
```

### Logic Analyzer

Essential signals to monitor:
1. **MIDI TX** (GPIO8): Verify MIDI messages
2. **I2C SDA/SCL** (GPIO4/5): Check display communication
3. **Encoder pins** (GPIO14/15): Verify encoder signals
4. **LED data** (GPIO23): Check WS2812 timing

### GDB Debugging

```bash
# Connect with PicoProbe
arm-none-eabi-gdb build/genseq.elf
(gdb) target remote localhost:3333
(gdb) monitor reset init
(gdb) load
(gdb) break main
(gdb) continue
```

### Core-Specific Debugging

```bash
# Debug specific core
(gdb) break core1_main
(gdb) info threads
(gdb) thread 2  # Switch to core 1
```

## Common Error Codes

### Flash Errors

| Error | Cause | Solution |
|-------|-------|----------|
| "No drive" | BOOTSEL not pressed | Hold BOOTSEL while connecting USB |
| "Copy failed" | Insufficient space | Delete old UF2 files first |
| "Corrupted" | Incomplete copy | Re-copy UF2 file |

### Runtime Errors

| Error | Cause | Solution |
|-------|-------|----------|
| Hard fault | Memory access violation | Check array bounds, pointer validity |
| Stack overflow | Too much recursion/local vars | Increase stack size, reduce locals |
| Watchdog reset | Loop too long | Break long loops, use watchdog_disable() |

## Recovery Procedures

### Bricked Device

1. **Hold BOOTSEL** while connecting USB
2. **Flash fresh firmware**:
   ```bash
   # Flash a simple blink program first
   cp blink.uf2 /media/$(whoami)/RPI-RP2/
   ```
3. **If still not working**:
   - Check for physical damage
   - Try different USB cable/port
   - Test with known good Pico

### Corrupted Build

```bash
# Complete clean
rm -rf build
git clean -fdx  # Remove all untracked files
git reset --hard HEAD  # Reset to last commit

# Rebuild
mkdir build && cd build
cmake .. && ninja
```

### Lost Source Code

```bash
# Use git reflog to find lost commits
git reflog

# Restore lost commit
git checkout <commit-hash>

# Or clone fresh copy
git clone https://github.com/woeps/pico-genseq.git
```

## Getting Help

### Debug Information to Collect

1. **Build environment**:
   ```bash
   uname -a
   cmake --version
   arm-none-eabi-gcc --version
   ```

2. **Project status**:
   ```bash
   git status
   git log --oneline -5
   ```

3. **Error logs**:
   - Full build output
   - Runtime serial output
   - GDB backtrace

### Community Resources

- **GitHub Issues**: Create detailed issue with logs
- **Pico SDK Forums**: https://forums.raspberrypi.com/
- **RP2040 Datasheet**: For hardware questions
- **MIDI.org**: For MIDI specification questions

## Prevention Tips

### Development Practices

1. **Commit often**: Small, logical commits
2. **Test changes**: Verify after each modification
3. **Backup config**: Save working CMakeLists.txt
4. **Document changes**: Update README with modifications

### Hardware Protection

1. **ESD protection**: Use anti-static measures
2. **Current limiting**: Add fuses where appropriate
3. **Polarity protection**: Diode on power input
4. **Overvoltage protection**: TVS diodes on I/O

### Code Safety

1. **Bounds checking**: Prevent buffer overflows
2. **Input validation**: Check all user inputs
3. **Error handling**: Graceful failure modes
4. **Watchdog**: Enable for production code

## Summary

Most issues fall into these categories:
- **Build**: Environment setup problems
- **Runtime**: Configuration or logic errors
- **Hardware**: Wiring or component failures
- **Performance**: Timing or optimization needs

Systematic debugging with the right tools and techniques can resolve most problems quickly. Always start with the simplest explanation and work toward more complex causes.
