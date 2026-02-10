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
file(GLOB_RECURSE SEQUENCER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/sequencer/*.cpp")
file(GLOB_RECURSE COMMANDS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/commands/*.cpp")
file(GLOB_RECURSE UI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.c")

# Print the found source files for debugging
message(STATUS "Sequencer sources: ${SEQUENCER_SOURCES}")
message(STATUS "Commands sources: ${COMMANDS_SOURCES}")
message(STATUS "UI sources: ${UI_SOURCES}")

# Add source files for the sequencer, commands, and UI components
add_executable(genseq 
    genseq.cpp
    ${SEQUENCER_SOURCES}
    ${COMMANDS_SOURCES}
    ${UI_SOURCES}
)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/generated)
pico_generate_pio_header(genseq ${CMAKE_CURRENT_LIST_DIR}/ui/hardware/driver/ws2812.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/generated)

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
)

# Add the standard include files to the build
target_include_directories(genseq PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/generated
)


pico_add_extra_outputs(genseq)