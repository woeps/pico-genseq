# CMake Configuration

## Overview

The GenSeq project uses CMake with the Raspberry Pi Pico SDK to build the firmware. The `CMakeLists.txt` file is configured to handle the dual-core architecture, PIO programs, and all necessary hardware libraries.

## CMakeLists.txt Breakdown

### Basic Configuration

```cmake
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

- **C Standard**: C11 for C files
- **C++ Standard**: C++17 for C++ files
- **Compile Commands**: Generates `compile_commands.json` for clangd

### Pico SDK Integration

```cmake
# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
```

This section:
- Detects the operating system
- Sets SDK and toolchain versions
- Includes the VSCode extension helper if present
- **Important**: Do not modify these lines if using the Pico VSCode extension

### Project Setup

```cmake
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(genseq C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()
```

- Sets target board to `pico` (can be changed to `pico2` etc.)
- Includes the Pico SDK import
- Defines the project with C, C++, and Assembly support
- Initializes the SDK

### Source File Management

```cmake
# Find all source files in the sequencer, commands, and UI directories
file(GLOB_RECURSE SEQUENCER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/sequencer/*.cpp")
file(GLOB_RECURSE COMMANDS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/commands/*.cpp")
file(GLOB_RECURSE UI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.c")

# Print the found source files for debugging
message(STATUS "Sequencer sources: ${SEQUENCER_SOURCES}")
message(STATUS "Commands sources: ${COMMANDS_SOURCES}")
message(STATUS "UI sources: ${UI_SOURCES}")
```

This approach:
- Automatically finds all source files in each directory
- Supports both `.cpp` and `.c` files for UI
- Provides debug output showing found files

### Executable Definition

```cmake
# Add source files for the sequencer, commands, and UI components
add_executable(genseq 
    src/genseq.cpp
    ${SEQUENCER_SOURCES}
    ${COMMANDS_SOURCES}
    ${UI_SOURCES}
)
```

### PIO Program Generation

```cmake
file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/src/generated)
pico_generate_pio_header(genseq ${CMAKE_CURRENT_LIST_DIR}/src/ui/hardware/driver/ws2812.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/src/generated)
```

- Creates the `src/generated` directory
- Generates C header from PIO assembly file
- Outputs to `src/generated` for inclusion

### Binary Metadata

```cmake
# binary info (readabl by picotool)
pico_set_program_name(genseq "GenSeq")
pico_set_program_version(genseq "0.1")
pico_set_program_description(genseq "Generative MIDI Sequence")
pico_set_program_url(genseq "https://github.com/woeps/pico-genseq")
```

Sets program information readable by `picotool`.

### Standard I/O Configuration

```cmake
# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(genseq 1)
pico_enable_stdio_usb(genseq 0)
```

- UART enabled for debug output
- USB stdio disabled (can be enabled for USB serial)

### Library Linking

```cmake
# Add the standard library to the build
target_link_libraries(genseq
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_uart
    hardware_gpio
    hardware_i2c
    hardware_dma
)
```

Links necessary Pico SDK libraries:
- `pico_stdlib` - Standard library includes
- `pico_multicore` - Dual-core support
- `hardware_pio` - PIO program support
- `hardware_uart` - UART communication (MIDI)
- `hardware_gpio` - GPIO control
- `hardware_i2c` - I2C for display
- `hardware_dma` - DMA for efficient data transfer

### Include Directories

```cmake
# Add the standard include files to the build
target_include_directories(genseq PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/src/generated
)
```

Adds source directories to the include path.

### Build Outputs

```cmake
pico_add_extra_outputs(genseq)
```

Generates additional build artifacts (UF2 file, etc.).

## Common Operations

### Adding a New Driver

1. Create source files in appropriate directory:
   ```
   src/ui/hardware/driver/new_driver.h
   src/ui/hardware/driver/new_driver.cpp
   ```

2. The `GLOB_RECURSE` will automatically pick up the new files

3. If the driver requires additional libraries:
   ```cmake
   target_link_libraries(genseq
       # ... existing libraries ...
       hardware_spi  # Add new library
   )
   ```

### Adding a New Library

1. Create library directory:
   ```
   src/libraries/new_lib/
   ├── new_lib.h
   └── new_lib.cpp
   ```

2. Add to CMakeLists.txt:
   ```cmake
   file(GLOB_RECURSE NEW_LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/libraries/new_lib/*.cpp")
   
   add_executable(genseq 
       # ... existing sources ...
       ${NEW_LIB_SOURCES}
   )
   ```

### Adding a New PIO Program

1. Create `.pio` file:
   ```
   src/ui/hardware/driver/new_program.pio
   ```

2. Add header generation:
   ```cmake
   pico_generate_pio_header(genseq 
       ${CMAKE_CURRENT_LIST_DIR}/src/ui/hardware/driver/new_program.pio 
       OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/src/generated
   )
   ```

3. Include in code:
   ```cpp
   #include "new_program.pio.h"
   ```

### Conditional Compilation

To add build options:

```cmake
option(ENABLE_FEATURE "Enable new feature" OFF)

if(ENABLE_FEATURE)
    target_compile_definitions(genseq PRIVATE ENABLE_FEATURE=1)
    # Add additional sources or libraries
endif()
```

### Platform-Specific Code

```cmake
if(PICO_BOARD STREQUAL "pico2")
    target_compile_definitions(genseq PRIVATE PICO_BOARD=2)
    # Add Pico 2 specific code
endif()
```

## Build Types

### Debug Build

```bash
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
ninja
```

Includes debug symbols and disables optimizations.

### Release Build

```bash
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
ninja
```

Optimized for size and speed.

## Build System Internals

### Compilation Database

The `CMAKE_EXPORT_COMPILE_COMMANDS ON` setting generates `compile_commands.json` which:
- Provides compile commands to clangd
- Enables proper code completion and error checking
- Is essential for the Windsurf IDE integration

### Ninja Build System

The project uses Ninja (specified in `.vscode/settings.json`):
- Faster builds than make
- Parallel compilation
- Better dependency tracking

## Troubleshooting

### Common CMake Issues

1. **SDK not found**:
   - Ensure `PICO_SDK_PATH` environment variable is set
   - Check SDK installation in `~/.pico-sdk/sdk/`

2. **Toolchain not found**:
   - Verify `PICO_TOOLCHAIN_PATH` is set
   - Check toolchain installation in `~/.pico-sdk/toolchain/`

3. **Missing sources**:
   - Check `message(STATUS)` output in build log
   - Verify file paths and extensions

### Clean Build

```bash
cd build
rm -rf *
cmake ..
ninja
```

### Verbose Build

```bash
ninja -v
```

Shows all compiler commands.

## Best Practices

1. **Use GLOB_RECURSE sparingly**: While convenient, it can hide missing files
2. **Keep CMake organized**: Group related configurations
3. **Document custom options**: Comment any project-specific CMake code
4. **Test clean builds**: Regularly build from scratch
5. **Version control CMakeLists.txt**: Track all build configuration changes

## Summary

The CMake configuration is designed to:
- Automatically handle source file discovery
- Support the dual-core architecture
- Integrate with the Pico SDK ecosystem
- Provide a smooth development experience with proper tooling support
