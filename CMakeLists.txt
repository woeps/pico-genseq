# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

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
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(genseq C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

# Find all source files in the sequencer, commands, and UI directories
file(GLOB_RECURSE SEQUENCER_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/sequencer/*.cpp")
file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp")
file(GLOB_RECURSE COMMANDS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/commands/*.cpp")
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.c")
file(GLOB_RECURSE PERSISTENCE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/persistence/*.cpp")

# Print the found source files for debugging
message(STATUS "Sequencer sources: ${SEQUENCER_SOURCES}")
message(STATUS "Common sources: ${COMMON_SOURCES}")
message(STATUS "Commands sources: ${COMMANDS_SOURCES}")
message(STATUS "UI sources: ${UI_SOURCES}")
message(STATUS "Persistence sources: ${PERSISTENCE_SOURCES}")

# Add source files for the sequencer, commands, and UI components
add_executable(genseq 
    src/genseq.cpp
    ${SEQUENCER_SOURCES}
    ${COMMON_SOURCES}
    ${COMMANDS_SOURCES}
    ${UI_SOURCES}
    ${PERSISTENCE_SOURCES}
)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/src/generated)
pico_generate_pio_header(genseq ${CMAKE_CURRENT_LIST_DIR}/src/ui/hardware/driver/ws2812.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/src/generated)

# binary info (readabl by picotool)
pico_set_program_name(genseq "GenSeq")
pico_set_program_version(genseq "0.1")
pico_set_program_description(genseq "Generative MIDI Sequence")
pico_set_program_url(genseq "https://github.com/woeps/pico-genseq")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(genseq 1)
pico_enable_stdio_usb(genseq 0)

# Add the standard library to the build
target_link_libraries(genseq
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_uart
    hardware_gpio
    hardware_i2c
    hardware_dma
    hardware_flash
    hardware_sync
    tinyusb_host
    tinyusb_board
)

# Add the standard include files to the build
target_include_directories(genseq PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/src/config
        ${CMAKE_CURRENT_LIST_DIR}/src/generated
        ${CMAKE_CURRENT_LIST_DIR}/src/persistence
)

# Optional logic-analyzer instrumentation. Tracing and every individual pin are
# disabled by default; configure only GPIOs that are free on the actual hardware.
option(GENSEQ_PERFORMANCE_TRACE_ENABLED
       "Enable performance trace GPIOs" OFF)
set(GENSEQ_TRACE_TICK_PIN -1 CACHE STRING
    "GPIO driven high while a logical tick is processed")
set(GENSEQ_TRACE_MIDI_PIN -1 CACHE STRING
    "GPIO pulsed around each MIDI byte dispatch")
set(GENSEQ_TRACE_OPERATION_PIN -1 CACHE STRING
    "GPIO driven high while a sequencer operation is applied")
set(GENSEQ_TRACE_SAVE_PIN -1 CACHE STRING
    "GPIO driven high while a save is in progress")
set(GENSEQ_TRACE_FAULT_PIN -1 CACHE STRING
    "GPIO held high while a performance fault is latched")

target_compile_definitions(genseq PRIVATE
    GENSEQ_PERFORMANCE_TRACE_ENABLED=$<BOOL:${GENSEQ_PERFORMANCE_TRACE_ENABLED}>
    GENSEQ_TRACE_TICK_PIN=${GENSEQ_TRACE_TICK_PIN}
    GENSEQ_TRACE_MIDI_PIN=${GENSEQ_TRACE_MIDI_PIN}
    GENSEQ_TRACE_OPERATION_PIN=${GENSEQ_TRACE_OPERATION_PIN}
    GENSEQ_TRACE_SAVE_PIN=${GENSEQ_TRACE_SAVE_PIN}
    GENSEQ_TRACE_FAULT_PIN=${GENSEQ_TRACE_FAULT_PIN}
)

pico_add_extra_outputs(genseq)

# ---------------------------------------------------------------------------
# Optional standalone FRAM bring-up / wiring-check firmware (task 9.3).
# Separate executable; built only when GENSEQ_BUILD_FRAM_BRINGUP=ON so it never
# affects the default genseq build. Emits on UART0 (GP0/GP1, reaches a
# picoprobe UART bridge) and USB-CDC (/dev/ttyACM when the target USB is cabled).
#   cmake -B build-fram -DGENSEQ_BUILD_FRAM_BRINGUP=ON
#   cmake --build build-fram --target fram_bringup
# ---------------------------------------------------------------------------
option(GENSEQ_BUILD_FRAM_BRINGUP
       "Build the standalone MB85RS2MTA FRAM bring-up firmware" OFF)

if(GENSEQ_BUILD_FRAM_BRINGUP)
    add_executable(fram_bringup src/tools/fram_bringup.cpp)
    pico_set_program_name(fram_bringup "GenSeq FRAM bring-up")
    pico_set_program_description(fram_bringup "MB85RS2MTA RDID + 3-byte addressing check")
    # Emit on BOTH transports: UART0 (GP0/GP1) reaches the picoprobe's UART
    # bridge (/dev/ttyACM provided by the Debugprobe), and USB-CDC works when
    # the target's own USB is cabled. Picoprobe-only rigs rely on the UART path.
    pico_enable_stdio_uart(fram_bringup 1)
    pico_enable_stdio_usb(fram_bringup 1)
    target_link_libraries(fram_bringup
        pico_stdlib
        hardware_spi
        hardware_gpio
    )
    target_include_directories(fram_bringup PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/src/persistence
    )
    pico_add_extra_outputs(fram_bringup)
endif()

# ---------------------------------------------------------------------------
# FRAM save-path transparency trace harness (task 9.3, Route A).
# Drives a real FRAM eraseAndProgram while core1 emits 255 BPM Tick+MIDI
# on the 9.2 trace pins, so the BitScope shows whether ticks/MIDI keep
# flowing during a FRAM write (the transparency question).
#   cmake -B build-fram -DGENSEQ_BUILD_FRAM_SAVE_TRACE=ON
#   cmake --build build-fram --target fram_save_trace
# ---------------------------------------------------------------------------
option(GENSEQ_BUILD_FRAM_SAVE_TRACE
       "Build the FRAM save-path transparency trace harness" OFF)

if(GENSEQ_BUILD_FRAM_SAVE_TRACE)
    add_executable(fram_save_trace
        src/tools/fram_save_trace.cpp
        src/persistence/SaveRecord.cpp    # real codec (serialize)
    )
    pico_set_program_name(fram_save_trace "GenSeq FRAM save trace")
    pico_set_program_description(fram_save_trace
        "9.3 transparency: FRAM write under worst-case sequencing load")
    pico_enable_stdio_uart(fram_save_trace 1)
    pico_enable_stdio_usb(fram_save_trace 1)
    target_link_libraries(fram_save_trace
        pico_stdlib
        pico_multicore
        hardware_spi
        hardware_gpio
    )
    target_include_directories(fram_save_trace PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/src/persistence
    )
    pico_add_extra_outputs(fram_save_trace)
endif()
