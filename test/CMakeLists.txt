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
# Sanitizer builds (performance-safe-foundation task 12.1, Req 11.4).
#
# Required host suites should run under AddressSanitizer + UndefinedBehavior-
# Sanitizer where the toolchain supports them, to catch ownership and payload
# lifetime bugs the fixed-capacity components rely on being absent. Opt in with
#   cmake -B test/build -S test -DGENSEQ_SANITIZE=ON
# and run ctest as usual; the flags apply to every target in this project. Off by
# default so the normal build stays fast and dependency-free. GCC and Clang both
# support these sanitizers; a toolchain that does not will simply reject the
# flags at configure/compile time, which is the signal to build without them.
# ---------------------------------------------------------------------------
option(GENSEQ_SANITIZE "Build host tests with ASan + UBSan (task 12.1)" OFF)
if(GENSEQ_SANITIZE)
    message(STATUS "GENSEQ_SANITIZE=ON: building with -fsanitize=address,undefined")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

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
#   - src/common/{pattern,pitch_set,velocity_set,gate_set,Scale}.cpp
#
# (Scale.cpp joined in task 5: the built-in generator list lives in
# RegisterGenerators.cpp below, and the scale-aware pitch plugin registered
# there requires the shared scale constraint math.)
#
# NOT compiled host-side (Pico SDK dependent):
#   - src/sequencer/UartMidiOutput.cpp   (uart/gpio)
#   - src/sequencer/sequencer_task.cpp   (multicore, receiveCommand)
#   - src/commands/command.cpp           (multicore FIFO)
# ---------------------------------------------------------------------------
set(EMISSION_SOURCES
    "${SRC_DIR}/sequencer/sequencer.cpp"
    "${SRC_DIR}/sequencer/generators/RegisterGenerators.cpp"
    "${SRC_DIR}/common/pattern.cpp"
    "${SRC_DIR}/common/pitch_set.cpp"
    "${SRC_DIR}/common/velocity_set.cpp"
    "${SRC_DIR}/common/gate_set.cpp"
    "${SRC_DIR}/common/Scale.cpp"
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
# Example test: canonical MIDI channel conversion and validation (task 2.1).
#
# A standalone example (not property-based) validating Requirements 5.1 and 5.9:
# one canonical internal protocol channel (0..15) covering all 16 channels with
# no aliases, lossless 1..16 user-channel conversion, removal of the old channel
# 0/1 alias, and common::Pattern's canonical-channel accessor.
#
# It includes only the SDK-free common/MidiChannel.h and common/pattern.h and
# links genseq_emission for the Pattern/common symbols; NO Pico SDK libs and NO
# RapidCheck, so it builds host-side unconditionally.
#
# Its own main() means it MUST be excluded from the property-test glob below.
# ---------------------------------------------------------------------------
set(EXAMPLE_CANONICAL_CHANNEL_SRC "${TEST_DIR}/example_canonical_channel.cpp")

add_executable(genseq_example_canonical_channel "${EXAMPLE_CANONICAL_CHANNEL_SRC}")
target_include_directories(genseq_example_canonical_channel PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_canonical_channel PRIVATE genseq_emission)
add_test(NAME genseq_example_canonical_channel COMMAND genseq_example_canonical_channel)

# ---------------------------------------------------------------------------
# Example test: supported Time Signature ranges (task 3.1).
#
# A standalone example (not property-based) validating Requirements 1.3 and 1.7:
# bounded numerator (1..32) and the power-of-two denominator set {1,2,4,8,16,32}
# whose every member divides 96 exactly at PPQN 24, exact beat/bar tick
# derivation, and rejection (not rounding) of unsupported values.
#
# It includes only the SDK-free common/TimeSignature.h — header-only, so it needs
# no library link at all, but links genseq_emission for consistency with the
# sibling examples; NO Pico SDK libs and NO RapidCheck, so it builds host-side
# unconditionally. Its own main() means it MUST be excluded from the glob below.
# ---------------------------------------------------------------------------
set(EXAMPLE_TIME_SIGNATURE_SRC "${TEST_DIR}/example_time_signature.cpp")

add_executable(genseq_example_time_signature "${EXAMPLE_TIME_SIGNATURE_SRC}")
target_include_directories(genseq_example_time_signature PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_time_signature PRIVATE genseq_emission)
add_test(NAME genseq_example_time_signature COMMAND genseq_example_time_signature)

# ---------------------------------------------------------------------------
# Example test: stable PatternId runtime identity (task 2.2).
#
# A standalone example (not property-based) validating Requirements 5.1, 6.9,
# 7.8: a PatternId stays stable when another pattern is removed/reordered, a
# reused slot gets a higher generation so a stale reference is rejected, and the
# allocator is fixed-capacity and allocation-free.
#
# It includes only the SDK-free header-only common/PatternId.h; links
# genseq_emission for consistency with the sibling examples (no symbols actually
# needed); NO Pico SDK libs and NO RapidCheck, so it builds host-side
# unconditionally. Its own main() means it MUST be excluded from the glob below.
# ---------------------------------------------------------------------------
set(EXAMPLE_PATTERN_ID_SRC "${TEST_DIR}/example_pattern_id.cpp")

add_executable(genseq_example_pattern_id "${EXAMPLE_PATTERN_ID_SRC}")
target_include_directories(genseq_example_pattern_id PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_pattern_id PRIVATE genseq_emission)
add_test(NAME genseq_example_pattern_id COMMAND genseq_example_pattern_id)

# ---------------------------------------------------------------------------
# Example test: centralized NoteLifecycle (task 2.3).
#
# A standalone example validating Requirements 5.1-5.5, 5.8: ownership recorded
# at onset, release by recorded identity, overlapping-pattern retrigger with an
# aggregate refcount (final note-off only when the last owner releases),
# monophonic replacement, and releaseAll/panic leaving zero per-pattern AND
# aggregate ownership.
#
# NoteLifecycle.h is header-only and SDK-free (IMidiOutput + canonical
# channel/id types + MIDI byte tables). The test uses the host RecordingMidiOutput
# and links genseq_emission for consistency; NO Pico SDK libs, NO RapidCheck.
# Its own main() means it MUST be excluded from the glob below.
# ---------------------------------------------------------------------------
set(EXAMPLE_NOTE_LIFECYCLE_SRC "${TEST_DIR}/example_note_lifecycle.cpp")

add_executable(genseq_example_note_lifecycle "${EXAMPLE_NOTE_LIFECYCLE_SRC}")
target_include_directories(genseq_example_note_lifecycle PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_note_lifecycle PRIVATE genseq_emission)
add_test(NAME genseq_example_note_lifecycle COMMAND genseq_example_note_lifecycle)

# ---------------------------------------------------------------------------
# Example test: TransportTimeline (task 3.2).
#
# A standalone example validating Requirements 1.1-1.5: start() establishes the
# timeline, beat/bar boundaries derive from the tick index and meter, long-run
# tick timestamps are drift-free (carried remainder, checked against an exact
# rational reference across BPMs), a mid-run BPM change preserves tick index and
# phase, and an unsupported meter/zero BPM is rejected.
#
# TransportTimeline.h is header-only and SDK-free. Links genseq_emission for
# consistency; NO Pico SDK libs, NO RapidCheck. Its own main() means it MUST be
# excluded from the glob below.
# ---------------------------------------------------------------------------
set(EXAMPLE_TRANSPORT_TIMELINE_SRC "${TEST_DIR}/example_transport_timeline.cpp")

add_executable(genseq_example_transport_timeline "${EXAMPLE_TRANSPORT_TIMELINE_SRC}")
target_include_directories(genseq_example_transport_timeline PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_transport_timeline PRIVATE genseq_emission)
add_test(NAME genseq_example_transport_timeline COMMAND genseq_example_transport_timeline)

# ---------------------------------------------------------------------------
# Example test: Sequencer integration of NoteLifecycle + TransportTimeline
# (tasks 2.4, 2.5, 3.3, 4.1).
#
# Drives the REAL Sequencer through its public API with a FakeMonotonicClock to
# validate the wave-3 hot-path wiring: falling edges release the recorded onset
# (even after a pitch edit), Stop/mute/remove release ownership, repeated Play
# is a no-op, and the unified MIDI Clock emits one Start / one Clock per tick /
# one Stop.
#
# It links genseq_emission (real Sequencer + common) and uses the host
# RecordingMidiOutput + FakeMonotonicClock; NO Pico SDK libs, NO RapidCheck. Its
# own main() means it MUST be excluded from the glob below.
# ---------------------------------------------------------------------------
set(EXAMPLE_SEQUENCER_INTEGRATION_SRC "${TEST_DIR}/example_sequencer_integration.cpp")

add_executable(genseq_example_sequencer_integration "${EXAMPLE_SEQUENCER_INTEGRATION_SRC}")
target_include_directories(genseq_example_sequencer_integration PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_sequencer_integration PRIVATE genseq_emission)
add_test(NAME genseq_example_sequencer_integration COMMAND genseq_example_sequencer_integration)

# ---------------------------------------------------------------------------
# Example test: allocation-free core1 command path (task 6.4).
#
# Proves Sequencer::processCommand(msg, snapshot) — the application half of the
# core1 cut-over to commands::receiveCommandInto — is behaviorally equivalent to
# the legacy scalar (CommandMessage-vector) path, and validates set commands
# against the PatternSnapshot's counts. SDK-free (Sequencer + PatternSnapshot).
# ---------------------------------------------------------------------------
set(EXAMPLE_COMMAND_SNAPSHOT_PATH_SRC "${TEST_DIR}/example_command_snapshot_path.cpp")

add_executable(genseq_example_command_snapshot_path "${EXAMPLE_COMMAND_SNAPSHOT_PATH_SRC}")
target_include_directories(genseq_example_command_snapshot_path PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_command_snapshot_path PRIVATE genseq_emission)
add_test(NAME genseq_example_command_snapshot_path COMMAND genseq_example_command_snapshot_path)

# ---------------------------------------------------------------------------
# Example test: steady-state zero-allocation gate (task 12.2 / Req 7.9).
#
# Overrides global operator new to count allocations and asserts the per-tick
# scheduling+emission path and the allocation-free command-decode path perform
# zero heap allocations after warm-up. Documents (does not assert zero) the
# set-application allocation and the fixed queue budgets. SDK-free.
# ---------------------------------------------------------------------------
set(EXAMPLE_STEADY_STATE_ALLOCATION_SRC "${TEST_DIR}/example_steady_state_allocation.cpp")

add_executable(genseq_example_steady_state_allocation "${EXAMPLE_STEADY_STATE_ALLOCATION_SRC}")
target_include_directories(genseq_example_steady_state_allocation PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_steady_state_allocation PRIVATE genseq_emission)
add_test(NAME genseq_example_steady_state_allocation COMMAND genseq_example_steady_state_allocation)

# ---------------------------------------------------------------------------
# Example test: fixed-capacity MIDI event queue (task 4.2).
#
# A standalone example validating Requirements 4.1, 4.2, 4.5, 4.7: deterministic
# coincident ordering (timestamp, class priority, PatternId), real-time priority,
# fixed capacity with no growth, and reserved safety capacity (ordinary events
# refused into the reserve; a full-queue safety event reports DirectRequired).
#
# MidiEventQueue.h is header-only and SDK-free (only <cstdint> + canonical
# PatternId). Links genseq_emission for consistency; NO Pico SDK libs, NO
# RapidCheck. Its own main() means it MUST be excluded from the glob below.
# ---------------------------------------------------------------------------
set(EXAMPLE_MIDI_EVENT_QUEUE_SRC "${TEST_DIR}/example_midi_event_queue.cpp")

add_executable(genseq_example_midi_event_queue "${EXAMPLE_MIDI_EVENT_QUEUE_SRC}")
target_include_directories(genseq_example_midi_event_queue PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_midi_event_queue PRIVATE genseq_emission)
add_test(NAME genseq_example_midi_event_queue COMMAND genseq_example_midi_event_queue)

# ---------------------------------------------------------------------------
# Example test: Scheduler::service(now) front end (task 5.1).
#
# A standalone example validating Requirements 3.1, 3.2, 3.3: every serviced tick
# carries its intended (deadline) timestamp independent of the observation time,
# immediate safety work runs first on every call, and the per-service work is
# bounded by a documented tick budget (surplus overdue ticks reported, not
# drained in a burst; the next call resumes from the authoritative phase).
#
# Scheduler.h / TransportTimeline.h / PerformanceMetrics.h are header-only and
# SDK-free. Links genseq_emission for consistency; NO Pico SDK libs, NO
# RapidCheck. Its own main() means it MUST be excluded from the glob below.
# ---------------------------------------------------------------------------
set(EXAMPLE_SCHEDULER_SRC "${TEST_DIR}/example_scheduler.cpp")

add_executable(genseq_example_scheduler "${EXAMPLE_SCHEDULER_SRC}")
target_include_directories(genseq_example_scheduler PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_scheduler PRIVATE genseq_emission)
add_test(NAME genseq_example_scheduler COMMAND genseq_example_scheduler)

# ---------------------------------------------------------------------------
# Example test: output overload detection (task 4.3).
#
# A standalone example validating Requirements 4.4, 4.5, 4.6: current/max backlog
# accounting, a backlog sustained for one beat of monotonic time latching a
# sustained-overload signal, and a transient backlog that drains before one beat
# NOT latching. OverloadDetector.h is header-only and SDK-free.
# ---------------------------------------------------------------------------
set(EXAMPLE_OVERLOAD_DETECTOR_SRC "${TEST_DIR}/example_overload_detector.cpp")

add_executable(genseq_example_overload_detector "${EXAMPLE_OVERLOAD_DETECTOR_SRC}")
target_include_directories(genseq_example_overload_detector PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_overload_detector PRIVATE genseq_emission)
add_test(NAME genseq_example_overload_detector COMMAND genseq_example_overload_detector)

# ---------------------------------------------------------------------------
# Example test: fixed-capacity CommandInbox + sequence IDs/acks (tasks 6.1, 6.2).
#
# A standalone example validating Requirements 7.1-7.4, 7.8, 6.9: single-producer/
# single-consumer ring with complete-slot publish (no payload-word blocking),
# fixed capacity (full refused, not grown), bounded drain, monotonic sequence IDs
# with Accepted/Applied/Rejected ack status, and stale-generation rejection
# against the live PatternIdAllocator. CommandInbox.h is header-only and SDK-free.
# ---------------------------------------------------------------------------
set(EXAMPLE_COMMAND_INBOX_SRC "${TEST_DIR}/example_command_inbox.cpp")

add_executable(genseq_example_command_inbox "${EXAMPLE_COMMAND_INBOX_SRC}")
target_include_directories(genseq_example_command_inbox PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_command_inbox PRIVATE genseq_emission)
add_test(NAME genseq_example_command_inbox COMMAND genseq_example_command_inbox)

# ---------------------------------------------------------------------------
# Example test: fake-clock scheduler + fault integration (task 5.4).
#
# Drives the REAL Sequencer across the three lateness regimes: under one beat
# (reconcile, no catch-up burst, no fault), at/beyond one beat (fail closed:
# release notes, one Stop, latched cause, no auto-restart), and explicit Play
# restart after a fault. Links genseq_emission; NO SDK libs, NO RapidCheck.
# ---------------------------------------------------------------------------
set(EXAMPLE_SCHEDULER_FAULT_SRC "${TEST_DIR}/example_scheduler_fault.cpp")

add_executable(genseq_example_scheduler_fault "${EXAMPLE_SCHEDULER_FAULT_SRC}")
target_include_directories(genseq_example_scheduler_fault PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_scheduler_fault PRIVATE genseq_emission)
add_test(NAME genseq_example_scheduler_fault COMMAND genseq_example_scheduler_fault)

# ---------------------------------------------------------------------------
# Example test: fixed-capacity Pattern snapshot (task 6.4).
#
# Validates the allocation-free container core1 decodes commands into: gate
# bitset round-trip, count clamping at protocol caps, pitch/velocity round-trip,
# clear-and-reuse, and the documented fixed footprint. Header-only, SDK-free.
# ---------------------------------------------------------------------------
set(EXAMPLE_PATTERN_SNAPSHOT_SRC "${TEST_DIR}/example_pattern_snapshot.cpp")

add_executable(genseq_example_pattern_snapshot "${EXAMPLE_PATTERN_SNAPSHOT_SRC}")
target_include_directories(genseq_example_pattern_snapshot PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_pattern_snapshot PRIVATE genseq_emission)
add_test(NAME genseq_example_pattern_snapshot COMMAND genseq_example_pattern_snapshot)

# ---------------------------------------------------------------------------
# Example test: TimingPolicy + effective-tick resolution (task 7.1).
#
# Validates Immediate/Next Beat/Next Bar resolution against the Transport
# Timeline across meters, the on-boundary -> next-boundary convention, and isDue.
# Header-only, SDK-free.
# ---------------------------------------------------------------------------
set(EXAMPLE_TIMING_POLICY_SRC "${TEST_DIR}/example_timing_policy.cpp")

add_executable(genseq_example_timing_policy "${EXAMPLE_TIMING_POLICY_SRC}")
target_include_directories(genseq_example_timing_policy PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_timing_policy PRIVATE genseq_emission)
add_test(NAME genseq_example_timing_policy COMMAND genseq_example_timing_policy)

# ---------------------------------------------------------------------------
# Example test: Save Snapshot semantics (task 8.1).
#
# Validates Requirements 8.1-8.4: capture() takes an immutable by-value copy of
# accepted Working State; a later edit to the live state cannot mutate the
# in-progress snapshot; accepted (pending-application) changes are included and
# unkept drafts excluded (via what the caller passes). SaveSnapshot.h +
# PersistableConfig.h are header-only and SDK-free.
# ---------------------------------------------------------------------------
set(EXAMPLE_SAVE_SNAPSHOT_SRC "${TEST_DIR}/example_save_snapshot.cpp")

add_executable(genseq_example_save_snapshot "${EXAMPLE_SAVE_SNAPSHOT_SRC}")
target_include_directories(genseq_example_save_snapshot PRIVATE
    ${EMISSION_INCLUDE_DIRS}
    "${SRC_DIR}/persistence"
)
target_link_libraries(genseq_example_save_snapshot PRIVATE genseq_emission)
add_test(NAME genseq_example_save_snapshot COMMAND genseq_example_save_snapshot)

# ---------------------------------------------------------------------------
# Example test: atomic Pattern snapshot application (task 7.4).
#
# Validates Requirements 5.7, 6.8, 7.1: the complete gate/pitch/velocity material
# is swapped atomically; a sounding note (owned by NoteLifecycle) survives; the
# release uses the recorded onset. Header-only applier; links genseq_emission for
# common::Pattern + the sets.
# ---------------------------------------------------------------------------
set(EXAMPLE_PATTERN_SNAPSHOT_APPLY_SRC "${TEST_DIR}/example_pattern_snapshot_apply.cpp")

add_executable(genseq_example_pattern_snapshot_apply "${EXAMPLE_PATTERN_SNAPSHOT_APPLY_SRC}")
target_include_directories(genseq_example_pattern_snapshot_apply PRIVATE ${EMISSION_INCLUDE_DIRS})
target_link_libraries(genseq_example_pattern_snapshot_apply PRIVATE genseq_emission)
add_test(NAME genseq_example_pattern_snapshot_apply COMMAND genseq_example_pattern_snapshot_apply)

# ---------------------------------------------------------------------------
# Example test: bounded nonblocking save service (task 8.2).
#
# Validates Requirements 8.5-8.9: begin/pump/terminal state machine over the
# in-memory FakeFlashStore, distinct SUCCESS/FAIL outcomes, snapshot immutability
# across post-begin edits, no overlapping saves, and the UI/input service-gap
# metric. Links genseq_persistence (serialize/manager); SaveService/SaveSnapshot
# are header-only.
# ---------------------------------------------------------------------------
set(EXAMPLE_SAVE_SERVICE_SRC "${TEST_DIR}/example_save_service.cpp")

add_executable(genseq_example_save_service "${EXAMPLE_SAVE_SERVICE_SRC}")
target_include_directories(genseq_example_save_service PRIVATE
    ${EMISSION_INCLUDE_DIRS}
    "${SRC_DIR}/persistence"
    "${TEST_DIR}/persistence"
)
target_link_libraries(genseq_example_save_service PRIVATE genseq_persistence)
add_test(NAME genseq_example_save_service COMMAND genseq_example_save_service)

# ---------------------------------------------------------------------------
# Example test: distinct latched Timing Fault UI state (task 11.1).
#
# Validates Requirements 10.3, 10.4, 10.7: the latched fault indicator is
# distinct from the SaveBanner feedback, preserves Working State, is cleared only
# explicitly, and retains a machine-readable cause. ui/state/UIState.h is
# header-only (depends only on the header-only common::PlayingOrder enum).
# ---------------------------------------------------------------------------
set(EXAMPLE_FAULT_UI_STATE_SRC "${TEST_DIR}/example_fault_ui_state.cpp")

add_executable(genseq_example_fault_ui_state "${EXAMPLE_FAULT_UI_STATE_SRC}")
target_include_directories(genseq_example_fault_ui_state PRIVATE
    ${EMISSION_INCLUDE_DIRS}
    "${SRC_DIR}/ui"
    "${SRC_DIR}/ui/state"
)
target_link_libraries(genseq_example_fault_ui_state PRIVATE genseq_emission)
add_test(NAME genseq_example_fault_ui_state COMMAND genseq_example_fault_ui_state)

# ---------------------------------------------------------------------------
# Example test: backend-neutral A/B record store (task 10.1).
#
# Validates Req 9.1 + the A/B transactional shape for 9.6/9.7: commit alternates
# slots and bumps generation; load selects the newest valid committed record; a
# failed/interrupted commit preserves the previous record; capacity guard. Links
# genseq_persistence (serialize/deserialize) + the in-memory FakeFlashStore.
# ---------------------------------------------------------------------------
set(EXAMPLE_RECORD_STORE_SRC "${TEST_DIR}/example_record_store.cpp")

add_executable(genseq_example_record_store "${EXAMPLE_RECORD_STORE_SRC}")
target_include_directories(genseq_example_record_store PRIVATE
    ${EMISSION_INCLUDE_DIRS}
    "${SRC_DIR}/persistence"
    "${TEST_DIR}/persistence"
)
target_link_libraries(genseq_example_record_store PRIVATE genseq_persistence)
add_test(NAME genseq_example_record_store COMMAND genseq_example_record_store)

# ---------------------------------------------------------------------------
# Example test: MB85RS2MTA FRAM driver protocol (task 9.3 prep).
#
# Proves the SDK-free Mb85rsFram IFlashStore driver against a host device model
# (FakeFramSpi): round-trip, 3-byte addressing beyond 64 KB, WREN sequencing,
# read-back verify, device-id, and RecordStore backend-neutrality over FRAM.
# ---------------------------------------------------------------------------
set(EXAMPLE_FRAM_DRIVER_SRC "${TEST_DIR}/example_fram_driver.cpp")

add_executable(genseq_example_fram_driver "${EXAMPLE_FRAM_DRIVER_SRC}")
target_include_directories(genseq_example_fram_driver PRIVATE
    ${EMISSION_INCLUDE_DIRS}
    "${SRC_DIR}/persistence"
    "${TEST_DIR}/persistence"
)
target_link_libraries(genseq_example_fram_driver PRIVATE genseq_persistence)
add_test(NAME genseq_example_fram_driver COMMAND genseq_example_fram_driver)

# ---------------------------------------------------------------------------
# Example test: persistent fault indication + explicit-Play ack (tasks 11.2/11.3).
#
# Validates Req 10.4/10.5/10.6: the fault matrix color is distinct and takes
# precedence, does not auto-dismiss, and an explicit Play clears the latch (only
# then). ui/state/FaultIndication.h + UIState.h are header-only.
# ---------------------------------------------------------------------------
set(EXAMPLE_FAULT_INDICATION_SRC "${TEST_DIR}/example_fault_indication.cpp")

add_executable(genseq_example_fault_indication "${EXAMPLE_FAULT_INDICATION_SRC}")
target_include_directories(genseq_example_fault_indication PRIVATE
    ${EMISSION_INCLUDE_DIRS}
    "${SRC_DIR}/ui"
    "${SRC_DIR}/ui/state"
)
target_link_libraries(genseq_example_fault_indication PRIVATE genseq_emission)
add_test(NAME genseq_example_fault_indication COMMAND genseq_example_fault_indication)

# ---------------------------------------------------------------------------
# Example test: snapshot + save-coordination (task 8.3).
#
# Validates Req 8.1-8.9: draft/working/pending distinctions, mutation after
# capture, a transactional A/B failure preserving the previous record, and that
# unsafe coordination (capacity/injected failure) performs ZERO destructive
# writes. Links genseq_persistence + the in-memory FakeFlashStore.
# ---------------------------------------------------------------------------
set(EXAMPLE_SAVE_COORDINATION_SRC "${TEST_DIR}/example_save_coordination.cpp")

add_executable(genseq_example_save_coordination "${EXAMPLE_SAVE_COORDINATION_SRC}")
target_include_directories(genseq_example_save_coordination PRIVATE
    ${EMISSION_INCLUDE_DIRS}
    "${SRC_DIR}/persistence"
    "${TEST_DIR}/persistence"
)
target_link_libraries(genseq_example_save_coordination PRIVATE genseq_persistence)
add_test(NAME genseq_example_save_coordination COMMAND genseq_example_save_coordination)

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
    # The canonical-channel example (task 2.1) is its own dedicated target with
    # its own main(); exclude it from the shared runner.
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_CANONICAL_CHANNEL_SRC}")
    # The time-signature example (task 3.1) is its own dedicated target with its
    # own main(); exclude it from the shared runner.
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_TIME_SIGNATURE_SRC}")
    # The pattern-id example (task 2.2) is its own dedicated target with its own
    # main(); exclude it from the shared runner.
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_PATTERN_ID_SRC}")
    # The NoteLifecycle (2.3) and TransportTimeline (3.2) examples are their own
    # dedicated targets with their own main(); exclude them from the shared runner.
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_NOTE_LIFECYCLE_SRC}")
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_TRANSPORT_TIMELINE_SRC}")
    # The Sequencer integration example (tasks 2.4/2.5/3.3/4.1) is its own target
    # with its own main(); exclude it from the shared runner.
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_SEQUENCER_INTEGRATION_SRC}")
    # The allocation-free command-path example (6.4) is its own target with its
    # own main(); exclude it from the shared runner.
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_COMMAND_SNAPSHOT_PATH_SRC}")
    # The steady-state allocation gate (12.2) overrides global operator new and
    # has its own main(); it MUST be its own target, never in the shared runner.
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_STEADY_STATE_ALLOCATION_SRC}")
    # The MIDI event queue (4.2) and Scheduler (5.1) examples are their own
    # targets with their own main(); exclude them from the shared runner.
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_MIDI_EVENT_QUEUE_SRC}")
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_SCHEDULER_SRC}")
    # The overload detector (4.3) and CommandInbox (6.1/6.2) examples are their
    # own targets with their own main(); exclude them from the shared runner.
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_OVERLOAD_DETECTOR_SRC}")
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_COMMAND_INBOX_SRC}")
    # Wave-6 examples (5.4 scheduler-fault, 6.4 pattern-snapshot, 7.1 timing-
    # policy) are their own targets with their own main(); exclude them.
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_SCHEDULER_FAULT_SRC}")
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_PATTERN_SNAPSHOT_SRC}")
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_TIMING_POLICY_SRC}")
    # Wave-7 save-snapshot example (8.1) is its own target with its own main().
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_SAVE_SNAPSHOT_SRC}")
    # Wave-8 examples (7.4 apply, 8.2 save-service, 11.1 fault-ui-state) are
    # their own targets with their own main(); exclude them from the runner.
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_PATTERN_SNAPSHOT_APPLY_SRC}")
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_SAVE_SERVICE_SRC}")
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_FAULT_UI_STATE_SRC}")
    # Wave-9 host examples (10.1 record-store, 11.2/11.3 fault-indication, 8.3
    # save-coordination) are their own targets with their own main().
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_RECORD_STORE_SRC}")
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_FRAM_DRIVER_SRC}")
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_FAULT_INDICATION_SRC}")
    list(REMOVE_ITEM TEST_SOURCES "${EXAMPLE_SAVE_COORDINATION_SRC}")

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

