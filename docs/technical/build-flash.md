# Build & Flash Guide

## Overview

This guide explains how to build the GenSeq firmware and flash it onto a Raspberry Pi Pico. Two methods are covered: USB flashing (simple) and PicoProbe debugging (advanced).

## Prerequisites

Before building or flashing, ensure:
- Pico SDK is installed (`~/.pico-sdk/sdk/2.1.1/`)
- ARM toolchain is installed (`~/.pico-sdk/toolchain/14_2_Rel1/`)
- CMake and Ninja are available
- VSCode with Pico extension is installed (recommended)

## Building the Project

### Method 1: Using VSCode Tasks

1. Open the project in VSCode
2. Press `Ctrl+Shift+P` (or `Cmd+Shift+P` on Mac)
3. Select "Tasks: Run Task"
4. Choose "Build" or "Flash"

### Method 2: Command Line Build

```bash
# Navigate to project root
cd /home/chris/Projects/c/pico/genseq

# Create and enter build directory
mkdir -p build
cd build

# Configure with CMake
cmake ..

# Build the project
ninja
```

### Build Outputs

Successful build creates:
- `genseq.uf2` - UF2 file for USB flashing
- `genseq.elf` - ELF binary for debugging
- `genseq.bin` - Raw binary file
- `genseq.hex` - Intel HEX format

### Build Options

#### Debug Build
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
ninja
```
- Includes debug symbols
- Disables optimizations
- Larger binary size

#### Release Build
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
ninja
```
- Optimized for size and speed
- No debug symbols
- Smaller binary size

#### Verbose Build
```bash
ninja -v
```
Shows all compiler commands for troubleshooting.

## Flashing Methods

### Method 1: USB Flashing (Simple)

This is the easiest method for initial flashing or updates.

#### Steps:

1. **Connect Pico in BOOTSEL mode**:
   - Hold the BOOTSEL button on the Pico
   - Connect USB cable while holding BOOTSEL
   - Release BOOTSEL button
   - Pico appears as USB mass storage device

2. **Copy UF2 file**:
   ```bash
   # On Linux
   cp build/genseq.uf2 /media/$(whoami)/RPI-RP2/
   
   # Or manually drag and drop in file manager
   ```

3. **Wait for completion**:
   - File copy completes automatically
   - Pico reboots and runs the firmware
   - USB storage device disappears

#### Advantages:
- No special hardware needed
- Works on any OS
- Simple and reliable

#### Disadvantages:
- No debugging capability
- Requires physical access to BOOTSEL button
- Slower for repeated flashing

### Method 2: PicoProbe Flashing (Advanced)

PicoProbe allows debugging and flashing without BOOTSEL mode.

#### Hardware Setup:

1. **Create a PicoProbe**:
   - Use another Pico as a debug probe
   - Flash PicoProbe firmware onto it
   - Connect between target Pico and computer

2. **Wiring**:
   ```
   PicoProbe        Target Pico
   ---------        ----------
   GP2   <---->     SWDIO
   GP3   <---->     SWCLK
   GND   <---->     GND
   GP4/5 <---->     UART (optional)
   ```

#### Software Setup:

1. **Install OpenOCD**:
   ```bash
   # Already included in Pico SDK at:
   ~/.pico-sdk/openocd/0.12.0+dev/openocd
   ```

2. **Configure IDE**:
   - Debug configuration is in `.vscode/launch.json`
   - Uses Cortex-Debug extension
   - Paths to ELF, GDB, OpenOCD, and target config are hardcoded for the RP2040 board
   - **Note**: Windsurf IDE does not support `${command:raspberry-pi-pico.*}` variables from the Pico VS Code extension — all paths must be explicit

#### Flashing with PicoProbe:

1. **Via VSCode**:
   - Press `F5` or go to Run & Debug
   - Select "Pico Debug (Cortex-Debug)"
   - Automatically flashes and starts debugging

2. **Via Command Line**:
   ```bash
   # Start OpenOCD
   ~/.pico-sdk/openocd/0.12.0+dev/openocd \
     -f interface/cmsis-dap.cfg \
     -f target/rp2040.cfg \
     -c "program build/genseq.elf verify reset exit"
   ```

#### Advantages:
- No BOOTSEL button needed
- Supports debugging
- Faster for development
- Can flash while running

#### Disadvantages:
- Requires additional hardware
- More complex setup
- Needs proper wiring

## Method 3: Picotool Command Line

Picotool provides command-line access to Pico features.

#### Installation:
Already included with Pico SDK at `~/.pico-sdk/picotool/2.1.1/picotool`

#### Usage:

1. **Load firmware**:
   ```bash
   picotool load build/genseq.uf2
   ```

2. **Reboot to application**:
   ```bash
   picotool reboot
   ```

3. **Get device info**:
   ```bash
   picotool info
   ```

## Troubleshooting

### Build Issues

#### "SDK not found"
```bash
export PICO_SDK_PATH=~/.pico-sdk/sdk/2.1.1
cmake ..
```

#### "Toolchain not found"
```bash
export PICO_TOOLCHAIN_PATH=~/.pico-sdk/toolchain/14_2_Rel1
cmake ..
```

#### "Ninja not found"
```bash
# Add to PATH
export PATH=$PATH:~/.pico-sdk/ninja/v1.12.1
```

### Flash Issues

#### USB not detected in BOOTSEL mode:
- Try different USB cable
- Check USB port
- Hold BOOTSEL longer before connecting
- Try on different computer

#### PicoProbe not detected:
- Check wiring connections
- Verify PicoProbe firmware
- Check OpenOCD version
- Try different USB port

#### "Permission denied" on Linux:
```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER
# Logout and login again
```

### Runtime Issues

#### Nothing happens after flash:
- Check build completed successfully
- Verify correct binary for board (pico vs pico2)
- Check power supply
- Try USB serial output

#### Garbled output on UART:
- Check baud rate (default 115200)
- Verify UART pins
- Check ground connection

## Development Workflow

### Recommended Workflow for Development:

1. **Initial Setup**:
   - Use USB flashing for first time
   - Verify basic functionality

2. **Development Cycle**:
   - Set up PicoProbe for debugging
   - Use VSCode debug integration
   - Flash and debug in one step

3. **Production/Release**:
   - Build release version
   - Use USB flashing for deployment
   - Test on target hardware

### Automation Scripts

Create a flash script (`flash.sh`):
```bash
#!/bin/bash
cd build
ninja
echo "Ready to flash. Put Pico in BOOTSEL mode and press Enter."
read
cp genseq.uf2 /media/$(whoami)/RPI-RP2/
echo "Flash complete!"
```

Make executable:
```bash
chmod +x flash.sh
```

## Performance Tips

### Faster Builds:

1. **Use Ninja**:
   - Parallel builds by default
   - Better dependency tracking

2. **ccache** (optional):
   ```bash
   export CC="ccache arm-none-eabi-gcc"
   export CXX="ccache arm-none-eabi-g++"
   ```

3. **Incremental builds**:
   - Only rebuild what changed
   - Don't clean unless necessary

### Faster Flashing:

1. **Use PicoProbe**:
   - No BOOTSEL mode needed
   - Faster file transfer

2. **Disable unnecessary features**:
   - Turn off debug output in release
   - Optimize binary size

## Summary

- **USB Flashing**: Simple, no extra hardware needed
- **PicoProbe**: Advanced debugging, faster development
- **Picotool**: Command-line utilities
- **VSCode Integration**: Seamless development experience

Choose the method that best fits your development stage and hardware availability.
