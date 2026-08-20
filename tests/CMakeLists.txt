cmake_minimum_required(VERSION 3.13)
project(genseq_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC ${CMAKE_CURRENT_LIST_DIR}/../src)

add_executable(genseq_tests
    main.cpp
    test_types.cpp
    ${SRC}/ui/KeyNames.cpp
)

target_include_directories(genseq_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/stubs
    ${SRC}
)

target_compile_options(genseq_tests PRIVATE -Wall -Wextra)