# ---------------------------------------------------------------------------
# Unit tests (generative-engine-expansion): test/unit/*.cpp.
#
# Standalone unit tests with their own main(). They live in test/unit/ so the
# property-test glob (test/*.cpp) never sweeps them, and each is built
# unconditionally: they exercise header-only SDK-free components and need NO
# RapidCheck. The ctest names match the plan's verification regexes exactly
# (ctest --test-dir build/host -R <test_name>).
# ---------------------------------------------------------------------------

# Task 11: three-level immediate/quantized timing policy (FR10, AC7).
# Validates OperationTimingConfig + the resolveTiming/computeApplyTick
# extension of TimingPolicy against the authoritative TransportTimeline.
set(UNIT_TIMING_POLICY_SRC "${TEST_DIR}/unit/test_timing_policy.cpp")

add_executable(genseq_test_timing_policy "${UNIT_TIMING_POLICY_SRC}")
target_include_directories(genseq_test_timing_policy PRIVATE ${EMISSION_INCLUDE_DIRS})
# Link the emission logic but NONE of the Pico SDK libs and NO RapidCheck;
# TimingPolicy.h / OperationTimingConfig.h are header-only and SDK-free.
target_link_libraries(genseq_test_timing_policy PRIVATE genseq_emission)
add_test(NAME test_timing_policy COMMAND genseq_test_timing_policy)

