# Standalone HOST test project for the MIDI output interface feature.
#
# This project is intentionally SEPARATE from the firmware build. It compiles the
# SDK-free "emission logic" translation units of the sequencer against minimal host
# stubs (test/pico_stubs) that shadow the real Pico SDK headers, and links NONE of
# the Pico SDK hardware libraries (hardware_uart, hardware_gpio, pico_multicore,
# pico_stdlib). It uses RapidCheck for property-based testing (>=100 iterations by
# default).
#
# Build:
#   cmake -B test/build -S test
#   cmake --build test/build
#   ctest --test-dir test/build
#
cmake_minimum_required(VERSION 3.13)
project(genseq_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(SRC_DIR   "${REPO_ROOT}/src")
set(TEST_DIR  "${CMAKE_CURRENT_SOURCE_DIR}")

# ---------------------------------------------------------------------------
# RapidCheck (property-based testing)
#
# Prefer FetchContent from the upstream GitHub repo. If the network is
# unavailable, a locally-provided RapidCheck can be supplied two ways:
#   1. -DRAPIDCHECK_LOCAL_DIR=/path/to/rapidcheck  (added via add_subdirectory)
#   2. an already-installed package discoverable by find_package(rapidcheck)
# The RAPIDCHECK_AVAILABLE flag gates whether the property-test executable and
# its CTest registration are created.
# ---------------------------------------------------------------------------
set(RAPIDCHECK_AVAILABLE FALSE)

if(DEFINED RAPIDCHECK_LOCAL_DIR AND EXISTS "${RAPIDCHECK_LOCAL_DIR}/CMakeLists.txt")
    message(STATUS "Using local RapidCheck from ${RAPIDCHECK_LOCAL_DIR}")
    add_subdirectory("${RAPIDCHECK_LOCAL_DIR}" "${CMAKE_BINARY_DIR}/rapidcheck-local")
    set(RAPIDCHECK_AVAILABLE TRUE)
else()
    include(FetchContent)
    FetchContent_Declare(
        rapidcheck
        GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
        GIT_TAG        master
    )
    # FetchContent_MakeAvailable errors hard on network failure. Wrap it so a
    # missing network degrades gracefully (RAPIDCHECK_AVAILABLE stays FALSE)
    # instead of aborting configuration — we still build the emission library to
    # validate the host stubs. On success, RapidCheck's own targets are defined.
    #
    # NOTE: This calls the modern FetchContent_MakeAvailable form (no deprecated
    # FetchContent_Populate(name)). Any fetch error is caught below via a marker.
    set(_rc_ok TRUE)
    if(COMMAND FetchContent_MakeAvailable)
        FetchContent_MakeAvailable(rapidcheck)
    else()
        set(_rc_ok FALSE)
    endif()
    if(_rc_ok AND TARGET rapidcheck)
        set(RAPIDCHECK_AVAILABLE TRUE)
    endif()
endif()

if(NOT RAPIDCHECK_AVAILABLE)
    message(WARNING
        "RapidCheck could not be fetched or located. Property-test targets will "
        "be skipped. Provide -DRAPIDCHECK_LOCAL_DIR=<path> for an offline build. "
        "The host-compilation smoke library (genseq_emission) is still built to "
        "validate the sequencer emission TU compiles against the host stubs.")
endif()

# ---------------------------------------------------------------------------
# Emission logic: the SDK-free translation units.
#
# Compiled host-side:
#   - src/sequencer/sequencer.cpp        (emission logic)
#   - src/common/{pattern,pitch_set,velocity_set,gate_set}.cpp
#
# NOT compiled host-side (Pico SDK dependent):
#   - src/sequencer/UartMidiOutput.cpp   (uart/gpio)
#   - src/sequencer/sequencer_task.cpp   (multicore, receiveCommand)
#   - src/commands/command.cpp           (multicore FIFO)
# ---------------------------------------------------------------------------
set(EMISSION_SOURCES
    "${SRC_DIR}/sequencer/sequencer.cpp"
    "${SRC_DIR}/common/pattern.cpp"
    "${SRC_DIR}/common/pitch_set.cpp"
    "${SRC_DIR}/common/velocity_set.cpp"
    "${SRC_DIR}/common/gate_set.cpp"
)

# Include dirs, ORDER MATTERS: pico_stubs FIRST so it shadows the real SDK.
set(EMISSION_INCLUDE_DIRS
    "${TEST_DIR}/pico_stubs"
    "${SRC_DIR}"
    "${SRC_DIR}/sequencer"
    "${SRC_DIR}/common"
    "${SRC_DIR}/commands"
    "${TEST_DIR}"
)

# A static library carrying the emission logic. Building this at all is the
# host-compilation smoke check (Req 5.5): it proves the sequencer emission TU
# compiles with zero Pico SDK libraries.
add_library(genseq_emission STATIC ${EMISSION_SOURCES})
target_include_directories(genseq_emission PUBLIC ${EMISSION_INCLUDE_DIRS})
# Deliberately link NONE of hardware_uart, hardware_gpio, pico_multicore,
# pico_stdlib. Pure host build.

# ---------------------------------------------------------------------------
# Persistence codec + manager: the SDK-free persistence translation units
# (task 8.2).
#
# Compiled host-side:
#   - src/persistence/SaveRecord.cpp         (pure codec: serialize/deserialize/crc32)
#   - src/persistence/PersistenceManager.cpp (orchestration over IFlashStore)
#
# NOT compiled host-side (Pico SDK dependent, firmware-only):
#   - src/persistence/FlashStore.cpp         (hardware/flash.h, pico/multicore.h)
#
# These two TUs depend only on <cstdint>/<vector>/<array> and the header-only
# common::PlayingOrder enum (via "../common/pitch_set.h"); they need NO symbols
# from pitch_set.cpp, so common sources are intentionally NOT re-added here — that
# would duplicate the symbols already carried by genseq_emission. This is a
# separate library so the existing genseq_emission / example / smoke targets are
# left completely undisturbed.
#
# The persistence include dirs are exported PUBLIC so any target linking this
# library (e.g. genseq_property_tests) can resolve "persistence/IFlashStore.h",
# "persistence/PersistableConfig.h" and test/persistence/FakeFlashStore.h.
# ---------------------------------------------------------------------------
set(PERSISTENCE_SOURCES
    "${SRC_DIR}/persistence/SaveRecord.cpp"
    "${SRC_DIR}/persistence/PersistenceManager.cpp"
)

# Include dirs for the persistence codec, keeping pico_stubs FIRST so it shadows
# the real SDK for any transitively-included header.
set(PERSISTENCE_INCLUDE_DIRS
    "${TEST_DIR}/pico_stubs"
    "${SRC_DIR}"
    "${SRC_DIR}/persistence"
    "${SRC_DIR}/common"
    "${TEST_DIR}"
    "${TEST_DIR}/persistence"
)

message(STATUS
    "Persistence host build: compiling SaveRecord.cpp + PersistenceManager.cpp "
    "(SDK-free). src/persistence/FlashStore.cpp is intentionally EXCLUDED from "
    "the host build — it is the only SDK-dependent persistence TU (hardware/flash.h, "
    "pico/multicore.h) and is firmware-only.")

add_library(genseq_persistence STATIC ${PERSISTENCE_SOURCES})
target_include_directories(genseq_persistence PUBLIC ${PERSISTENCE_INCLUDE_DIRS})
# Pure host build: links NONE of the Pico SDK libraries.

# ---------------------------------------------------------------------------
# Example test: destruction through the base interface (task 2.3).
#
# A standalone example (not property-based) validating Requirement 1.2: deleting
# a derived IMidiOutput through an IMidiOutput* base pointer runs the derived
# destructor. It includes only the SDK-free IMidiOutput.h and links no Pico SDK
# libraries and no RapidCheck, so it builds host-side unconditionally.
#
# It has its own main(), so it MUST NOT be swept into the property-test glob
# below (two main()s would fail to link). It is excluded there explicitly.
# ---------------------------------------------------------------------------
set(EXAMPLE_DESTRUCTION_SRC "${TEST_DIR}/example_destruction_through_base.cpp")

add_executable(genseq_example_destruction "${EXAMPLE_DESTRUCTION_SRC}")
target_include_directories(genseq_example_destruction PRIVATE ${EMISSION_INCLUDE_DIRS})
add_test(NAME genseq_example_destruction COMMAND genseq_example_destruction)

# ---------------------------------------------------------------------------
# Example test: Save_Record header layout (task 2.7).
#
# A standalone example (not property-based) validating Requirement 3.1: a
# serialized Save_Record begins with the fixed 12-byte header (magic {'G','S'},
# version 1, reserved 0, little-endian payloadLength, little-endian crc32) with
# the payload starting at offset HEADER_SIZE.
#
# It includes only the SDK-free persistence codec headers and links the
# genseq_persistence library (for serialize/crc32); it links NO Pico SDK libs
# and NO RapidCheck, so it builds host-side unconditionally.
#
# It has its own main(), so it MUST NOT be swept into the property-test glob
# below (two main()s would fail to link). It is excluded there explicitly.
# ---------------------------------------------------------------------------
set(EXAMPLE_HEADER_LAYOUT_SRC "${TEST_DIR}/example_header_layout.cpp")

add_executable(genseq_example_header_layout "${EXAMPLE_HEADER_LAYOUT_SRC}")
target_include_directories(genseq_example_header_layout PRIVATE
    ${EMISSION_INCLUDE_DIRS}
    "${SRC_DIR}/persistence"
    "${TEST_DIR}/persistence"
)
# Link the persistence codec (serialize/crc32) but NONE of the Pico SDK libs and
# NO RapidCheck.
target_link_libraries(genseq_example_header_layout PRIVATE genseq_persistence)
add_test(NAME genseq_example_header_layout COMMAND genseq_example_header_layout)

# ---------------------------------------------------------------------------
# Example test: FlashResult -> SaveOutcome mapping (task 3.8).
#
# A standalone example (not property-based) validating Requirements 4.7 and 4.8:
# PersistenceManager::save maps the underlying IFlashStore result onto a
# SaveOutcome — an OK write -> SUCCESS; each injected flash failure
# (ERASE_FAILED / PROGRAM_FAILED / VERIFY_MISMATCH) -> FAIL_FLASH; and an
# oversized record vs a tiny capacity -> FAIL_CAPACITY (capacity guard runs
# before any flash access).
#
# It includes only the SDK-free persistence codec/manager headers plus the
# in-memory test/persistence/FakeFlashStore.h and links the genseq_persistence
# library (for serialize/PersistenceManager); it links NO Pico SDK libs and NO
# RapidCheck, so it builds host-side unconditionally.
#
# It has its own main(), so it MUST NOT be swept into the property-test glob
# below (two main()s would fail to link). It is excluded there explicitly.
# ---------------------------------------------------------------------------
set(EXAMPLE_OUTCOME_MAPPING_SRC "${TEST_DIR}/example_outcome_mapping.cpp")

add_executable(genseq_example_outcome_mapping "${EXAMPLE_OUTCOME_MAPPING_SRC}")
target_include_directories(genseq_example_outcome_mapping PRIVATE
    ${EMISSION_INCLUDE_DIRS}
    "${SRC_DIR}/persistence"
    "${TEST_DIR}/persistence"
)
# Link the persistence codec/manager (serialize/PersistenceManager) but NONE of
# the Pico SDK libs and NO RapidCheck.
target_link_libraries(genseq_example_outcome_mapping PRIVATE genseq_persistence)
add_test(NAME genseq_example_outcome_mapping COMMAND genseq_example_outcome_mapping)

# ---------------------------------------------------------------------------
# Example test: boot-banner policy — only CRC_MISMATCH warns.
#
# A standalone example (not property-based) validating the narrowed boot-time
# banner policy: persistence::shouldWarnOnBootLoad returns true ONLY for
# LoadStatus::CRC_MISMATCH (a well-formed GenSeq record whose payload failed CRC)
# and false for every other status (OK / ABSENT / BAD_MAGIC / BAD_VERSION /
# TRUNCATED), which fall back to defaults silently.
#
# It includes only the SDK-free persistence headers (BootBannerPolicy.h is
# header-only; the LoadStatus enum it uses is header-only in SaveRecord.h). It
# links genseq_persistence for consistency with the sibling persistence examples
# (harmless — no persistence symbols are actually referenced), and links NO Pico
# SDK libs and NO RapidCheck, so it builds host-side unconditionally.
#
# It has its own main(), so it MUST NOT be swept into the property-test glob
# below (two main()s would fail to link). It is excluded there explicitly.
# ---------------------------------------------------------------------------
set(EXAMPLE_BOOT_BANNER_POLICY_SRC "${TEST_DIR}/example_boot_banner_policy.cpp")

add_executable(genseq_example_boot_banner_policy "${EXAMPLE_BOOT_BANNER_POLICY_SRC}")
target_include_directories(genseq_example_boot_banner_policy PRIVATE
    ${EMISSION_INCLUDE_DIRS}
    "${SRC_DIR}/persistence"
    "${TEST_DIR}/persistence"
)
# Link the persistence codec for consistency with sibling examples but NONE of
# the Pico SDK libs and NO RapidCheck. BootBannerPolicy.h / LoadStatus are header-only.
target_link_libraries(genseq_example_boot_banner_policy PRIVATE genseq_persistence)
add_test(NAME genseq_example_boot_banner_policy COMMAND genseq_example_boot_banner_policy)

# ---------------------------------------------------------------------------
# Example test: banner distinctness + FlashResult -> SaveOutcome mapping (task 7.4).
#
# A standalone example (not property-based) validating Requirements 4.7, 4.8, 6.7, 7.2:
#   - Part 1 (7.2, 6.7): ui::state::SaveBanner SUCCESS / FAILURE / NOT_LOADED are mutually
#     distinct enum values (and distinct from NONE) — the host-testable proxy for "distinct
#     success / failure / not-loaded indications". HARDWARE DEVIATION: task 7.3 presents the
#     banners as distinct LED-matrix COLORS (no LCD is wired), so the concrete color
#     distinctness is asserted on-target; this host check guarantees the three enum states the
#     UI switches on are distinct so that color mapping is well-defined.
#   - Part 2 (4.7, 4.8): PersistenceManager::save maps OK -> SUCCESS, each flash failure ->
#     FAIL_FLASH, oversized -> FAIL_CAPACITY (compact overlap with example_outcome_mapping).
#   - Part 3 (7.1, 7.2 intent): SUCCESS outcome -> SaveBanner::SUCCESS, any failure ->
#     SaveBanner::FAILURE, mirroring UIController::drainSaveRequest's pure classification.
#
# It includes the SDK-free persistence codec/manager headers, the in-memory FakeFlashStore, and
# the SDK-free ui/state/UIState.h (for SaveBanner). It links genseq_persistence (serialize /
# PersistenceManager); SaveBanner is header-only. NO Pico SDK libs, NO RapidCheck, so it builds
# host-side unconditionally. Its own main() means it MUST be excluded from the property glob.
# ---------------------------------------------------------------------------
set(EXAMPLE_BANNER_MAPPING_SRC "${TEST_DIR}/example_banner_mapping.cpp")

add_executable(genseq_example_banner_mapping "${EXAMPLE_BANNER_MAPPING_SRC}")
target_include_directories(genseq_example_banner_mapping PRIVATE
    ${EMISSION_INCLUDE_DIRS}
    "${SRC_DIR}/persistence"
    "${TEST_DIR}/persistence"
    "${SRC_DIR}/ui"
    "${SRC_DIR}/ui/state"
)
# Link the persistence codec/manager (serialize / PersistenceManager) but NONE of the Pico SDK
# libs and NO RapidCheck. SaveBanner (ui/state/UIState.h) is header-only.
target_link_libraries(genseq_example_banner_mapping PRIVATE genseq_persistence)
add_test(NAME genseq_example_banner_mapping COMMAND genseq_example_banner_mapping)

# ---------------------------------------------------------------------------
# Example test: exactly-once core1 launch guard (task 4.3).
#
# A standalone example (not property-based) validating Requirement 4.4: repeated
# invocation of createSequencerTask launches core1 exactly once and does not
# construct a second Sequencer/UartMidiOutput.
#
# It compiles the REAL src/sequencer/sequencer_task.cpp host-side and links the
# emission library (genseq_emission) for the Sequencer/common symbols. It does
# NOT compile the real UartMidiOutput.cpp and links NO Pico SDK libraries: the
# test itself provides counting seams for multicore_launch_core1, the
# UartMidiOutput ctor/write, and commands::receiveCommand so it can observe the
# launch/construction counts.
#
# Like genseq_example_destruction, it carries its own main() plus symbol
# definitions, so it MUST be excluded from the property-test glob below.
# ---------------------------------------------------------------------------
set(EXAMPLE_LAUNCH_GUARD_SRC "${TEST_DIR}/example_launch_guard.cpp")

add_executable(genseq_example_launch_guard
    "${EXAMPLE_LAUNCH_GUARD_SRC}"
    "${SRC_DIR}/sequencer/sequencer_task.cpp"
)
target_include_directories(genseq_example_launch_guard PRIVATE ${EMISSION_INCLUDE_DIRS})
# Link the emission logic (Sequencer + common) but NONE of the Pico SDK libs and
# NOT the real UartMidiOutput.cpp (its host double lives in the test source).
target_link_libraries(genseq_example_launch_guard PRIVATE genseq_emission)
add_test(NAME genseq_example_launch_guard COMMAND genseq_example_launch_guard)

# ---------------------------------------------------------------------------
# Smoke test: host compilation with zero Pico SDK libraries (task 6.4).
#
# An EXPLICIT, self-documenting host-compilation smoke check validating Requirements
# 1.3, 3.2, 5.1, 5.5. Per the design's Testing Strategy ("Smoke (Req 1.3, 3.2, 5.1, 5.5)"),
# the build succeeding with zero errors IS the check.
#
# The smoke TU (test/smoke_host_compilation.cpp) #includes IMidiOutput.h standalone (proving
# the interface header is includable SDK-free) and sequencer.h (proving the Sequencer class
# header is usable host-side). It links genseq_emission — the REAL sequencer.cpp + common/*.cpp
# objects — and NONE of the Pico SDK hardware libraries (hardware_uart, hardware_gpio,
# pico_multicore, pico_stdlib) and NO RapidCheck. Linking the real emission objects turns this
# into a genuine end-to-end compile-AND-link check: if any emission TU referenced an unstubbed
# Pico SDK symbol, the link would fail.
#
# Like the example tests, it carries its own main(), so it MUST be excluded from the
# property-test glob below (two main()s would fail to link).
# ---------------------------------------------------------------------------
set(SMOKE_HOST_COMPILATION_SRC "${TEST_DIR}/smoke_host_compilation.cpp")

add_executable(genseq_smoke_host_compilation "${SMOKE_HOST_COMPILATION_SRC}")
target_include_directories(genseq_smoke_host_compilation PRIVATE ${EMISSION_INCLUDE_DIRS})
# Link the emission logic (Sequencer + common) but NONE of the Pico SDK libs and NO RapidCheck.
target_link_libraries(genseq_smoke_host_compilation PRIVATE genseq_emission)
add_test(NAME genseq_smoke_host_compilation COMMAND genseq_smoke_host_compilation)

# ---------------------------------------------------------------------------
# Reducer property test: non-CTRL S routes to the active view (task 6.4).
#
# Property 7 (persist-settings): for any modifier bitmask WITHOUT the CTRL bit,
# a KEY_PRESSED S is delegated to the active view and never requests a save.
#
# This test exercises ui::state::reduce (src/ui/state/Reducer.cpp), which
# references commands::send* (the multicore FIFO seam) and
# common::GateSet::createEuclidean. It is therefore built as its OWN dedicated
# executable — NOT swept into genseq_property_tests — so command.cpp / the Pico
# SDK are never pulled into that shared binary. Sources: the test TU (which
# provides host-safe no-op commands:: seam definitions and its own main()) plus
# the real Reducer.cpp. It links genseq_emission to reuse common/* (pitch_set,
# velocity_set, gate_set) and avoid duplicate symbols. Include dirs keep
# pico_stubs FIRST (Reducer.cpp -> command.h -> pico/multicore.h stub) and add
# src/ui + src/ui/state for the ui headers. Requires RapidCheck.
#
# Its own main() means it MUST be excluded from the property-test glob below.
# ---------------------------------------------------------------------------
set(REDUCER_PROPERTY7_SRC "${TEST_DIR}/property7_s_routing.cpp")

# ---------------------------------------------------------------------------
# Example test: Ctrl+S recognition and debounce intent (task 6.5).
#
# A standalone example (not property-based) validating Requirements 1.1, 1.2, 1.4:
# reduce() recognizes the Ctrl+S Save_Combo and consumes it GLOBALLY (sets
# saveRequested, does NOT route to the active view); plain S (no CTRL) still routes
# to the view and requests no save; and a second Ctrl+S while a save is already
# requested re-sets the flag idempotently and is still consumed globally. The
# actual in-progress suppression lives in UIController::drainSaveRequest via
# saveInProgress (integration/on-target scope), not the pure reducer.
#
# Like genseq_reducer_property_tests it exercises ui::state::reduce (Reducer.cpp),
# so it is built as its OWN dedicated executable: the test TU supplies host-safe
# no-op commands:: seam definitions and its own main(), and the real Reducer.cpp is
# compiled alongside it. It links genseq_emission to reuse common/* (avoiding
# duplicate symbols) but needs NO RapidCheck (it is an example), so it is built
# unconditionally like the other example targets. Include dirs keep pico_stubs
# FIRST (Reducer.cpp -> command.h -> pico/multicore.h stub) and add src/ui +
# src/ui/state for the ui headers.
#
# Its own main() means it MUST be excluded from the property-test glob below.
# ---------------------------------------------------------------------------
set(EXAMPLE_SAVE_COMBO_SRC "${TEST_DIR}/example_save_combo.cpp")

add_executable(genseq_example_save_combo
    "${EXAMPLE_SAVE_COMBO_SRC}"
    "${SRC_DIR}/ui/state/Reducer.cpp"
)
target_include_directories(genseq_example_save_combo PRIVATE
    ${EMISSION_INCLUDE_DIRS}
    "${SRC_DIR}/ui"
    "${SRC_DIR}/ui/state"
)
# Link the emission logic (common/*) but NONE of the Pico SDK libs and NO RapidCheck.
target_link_libraries(genseq_example_save_combo PRIVATE genseq_emission)
add_test(NAME genseq_example_save_combo COMMAND genseq_example_save_combo)

# ---------------------------------------------------------------------------
# Property-test executable.
#
# The property tests (task 6.3+) live in test/*.cpp. They are auto-discovered
# via glob so 6.3 can simply drop in its .cpp. Only built when RapidCheck is
# available.
# ---------------------------------------------------------------------------
if(RAPIDCHECK_AVAILABLE)
    file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${TEST_DIR}/*.cpp")
    # Example tests carry their own main() and are built as separate targets;
    # exclude them so the property-test executable has a single main().
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_DESTRUCTION_SRC}")
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_LAUNCH_GUARD_SRC}")
    list(REMOVE_ITEM TEST_SOURCES "${SMOKE_HOST_COMPILATION_SRC}")
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_HEADER_LAYOUT_SRC}")
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_OUTCOME_MAPPING_SRC}")
    # The boot-banner policy example is its own dedicated target with its own
    # main(); exclude it so the shared runner keeps a single main().
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_BOOT_BANNER_POLICY_SRC}")
    # The banner-mapping example (task 7.4) is its own dedicated target with its
    # own main(); exclude it so the shared runner keeps a single main().
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_BANNER_MAPPING_SRC}")
    # The reducer property test is its own dedicated target with its own main()
    # (it compiles Reducer.cpp + commands:: seams); exclude it from the shared
    # runner so the two main()s do not collide.
    list(REMOVE_ITEM TEST_SOURCES "${REDUCER_PROPERTY7_SRC}")
    # The Ctrl+S example (task 6.5) is its own dedicated example target with its
    # own main() (it compiles Reducer.cpp + commands:: seams); exclude it from the
    # shared runner so the two main()s do not collide.
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_SAVE_COMBO_SRC}")

    if(NOT TEST_SOURCES)
        message(STATUS
            "No test/*.cpp sources found yet (task 6.3 adds them). Property-test "
            "executable will be created once test sources exist.")
    else()
        add_executable(genseq_property_tests ${TEST_SOURCES})
        target_include_directories(genseq_property_tests PRIVATE
            ${EMISSION_INCLUDE_DIRS}
            "${SRC_DIR}/persistence"
            "${TEST_DIR}/persistence"
        )
        # Link the persistence codec/manager alongside the emission logic so any
        # persistence property/example tests dropped into test/*.cpp (and their
        # test/persistence/FakeFlashStore.h) compile and link (task 8.2).
        target_link_libraries(genseq_property_tests PRIVATE genseq_emission genseq_persistence rapidcheck)

        add_test(NAME genseq_property_tests COMMAND genseq_property_tests)
    endif()

    # Dedicated reducer property-test target (task 6.4). Compiles the real
    # Reducer.cpp host-side alongside the test TU (which supplies host-safe
    # commands:: seams and its own main()); links genseq_emission for common/*
    # and rapidcheck for the property runner.
    add_executable(genseq_reducer_property_tests
        "${REDUCER_PROPERTY7_SRC}"
        "${SRC_DIR}/ui/state/Reducer.cpp"
    )
    target_include_directories(genseq_reducer_property_tests PRIVATE
        ${EMISSION_INCLUDE_DIRS}
        "${SRC_DIR}/ui"
        "${SRC_DIR}/ui/state"
    )
    target_link_libraries(genseq_reducer_property_tests PRIVATE genseq_emission rapidcheck)
    add_test(NAME genseq_reducer_property_tests COMMAND genseq_reducer_property_tests)
endif()
