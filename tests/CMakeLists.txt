cmake_minimum_required(VERSION 3.13)
project(genseq_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC ${CMAKE_CURRENT_LIST_DIR}/../src)

add_executable(genseq_tests
    main.cpp
    test_types.cpp
    test_initview.cpp
    test_reducer.cpp
    test_decoder.cpp
    test_patternsview.cpp
    test_setdetailviews.cpp
    stubs/command_stub.cpp
    stubs/hardware_stub.cpp
    ${SRC}/common/gate_set.cpp
    ${SRC}/ui/KeyNames.cpp
    ${SRC}/ui/state/Reducer.cpp
    ${SRC}/ui/views/InitView.cpp
    ${SRC}/ui/views/PatternsView.cpp
    ${SRC}/ui/views/GateSetView.cpp
    ${SRC}/ui/views/PitchSetView.cpp
    ${SRC}/ui/views/VelocitySetView.cpp
    ${SRC}/ui/hardware/KeyboardDecoder.cpp
)

target_include_directories(genseq_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/stubs
    ${SRC}
)

target_compile_options(genseq_tests PRIVATE -Wall -Wextra)