# Task 1 (generative-engine-expansion): built-in scale templates + scale math
# (FR3, FR4, AC2).
#
# Validates the fixed read-only template table (20-30 entries, unique non-empty
# names, root bit set, intervals within 0-11) and the Scale value type
# (containsNote against the mask for every root across MIDI 0-127,
# nearestNoteInScale with the deterministic lower-note tie-break, the
# degreeToNote / noteToDegree round-trip with octave wrapping, and chromatic
# completeness).
set(UNIT_SCALE_SRC "${TEST_DIR}/unit/test_scale.cpp")

add_executable(genseq_test_scale
    "${UNIT_SCALE_SRC}"
    "${SRC_DIR}/common/Scale.cpp"
)
target_include_directories(genseq_test_scale PRIVATE ${EMISSION_INCLUDE_DIRS})
# Scale.cpp is a generative-engine translation unit. It joined genseq_emission
# in task 5 (the built-in pitch generator registered by RegisterGenerators.cpp
# requires the shared scale constraint math), but this target links NO
# library at all — so Scale.cpp is still compiled directly into it. NONE of
# the Pico SDK libs and NO RapidCheck; ScaleTemplates.h / Scale.h are
# SDK-free.
add_test(NAME test_scale COMMAND genseq_test_scale)

# Task 15: global modulation patterns and per-target config (FR11, FR13, FR14).
# Validates ModulationPattern / ModulationTarget / ModulationRegistry: step
# clamping and capacity, transport-derived step indices for beat/bar step
# durations, every playing order, independent targets on one pattern, the
# scaling/balance formula clamped to the target range, per-pattern default
# scope and the registry's fixed capacity.
set(UNIT_MODULATION_PATTERN_SRC "${TEST_DIR}/unit/test_modulation_pattern.cpp")

add_executable(genseq_test_modulation_pattern "${UNIT_MODULATION_PATTERN_SRC}")
target_include_directories(genseq_test_modulation_pattern PRIVATE ${EMISSION_INCLUDE_DIRS})
# Link the emission logic but NONE of the Pico SDK libs and NO RapidCheck;
# the modulation headers are header-only and SDK-free.
target_link_libraries(genseq_test_modulation_pattern PRIVATE genseq_emission)
add_test(NAME test_modulation_pattern COMMAND genseq_test_modulation_pattern)

# Task 2 (generative-engine-expansion): generator plugin interface (FR1, AC1,
# AC8).
#
# Validates GeneratorCapabilities / GeneratorParamDef / IGenerator /
# GeneratorRegistry with test-only fake generators: registration and id
# lookup, duplicate and capacity rejection, the capability bits, param-def
# iteration (count, ranges, defaults), base-pointer dispatch with the
# context/RNG plumbing, and the (currently empty) built-in registration
# list that lives solely in RegisterGenerators.cpp.
set(UNIT_GENERATOR_REGISTRY_SRC "${TEST_DIR}/unit/test_generator_registry.cpp")

add_executable(genseq_test_generator_registry
    "${UNIT_GENERATOR_REGISTRY_SRC}"
    "${SRC_DIR}/sequencer/generators/RegisterGenerators.cpp"
    "${SRC_DIR}/common/Scale.cpp"
)
target_include_directories(genseq_test_generator_registry PRIVATE ${EMISSION_INCLUDE_DIRS})
# The generator plugin interface headers are header-only and SDK-free;
# RegisterGenerators.cpp (the ONLY built-in listing, AC1) is compiled directly
# into this test target, together with Scale.cpp (the built-in pitch plugin
# registered there requires the shared scale constraint math). NONE of the
# Pico SDK libs and NO RapidCheck.
add_test(NAME test_generator_registry COMMAND genseq_test_generator_registry)

# Task 3 (generative-engine-expansion): per-pattern generative state and
# registry-based dispatch (FR1, FR4, AC1).
#
# Validates PatternGenerativeState (fixed POD: trivially copyable, standard
# layout, bounded size; valid defaults) and the Sequencer's per-role
# generator dispatch with test-only fake generators: dispatch by id for the
# right role (gate/pitch/velocity/duration), per-role fallback to the stored
# pattern data when no generator is assigned (or the id is unregistered),
# per-pattern state isolation across add/remove, and a zero-allocation gate
# on the dispatching tick path. The test drives the REAL Sequencer through
# the emission library with a LOCAL GeneratorRegistry, so the core files
# never name a concrete generator type (AC1).
set(UNIT_PATTERN_GENERATIVE_STATE_SRC "${TEST_DIR}/unit/test_pattern_generative_state.cpp")

add_executable(genseq_test_pattern_generative_state "${UNIT_PATTERN_GENERATIVE_STATE_SRC}")
target_include_directories(genseq_test_pattern_generative_state PRIVATE ${EMISSION_INCLUDE_DIRS})
# Link the emission logic (REAL Sequencer + common) but NONE of the Pico SDK
# libs and NO RapidCheck; PatternGenerativeState.h / the generator interface
# headers are header-only and SDK-free.
target_link_libraries(genseq_test_pattern_generative_state PRIVATE genseq_emission)
add_test(NAME test_pattern_generative_state COMMAND genseq_test_pattern_generative_state)

# Task 4 (generative-engine-expansion): Euclidean gate generator plugin
# (FR2, AC1).
#
# Golden-output tests: the step patterns were captured from the PRE-refactor
# common::GateSet::createEuclidean and pinned as literals, so the plugin
# (delegating to the shared common::euclideanPulseAt free function in
# gate_set.h, which createEuclidean also uses) is verified against the
# ORIGINAL algorithm. Covers representative (steps, pulses, rotation)
# tuples plus the edge cases 0 pulses, pulses == steps, pulses > steps and
# steps == 1, the wrapped second pattern revolution, an enumeration over
# the FULL supported ranges (steps 1..64, pulses 0..steps, rotation
# 0..steps-1) comparing the legacy createEuclidean output against the
# plugin output, GATE-only capabilities, paramDefs matching the existing
# UI limits (steps 1..64, pulses 0..64, rotation 0..63), the registration
# of "builtin.euclidean" by RegisterGenerators.cpp (the only built-in
# listing, AC1), and the default-id constant
# PatternGenerativeState::kDefaultGateGeneratorId naming that registered
# plugin (the default gate generator).
set(UNIT_EUCLIDEAN_GENERATOR_SRC "${TEST_DIR}/unit/test_euclidean_generator.cpp")

add_executable(genseq_test_euclidean_generator "${UNIT_EUCLIDEAN_GENERATOR_SRC}")
target_include_directories(genseq_test_euclidean_generator PRIVATE ${EMISSION_INCLUDE_DIRS})
# Link the emission logic (gate_set.cpp + RegisterGenerators.cpp) but NONE of
# the Pico SDK libs and NO RapidCheck; EuclideanGateGenerator.h is header-only
# and SDK-free.
target_link_libraries(genseq_test_euclidean_generator PRIVATE genseq_emission)
add_test(NAME test_euclidean_generator COMMAND genseq_test_euclidean_generator)

# Task 12 (generative-engine-expansion): global undo stack + working state
# manager (FR9, AC5, AC6).
#
# Validates UndoStack (depth clamped to 5-16, ring overwrite dropping the
# oldest entry, empty-stack no-op, fixed memory bound) and
# WorkingStateManager (per-pattern snapshots on ONE global stack, N undos
# after N mutations across different patterns, revert restoring the saved
# baseline for all patterns and clearing the stack, out-of-range rejection).
# UndoStack.h / WorkingStateManager.h are header-only and SDK-free.
set(UNIT_UNDO_STACK_SRC "${TEST_DIR}/unit/test_undo_stack.cpp")

add_executable(genseq_test_undo_stack "${UNIT_UNDO_STACK_SRC}")
target_include_directories(genseq_test_undo_stack PRIVATE ${EMISSION_INCLUDE_DIRS})
# Link the emission logic but NONE of the Pico SDK libs and NO RapidCheck;
# UndoStack.h / WorkingStateManager.h are header-only and SDK-free.
target_link_libraries(genseq_test_undo_stack PRIVATE genseq_emission)
add_test(NAME test_undo_stack COMMAND genseq_test_undo_stack)

# Task 13 (generative-engine-expansion): the Keep operation (FR15).
#
# Validates WorkingStateManager::keep(): the working state is unchanged by
# mutate+keep, revert after keep restores the KEPT state (not the older
# baseline), the undo stack is empty after keep (and post-keep records work
# again), and FR8 distance limits are measured from the kept state — the
# manager's baseline becomes the withinDistance anchor after Keep.
# WorkingStateManager.h / MutationConstraints.h are header-only and SDK-free.
set(UNIT_KEEP_OPERATION_SRC "${TEST_DIR}/unit/test_keep_operation.cpp")

add_executable(genseq_test_keep_operation "${UNIT_KEEP_OPERATION_SRC}")
target_include_directories(genseq_test_keep_operation PRIVATE ${EMISSION_INCLUDE_DIRS})
# Link the emission logic but NONE of the Pico SDK libs and NO RapidCheck;
# the keep operation lives entirely in the header-only SDK-free manager.
target_link_libraries(genseq_test_keep_operation PRIVATE genseq_emission)
add_test(NAME test_keep_operation COMMAND genseq_test_keep_operation)

# Task 8 (generative-engine-expansion): mutation operator architecture
# (FR5-FR8).
#
# Validates MutationConstraints (the FR7 target bitmask set/test/clear, the
# FR6 intensity clamped to 0-127, the FR8 distance-limit helper over two
# PatternGenerativeState snapshots, and the FR8 invariant checks: scale lock
# via the T1 Scale, locked fields, preserved pulse count) and the
# IMutationOperator contract (id / name / supportedTargets / apply) with a
# test-only stub operator reporting Applied / NoChange / Rejected plus its
# changed-target mask and restoring the state on rejection.
set(UNIT_MUTATION_FRAMEWORK_SRC "${TEST_DIR}/unit/test_mutation_framework.cpp")

add_executable(genseq_test_mutation_framework
    "${UNIT_MUTATION_FRAMEWORK_SRC}"
    "${SRC_DIR}/common/Scale.cpp"
)
target_include_directories(genseq_test_mutation_framework PRIVATE ${EMISSION_INCLUDE_DIRS})
# Like test_scale, the target compiles Scale.cpp directly (the scale-lock
# invariant calls common::Scale::containsNote) and links NO library, so the
# direct compile stays; every mutation-framework header is header-only and
# SDK-free. NONE of the Pico SDK libs and NO RapidCheck.
add_test(NAME test_mutation_framework COMMAND genseq_test_mutation_framework)

# Task 5 (generative-engine-expansion): scale-aware pitch generator plugin
# (FR2, FR4, AC2).
#
# Validates ScaleAwarePitchGenerator: every generated pitch satisfies
# Scale::containsNote for EVERY built-in template and root 0-11 (including
# the total no-in-scale-note-in-range fallback), fixed-seed determinism,
# the low/high note range respected within MIDI 0-127 (plus configure()
# normalization), PITCH-only capabilities, full in-range scale-note
# reachability, the null-scale default-C-Major fallback, the param table,
# and the registration of "builtin.pitch" by RegisterGenerators.cpp (the
# only built-in listing, AC1).
set(UNIT_SCALE_PITCH_GENERATOR_SRC "${TEST_DIR}/unit/test_scale_pitch_generator.cpp")

add_executable(genseq_test_scale_pitch_generator
    "${UNIT_SCALE_PITCH_GENERATOR_SRC}"
    "${SRC_DIR}/common/Scale.cpp"
    "${SRC_DIR}/sequencer/generators/RegisterGenerators.cpp"
)
target_include_directories(genseq_test_scale_pitch_generator PRIVATE ${EMISSION_INCLUDE_DIRS})
# Like test_scale / test_mutation_framework, the target compiles Scale.cpp
# directly (and links NO library, so the direct compile is required) plus
# RegisterGenerators.cpp (the ONLY built-in listing, AC1); the generator
# header itself is header-only and SDK-free. NONE of the Pico SDK libs and
# NO RapidCheck.
add_test(NAME test_scale_pitch_generator COMMAND genseq_test_scale_pitch_generator)
