# USB Keyboard Input Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the GPIO button and ADC potentiometer input wiring with a USB HID keyboard attached to the Pico's native USB port, with the reducer dispatching on key identity plus modifiers.

**Architecture:** The input layer splits into `KeyboardDecoder` (pure logic — HID report diffing and auto-repeat, no Pico or TinyUSB headers, host-testable) and `UsbKeyboard` (a thin TinyUSB host adapter that supplies reports and a millisecond clock). Decoded events flow into the existing `StateManager::dispatch()` unchanged. `reduce()` gains a global layer that consumes `F1`–`F12` and `space` before delegating to the active view.

**Tech Stack:** C++17, Raspberry Pi Pico SDK 2.1.1, TinyUSB host (`tinyusb_host`, boot-protocol HID), CMake + Ninja. Host tests build as a separate native CMake project with a hand-rolled ~40-line assertion harness (no external test dependency).

**Spec:** `docs/superpowers/specs/2026-08-21-usb-keyboard-input-design.md`

## Global Constraints

- Branch: `feature/usb-keyboard-input`. All commits land here.
- C++ standard is 17 (`CMAKE_CXX_STANDARD 17`); C standard is 11.
- `KeyId` enumerator values ARE HID usage codes. Never renumber them.
- `mod::CTRL/SHIFT/ALT/GUI` = `1<<0 .. 1<<3`, matching the HID modifier byte's low nibble.
- `REPEAT_DELAY_MS = 400`, `REPEAT_INTERVAL_MS = 30`.
- `value` clamps to 0–99, enforced in exactly one place: `state::setValue`.
- `pico_enable_stdio_usb(genseq 0)` stays `0`. Tracing is UART1 only; the native USB port belongs to the host stack.
- `tusb_config.h` must NOT define `CFG_TUSB_MCU`, `CFG_TUSB_OS` or `CFG_TUSB_DEBUG` — the SDK's `tinyusb_common_base` target already supplies them, and redefining them causes macro-redefinition errors.
- `Encoder`, `Display`, `LCD_I2C`, `Led`, `LedMatrix` and everything under `src/ui/hardware/driver/` are NOT modified by this plan.
- Firmware build command: `cmake --build build -j` (the `build/` directory is already configured with `PICO_SDK_PATH=/home/chris/.pico-sdk/sdk/2.1.1`). It must succeed at the end of every task.
- Host test command: `cmake -S tests -B tests/build && cmake --build tests/build -j && ./tests/build/genseq_tests`. `.gitignore` already contains a bare `build` pattern, so `tests/build/` is ignored — do not add it.

---

## File Structure

**Created:**

| File | Responsibility |
|---|---|
| `src/ui/KeyNames.{h,cpp}` | `toName(KeyId)` for tracing/labels. Not used by the reducer. |
| `src/ui/hardware/KeyboardDecoder.{h,cpp}` | HID report diffing, key-down tracking, auto-repeat. Zero Pico/TinyUSB dependencies. |
| `src/ui/hardware/UsbKeyboard.{h,cpp}` | TinyUSB host init/task, C callbacks, report → `KeyReport`, clock, dispatch to `StateManager`. |
| `src/config/tusb_config.h` | TinyUSB host configuration. |
| `tests/framework.h` | CHECK/CHECK_EQ/CHECK_STREQ macros, test registry, reporting runner. |
| `tests/main.cpp` | `int main()` calling the runner. |
| `tests/CMakeLists.txt` | Standalone native project (cannot live in the firmware build — the top-level CMake calls `pico_sdk_init()`). |
| `tests/stubs/pico/multicore.h` | Empty header satisfying `commands/command.h`. |
| `tests/stubs/command_stub.{h,cpp}` | Records `sendCommand()` calls so tests can assert PLAY/STOP. |
| `tests/stubs/hardware_stub.cpp` | No-op `Led`/`LedMatrix` bodies so `InitView.cpp` links on host. |
| `tests/test_types.cpp` | `combo()`, `toName()`, `Event` factories. |
| `tests/test_decoder.cpp` | Decoder press/release/repeat/rollover/disconnect. |
| `tests/test_reducer.cpp` | Global layer + delegation via a `FakeView`. |
| `tests/test_initview.cpp` | `InitView` sweep bindings and clamping. |

**Deleted:** `src/ui/hardware/Button.{h,cpp}`, `src/ui/hardware/Potentiometer.{h,cpp}`.

**Modified:** `src/ui/Types.h`, `src/ui/Event.h`, `src/ui/state/UIState.h`, `src/ui/state/StateManager.{h,cpp}`, `src/ui/state/Reducer.{h,cpp}`, `src/ui/views/InitView.cpp`, `src/ui/UIController.{h,cpp}`, `src/ui/ui.{h,cpp}`, `src/genseq.cpp`, `src/ui/hardware/HardwareConfig.h`, `src/config/pins.h`, `CMakeLists.txt`.

**Task ordering rationale:** Task 1 is purely additive, so the firmware keeps building. Task 2 swaps the event vocabulary — this is necessarily atomic, because deleting `ButtonId`/`POT_CHANGED` breaks `Button.cpp`, `Potentiometer.cpp` and `InitView.cpp` simultaneously. Tasks 3 and 4 add logic behind tests without touching the firmware's behavior. Task 5 wires the hardware in.

---

### Task 1: Host test harness, key types, and key names

Purely additive. `ButtonId`/`PotId` stay for now so the firmware keeps compiling.

**Files:**
- Create: `tests/CMakeLists.txt`, `tests/framework.h`, `tests/main.cpp`, `tests/test_types.cpp`
- Create: `src/ui/KeyNames.h`, `src/ui/KeyNames.cpp`
- Modify: `src/ui/Types.h` (append `KeyId`, `mod`, `combo`)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `enum class ui::KeyId : uint8_t` — values are HID usage codes
  - `namespace ui::mod { constexpr uint8_t NONE, CTRL, SHIFT, ALT, GUI; }`
  - `constexpr uint16_t ui::combo(KeyId id, uint8_t mods = mod::NONE)`
  - `const char* ui::toName(KeyId id)` (declared in `ui/KeyNames.h`)
  - Test macros `TEST(name)`, `CHECK(cond)`, `CHECK_EQ(a, b)`, `CHECK_STREQ(a, b)` from `tests/framework.h`

- [ ] **Step 1: Write the test harness**

Create `tests/framework.h`:

```cpp
#pragma once

#include <cstdio>
#include <cstring>
#include <vector>

namespace testing {

struct TestCase { const char* name; void (*fn)(); };

inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
inline int& failures() { static int f = 0; return f; }

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline int runAll() {
    int failedTests = 0;
    for (auto& tc : registry()) {
        const int before = failures();
        tc.fn();
        if (failures() > before) { ++failedTests; printf("FAIL %s\n", tc.name); }
        else                     { printf("ok   %s\n", tc.name); }
    }
    printf("\n%zu tests, %d failed\n", registry().size(), failedTests);
    return failedTests == 0 ? 0 : 1;
}

} // namespace testing

#define TEST(name)                                                   \
    static void name();                                              \
    static ::testing::Registrar reg_##name(#name, name);             \
    static void name()

#define CHECK(cond)                                                  \
    do { if (!(cond)) { ++::testing::failures();                     \
        printf("  %s:%d: CHECK failed: %s\n",                        \
               __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_EQ(a, b)                                               \
    do { const long long _a = (long long)(a);                        \
         const long long _b = (long long)(b);                        \
         if (_a != _b) { ++::testing::failures();                    \
        printf("  %s:%d: CHECK_EQ failed: %s != %s (%lld vs %lld)\n",\
               __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

#define CHECK_STREQ(a, b)                                            \
    do { const char* _a = (a); const char* _b = (b);                 \
         if (strcmp(_a, _b) != 0) { ++::testing::failures();         \
        printf("  %s:%d: CHECK_STREQ failed: \"%s\" != \"%s\"\n",    \
               __FILE__, __LINE__, _a, _b); } } while (0)
```

Create `tests/main.cpp`:

```cpp
#include "framework.h"

int main() { return testing::runAll(); }
```

- [ ] **Step 2: Write the failing test for key types**

Create `tests/test_types.cpp`:

```cpp
#include "framework.h"
#include "ui/Types.h"
#include "ui/KeyNames.h"

using namespace ui;

TEST(combo_packs_key_in_low_byte_and_mods_in_high_byte) {
    CHECK_EQ(combo(KeyId::UP), 0x0052);
    CHECK_EQ(combo(KeyId::UP, mod::SHIFT), 0x0252);
    CHECK_EQ(combo(KeyId::F1), 0x003A);
    CHECK_EQ(combo(KeyId::SPACE), 0x002C);
}

TEST(combo_distinguishes_modified_from_bare) {
    CHECK(combo(KeyId::UP) != combo(KeyId::UP, mod::SHIFT));
    CHECK(combo(KeyId::UP, mod::SHIFT) != combo(KeyId::UP, mod::CTRL));
}

TEST(combo_is_usable_as_a_switch_label) {
    switch (combo(KeyId::DOWN, mod::SHIFT)) {
        case combo(KeyId::DOWN):             CHECK(false); break;
        case combo(KeyId::DOWN, mod::SHIFT): CHECK(true);  break;
        default:                             CHECK(false); break;
    }
}

TEST(key_ids_are_hid_usage_codes) {
    CHECK_EQ(static_cast<uint8_t>(KeyId::A), 0x04);
    CHECK_EQ(static_cast<uint8_t>(KeyId::SPACE), 0x2C);
    CHECK_EQ(static_cast<uint8_t>(KeyId::F1), 0x3A);
    CHECK_EQ(static_cast<uint8_t>(KeyId::F12), 0x45);
    CHECK_EQ(static_cast<uint8_t>(KeyId::UP), 0x52);
}

TEST(function_keys_are_contiguous) {
    CHECK_EQ(static_cast<uint8_t>(KeyId::F12) - static_cast<uint8_t>(KeyId::F1), 11);
}

TEST(toName_returns_lowercase_names) {
    CHECK_STREQ(toName(KeyId::A), "a");
    CHECK_STREQ(toName(KeyId::SPACE), "space");
    CHECK_STREQ(toName(KeyId::F1), "f1");
    CHECK_STREQ(toName(KeyId::F12), "f12");
    CHECK_STREQ(toName(KeyId::UP), "up");
    CHECK_STREQ(toName(KeyId::NUM_0), "0");
}

TEST(toName_returns_question_mark_for_unnamed_keys) {
    CHECK_STREQ(toName(static_cast<KeyId>(0xB0)), "?");
    CHECK_STREQ(toName(KeyId::NONE), "?");
}
```

Create `tests/CMakeLists.txt`:

```cmake
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
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake -S tests -B tests/build && cmake --build tests/build -j`
Expected: FAIL at compile time — `ui/KeyNames.h` does not exist, and `KeyId`, `mod`, `combo` are not declared in `ui/Types.h`.

- [ ] **Step 4: Add the key types**

Append to `src/ui/Types.h`, inside `namespace ui`, after the existing `PotId`/`POT_COUNT` declarations (leave those in place — Task 2 removes them). Add `#include <cstdint>` at the top of the file if it is not already there.

```cpp
// Values ARE HID usage codes - the decoder casts straight from the report.
enum class KeyId : uint8_t {
    NONE = 0x00,

    A = 0x04, B = 0x05, C = 0x06, D = 0x07, E = 0x08, F = 0x09,
    G = 0x0A, H = 0x0B, I = 0x0C, J = 0x0D, K = 0x0E, L = 0x0F,
    M = 0x10, N = 0x11, O = 0x12, P = 0x13, Q = 0x14, R = 0x15,
    S = 0x16, T = 0x17, U = 0x18, V = 0x19, W = 0x1A, X = 0x1B,
    Y = 0x1C, Z = 0x1D,

    NUM_1 = 0x1E, NUM_2 = 0x1F, NUM_3 = 0x20, NUM_4 = 0x21, NUM_5 = 0x22,
    NUM_6 = 0x23, NUM_7 = 0x24, NUM_8 = 0x25, NUM_9 = 0x26, NUM_0 = 0x27,

    ENTER = 0x28, ESCAPE = 0x29, BACKSPACE = 0x2A, TAB = 0x2B,
    SPACE = 0x2C, MINUS = 0x2D, EQUAL = 0x2E,

    F1 = 0x3A, F2  = 0x3B, F3  = 0x3C, F4  = 0x3D, F5  = 0x3E, F6  = 0x3F,
    F7 = 0x40, F8  = 0x41, F9  = 0x42, F10 = 0x43, F11 = 0x44, F12 = 0x45,

    RIGHT = 0x4F, LEFT = 0x50, DOWN = 0x51, UP = 0x52,
};

// Mirrors the low nibble of the HID modifier byte. Right-hand modifiers
// occupy the high nibble and are folded onto these same bits by the decoder.
namespace mod {
    constexpr uint8_t NONE  = 0;
    constexpr uint8_t CTRL  = 1 << 0;
    constexpr uint8_t SHIFT = 1 << 1;
    constexpr uint8_t ALT   = 1 << 2;
    constexpr uint8_t GUI   = 1 << 3;
}

// Packs a key and its modifiers into one value usable as a switch label.
constexpr uint16_t combo(KeyId id, uint8_t mods = mod::NONE) {
    return static_cast<uint16_t>((static_cast<uint16_t>(mods) << 8) |
                                 static_cast<uint8_t>(id));
}
```

Create `src/ui/KeyNames.h`:

```cpp
#pragma once

#include "Types.h"

namespace ui {

// Human-readable name for tracing and labels. Returns "?" for unnamed keys.
// Not used by the reducer, which dispatches on combo() values.
const char* toName(KeyId id);

} // namespace ui
```

Create `src/ui/KeyNames.cpp`:

```cpp
#include "KeyNames.h"

namespace ui {
namespace {

struct Entry { KeyId id; const char* name; };

constexpr Entry NAMES[] = {
    {KeyId::A, "a"}, {KeyId::B, "b"}, {KeyId::C, "c"}, {KeyId::D, "d"},
    {KeyId::E, "e"}, {KeyId::F, "f"}, {KeyId::G, "g"}, {KeyId::H, "h"},
    {KeyId::I, "i"}, {KeyId::J, "j"}, {KeyId::K, "k"}, {KeyId::L, "l"},
    {KeyId::M, "m"}, {KeyId::N, "n"}, {KeyId::O, "o"}, {KeyId::P, "p"},
    {KeyId::Q, "q"}, {KeyId::R, "r"}, {KeyId::S, "s"}, {KeyId::T, "t"},
    {KeyId::U, "u"}, {KeyId::V, "v"}, {KeyId::W, "w"}, {KeyId::X, "x"},
    {KeyId::Y, "y"}, {KeyId::Z, "z"},

    {KeyId::NUM_1, "1"}, {KeyId::NUM_2, "2"}, {KeyId::NUM_3, "3"},
    {KeyId::NUM_4, "4"}, {KeyId::NUM_5, "5"}, {KeyId::NUM_6, "6"},
    {KeyId::NUM_7, "7"}, {KeyId::NUM_8, "8"}, {KeyId::NUM_9, "9"},
    {KeyId::NUM_0, "0"},

    {KeyId::ENTER, "enter"}, {KeyId::ESCAPE, "escape"},
    {KeyId::BACKSPACE, "backspace"}, {KeyId::TAB, "tab"},
    {KeyId::SPACE, "space"}, {KeyId::MINUS, "minus"}, {KeyId::EQUAL, "equal"},

    {KeyId::F1, "f1"},   {KeyId::F2, "f2"},   {KeyId::F3, "f3"},
    {KeyId::F4, "f4"},   {KeyId::F5, "f5"},   {KeyId::F6, "f6"},
    {KeyId::F7, "f7"},   {KeyId::F8, "f8"},   {KeyId::F9, "f9"},
    {KeyId::F10, "f10"}, {KeyId::F11, "f11"}, {KeyId::F12, "f12"},

    {KeyId::RIGHT, "right"}, {KeyId::LEFT, "left"},
    {KeyId::DOWN, "down"},   {KeyId::UP, "up"},
};

} // namespace

const char* toName(KeyId id)
{
    for (const auto& entry : NAMES) {
        if (entry.id == id) return entry.name;
    }
    return "?";
}

} // namespace ui
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake -S tests -B tests/build && cmake --build tests/build -j && ./tests/build/genseq_tests`
Expected: PASS — `7 tests, 0 failed`.

- [ ] **Step 6: Verify the firmware still builds**

Run: `cmake --build build -j`
Expected: succeeds. This task is purely additive; `Button.cpp` and `Potentiometer.cpp` are untouched.

- [ ] **Step 7: Commit**

```bash
git add tests src/ui/Types.h src/ui/KeyNames.h src/ui/KeyNames.cpp
git commit -m "test(ui): add host test harness, KeyId types and key names"
```

---

### Task 2: Swap the event vocabulary and remove the old input wiring

Necessarily atomic: deleting `ButtonId`/`POT_CHANGED` breaks `Button.cpp`, `Potentiometer.cpp` and `InitView.cpp` at the same time. After this task the firmware builds and has no input at all — that is expected and temporary.

**Files:**
- Delete: `src/ui/hardware/Button.h`, `src/ui/hardware/Button.cpp`, `src/ui/hardware/Potentiometer.h`, `src/ui/hardware/Potentiometer.cpp`
- Modify: `src/ui/Types.h`, `src/ui/Event.h`, `src/ui/state/UIState.h`, `src/ui/state/StateManager.h`, `src/ui/state/StateManager.cpp`, `src/ui/state/Reducer.h`, `src/ui/state/Reducer.cpp`, `src/ui/views/InitView.cpp`, `src/ui/UIController.h`, `src/ui/UIController.cpp`, `src/ui/ui.h`, `src/ui/ui.cpp`, `src/genseq.cpp`, `src/ui/hardware/HardwareConfig.h`, `src/config/pins.h`
- Create: `tests/stubs/pico/multicore.h`, `tests/stubs/command_stub.h`, `tests/stubs/command_stub.cpp`, `tests/stubs/hardware_stub.cpp`, `tests/test_initview.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `KeyId`, `mod`, `combo` from Task 1.
- Produces:
  - `enum class ui::events::EventType : uint8_t { KEY_PRESSED, KEY_RELEASED, KEY_HELD }`
  - `ui::events::Event` with `data.key.id` (`KeyId`) and `data.key.mods` (`uint8_t`)
  - `static Event Event::keyPressed(KeyId, uint8_t mods, uint32_t ts)` and `keyReleased` / `keyHeld` with identical parameter lists
  - `ui::state::ViewId::COUNT` and `constexpr size_t ui::state::VIEW_COUNT`
  - `void ui::state::setValue(UIState&, int)` — declared in `Reducer.h`, clamps to 0–99
  - `commands::testing::sentCommands()` and `commands::testing::reset()` from `tests/stubs/command_stub.cpp`

- [ ] **Step 1: Write the failing test for InitView bindings**

Create `tests/stubs/hardware_stub.cpp`:

```cpp
// No-op Led / LedMatrix bodies. Led.h and LedMatrix.h are Pico-free, so views
// that hold references to them compile and link on the host against these.
#include "ui/hardware/Led.h"
#include "ui/hardware/LedMatrix.h"

namespace hardware {

Led::Led(uint8_t pin)
    : pin(pin), state(false), blinking(false),
      onTime(0), offTime(0), lastToggleTime(0) {}
void Led::update() {}
void Led::on() {}
void Led::off() {}
void Led::toggle() {}
void Led::blink(uint32_t, uint32_t) {}

LedMatrix::LedMatrix(uint8_t pin) : pin(pin), buffer{}, dirty(false) {}
void LedMatrix::update() {}
void LedMatrix::clear() {}
void LedMatrix::setPixel(uint8_t, uint8_t, uint32_t) {}
void LedMatrix::fill(uint32_t) {}
void LedMatrix::drawNumber(int, uint32_t) {}
void LedMatrix::drawLabel(const char (&)[4], uint32_t) {}
void LedMatrix::drawNote(const char (&)[3], uint32_t) {}

} // namespace hardware
```

Create `tests/stubs/pico/multicore.h`. It stands in for the SDK header that `commands/command.h` includes. It must pull in `<cstdint>`: `command.h` uses `uint8_t` but includes nothing else, relying on the real SDK header to supply it transitively.

```cpp
#pragma once

// Host-test stub for the Pico SDK header. commands/command.h relies on it
// to supply the fixed-width integer types.
#include <cstdint>
```

Create `tests/stubs/command_stub.cpp`:

```cpp
#include "commands/command.h"
#include "command_stub.h"

namespace commands {

namespace testing {
std::vector<CommandMessage>& sentCommands() {
    static std::vector<CommandMessage> v;
    return v;
}
void reset() { sentCommands().clear(); }
} // namespace testing

void sendCommand(Command cmd, uint8_t param1, uint8_t param2) {
    testing::sentCommands().push_back({cmd, param1, param2});
}

CommandMessage receiveCommand() { return {Command::NOOP, 0, 0}; }

} // namespace commands
```

Create `tests/stubs/command_stub.h`:

```cpp
#pragma once

#include <vector>
#include "commands/command.h"

namespace commands::testing {

std::vector<CommandMessage>& sentCommands();
void reset();

} // namespace commands::testing
```

Create `tests/test_initview.cpp`:

```cpp
#include "framework.h"
#include "ui/views/InitView.h"
#include "ui/state/UIState.h"
#include "ui/Event.h"

using namespace ui;

namespace {

struct Fixture {
    hardware::Led led{0};
    hardware::LedMatrix matrix{0};
    InitView view{led, matrix};
};

state::UIState stateWithValue(int value) {
    state::UIState s;
    s.value = value;
    return s;
}

events::Event press(KeyId id, uint8_t mods = mod::NONE) {
    return events::Event::keyPressed(id, mods, 0);
}

} // namespace

TEST(initview_up_increments_by_one) {
    Fixture f;
    const auto next = f.view.handleEvent(stateWithValue(5), press(KeyId::UP));
    CHECK_EQ(next.value, 6);
}

TEST(initview_down_decrements_by_one) {
    Fixture f;
    const auto next = f.view.handleEvent(stateWithValue(5), press(KeyId::DOWN));
    CHECK_EQ(next.value, 4);
}

TEST(initview_shift_makes_steps_coarse) {
    Fixture f;
    CHECK_EQ(f.view.handleEvent(stateWithValue(50), press(KeyId::UP, mod::SHIFT)).value, 60);
    CHECK_EQ(f.view.handleEvent(stateWithValue(50), press(KeyId::DOWN, mod::SHIFT)).value, 40);
}

TEST(initview_treats_key_held_like_key_pressed) {
    Fixture f;
    const auto held = events::Event::keyHeld(KeyId::UP, mod::NONE, 0);
    CHECK_EQ(f.view.handleEvent(stateWithValue(5), held).value, 6);
}

TEST(initview_ignores_key_released) {
    Fixture f;
    const auto released = events::Event::keyReleased(KeyId::UP, mod::NONE, 0);
    CHECK_EQ(f.view.handleEvent(stateWithValue(5), released).value, 5);
}

TEST(initview_clamps_at_both_ends) {
    Fixture f;
    CHECK_EQ(f.view.handleEvent(stateWithValue(95), press(KeyId::UP, mod::SHIFT)).value, 99);
    CHECK_EQ(f.view.handleEvent(stateWithValue(99), press(KeyId::UP)).value, 99);
    CHECK_EQ(f.view.handleEvent(stateWithValue(5),  press(KeyId::DOWN, mod::SHIFT)).value, 0);
    CHECK_EQ(f.view.handleEvent(stateWithValue(0),  press(KeyId::DOWN)).value, 0);
}

TEST(initview_ignores_unbound_keys) {
    Fixture f;
    CHECK_EQ(f.view.handleEvent(stateWithValue(5), press(KeyId::LEFT)).value, 5);
    CHECK_EQ(f.view.handleEvent(stateWithValue(5), press(KeyId::A)).value, 5);
}
```

Extend `tests/CMakeLists.txt` — replace the `add_executable` block with:

```cmake
add_executable(genseq_tests
    main.cpp
    test_types.cpp
    test_initview.cpp
    stubs/command_stub.cpp
    stubs/hardware_stub.cpp
    ${SRC}/ui/KeyNames.cpp
    ${SRC}/ui/state/Reducer.cpp
    ${SRC}/ui/views/InitView.cpp
)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake -S tests -B tests/build && cmake --build tests/build -j`
Expected: FAIL at compile time — `Event::keyPressed` does not exist, `InitView::handleEvent` still switches on `ButtonId`, and `state::setValue` is not declared in `Reducer.h`.

- [ ] **Step 3: Rewrite the event vocabulary**

Replace the whole of `src/ui/Event.h` with:

```cpp
#pragma once

#include <cstdint>
#include "Types.h"

namespace ui::events {

enum class EventType : uint8_t {
    KEY_PRESSED,
    KEY_RELEASED,
    KEY_HELD      // auto-repeat while the key stays down
};

struct Event {
    EventType type;
    uint32_t timestamp;

    union {
        struct {
            KeyId id;
            uint8_t mods;   // folded modifier bitmask, see ui::mod
        } key;
    } data;

    Event() : type(EventType::KEY_PRESSED), timestamp(0) {
        data.key.id = KeyId::NONE;
        data.key.mods = mod::NONE;
    }

    static Event keyPressed(KeyId id, uint8_t mods, uint32_t timestamp) {
        return make(EventType::KEY_PRESSED, id, mods, timestamp);
    }

    static Event keyReleased(KeyId id, uint8_t mods, uint32_t timestamp) {
        return make(EventType::KEY_RELEASED, id, mods, timestamp);
    }

    static Event keyHeld(KeyId id, uint8_t mods, uint32_t timestamp) {
        return make(EventType::KEY_HELD, id, mods, timestamp);
    }

private:
    static Event make(EventType type, KeyId id, uint8_t mods, uint32_t timestamp) {
        Event e;
        e.type = type;
        e.timestamp = timestamp;
        e.data.key.id = id;
        e.data.key.mods = mods;
        return e;
    }
};

} // namespace ui::events
```

In `src/ui/Types.h`, delete `enum class ButtonId {...}`, `BUTTON_COUNT`, `enum class PotId {...}` and `POT_COUNT`. Keep `KeyId`, `mod` and `combo`.

- [ ] **Step 4: Delete the old input hardware**

```bash
git rm src/ui/hardware/Button.h src/ui/hardware/Button.cpp \
       src/ui/hardware/Potentiometer.h src/ui/hardware/Potentiometer.cpp
```

- [ ] **Step 5: Add ViewId::COUNT and size the view arrays by it**

In `src/ui/state/UIState.h`, add `#include <cstddef>` and replace the `ViewId` enum with:

```cpp
enum class ViewId : uint8_t {
    INIT,
    SETTINGS,
    COUNT       // sentinel - keep last
};

constexpr size_t VIEW_COUNT = static_cast<size_t>(ViewId::COUNT);
```

In `src/ui/state/StateManager.h`, change the member and setter:

```cpp
    void setViews(std::array<IView*, VIEW_COUNT> views) { this->views = views; }
...
    std::array<IView*, VIEW_COUNT> views;
```

In `src/ui/state/StateManager.cpp`, replace the constructor:

```cpp
StateManager::StateManager() { views.fill(nullptr); }
```

In `src/ui/UIController.h`, replace the `views` member declaration:

```cpp
    std::array<IView*, state::VIEW_COUNT> views;
```

- [ ] **Step 6: Declare setValue and add the clamp**

In `src/ui/state/Reducer.h`, add to the state-setter block:

```cpp
void setValue(UIState& state, int value);
```

In `src/ui/state/Reducer.cpp`, replace the existing `setValue` body with the clamped version:

```cpp
void setValue(UIState& state, int value) {
    state.value = std::max(0, std::min(99, value));
}
```

`<algorithm>` is already included in that file.

- [ ] **Step 7: Rewrite InitView::handleEvent**

In `src/ui/views/InitView.cpp`, replace the whole `handleEvent` body with:

```cpp
state::UIState InitView::handleEvent(const state::UIState& state, const events::Event& event)
{
    if (event.type != events::EventType::KEY_PRESSED &&
        event.type != events::EventType::KEY_HELD) {
        return state;
    }

    state::UIState newState = state;

    switch (combo(event.data.key.id, event.data.key.mods)) {
        case combo(KeyId::UP):               state::setValue(newState, state.value + 1);  break;
        case combo(KeyId::UP,   mod::SHIFT): state::setValue(newState, state.value + 10); break;
        case combo(KeyId::DOWN):             state::setValue(newState, state.value - 1);  break;
        case combo(KeyId::DOWN, mod::SHIFT): state::setValue(newState, state.value - 10); break;
        default: break;
    }

    return newState;
}
```

- [ ] **Step 8: Drop the button and pot wiring from the config chain**

In `src/ui/hardware/HardwareConfig.h`, remove the `buttonPins` and `potPin` fields, leaving:

```cpp
struct HardwareConfig {
    uint8_t ledPin;
    uint8_t ledMatrixPin;
};
```

In `src/config/pins.h`, delete the `BUTTON_A_PIN`–`BUTTON_F_PIN` defines (and the `// BUTTONS (A-F)` comment) and the `POT_PIN` define (and its `// POTENTIOMETER (ADC0)` comment).

In `src/ui/UIController.h`, remove the `#include "hardware/Button.h"` and `#include "hardware/Potentiometer.h"` lines and the `buttons` and `pot` members.

In `src/ui/UIController.cpp`, delete the button-construction loop and the `pot = ...` line from `initialize()`, and reduce `update()` to:

```cpp
void UIController::update()
{
    led->update();
    ledMatrix->update();
}
```

In `src/ui/ui.h` and `src/ui/ui.cpp`, change both `UI`'s constructor and `createUITask` to take only `(uint8_t ledPin, uint8_t ledMatrixPin)`, and reduce the config initializer to `config{ledPin, ledMatrixPin}`.

In `src/genseq.cpp`, delete the `buttonPins` array and call:

```cpp
    ui::createUITask(LED_PIN, LED_MATRIX_PIN);
```

- [ ] **Step 9: Run the tests to verify they pass**

Run: `cmake -S tests -B tests/build && cmake --build tests/build -j && ./tests/build/genseq_tests`
Expected: PASS — `14 tests, 0 failed`.

- [ ] **Step 10: Verify the firmware still builds**

Run: `cmake --build build -j`
Expected: succeeds. The firmware now has no input source; that is wired up in Task 5.

- [ ] **Step 11: Commit**

```bash
git add -A
git commit -m "refactor(ui): replace button/pot events with key events

Deletes Button and Potentiometer, swaps EventType to KEY_PRESSED/
KEY_RELEASED/KEY_HELD carrying KeyId plus modifiers, rewrites InitView
to an up/down sweep with shift for coarse steps, sizes the view arrays
by ViewId::COUNT, and clamps value 0-99 inside setValue.

The firmware has no input source until the USB keyboard is wired in."
```

---

### Task 3: Global bindings in the reducer

**Files:**
- Modify: `src/ui/state/Reducer.cpp`
- Create: `tests/test_reducer.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Event` factories, `setValue`, `VIEW_COUNT` from Task 2; `combo`, `KeyId`, `mod` from Task 1.
- Produces: `reduce()` consuming `F1`–`F12` and `space` before delegating. No new public symbols — `isReserved`, `applyGlobal` and `FUNCTION_KEY_VIEWS` are file-local.

- [ ] **Step 1: Write the failing test**

Create `tests/test_reducer.cpp`:

```cpp
#include "framework.h"
#include "stubs/command_stub.h"
#include "ui/state/Reducer.h"
#include "ui/state/UIState.h"
#include "ui/views/IView.h"
#include "ui/Event.h"

using namespace ui;

namespace {

// Records what reached the view and marks the state so delegation is visible.
class FakeView : public IView {
public:
    int eventsSeen = 0;
    KeyId lastKey = KeyId::NONE;

    state::UIState handleEvent(const state::UIState& state, const events::Event& event) override {
        ++eventsSeen;
        lastKey = event.data.key.id;
        state::UIState next = state;
        next.value = 42;            // sentinel: the view ran
        return next;
    }

    void render(const state::UIState&) override {}
};

events::Event press(KeyId id, uint8_t mods = mod::NONE) {
    return events::Event::keyPressed(id, mods, 0);
}

} // namespace

TEST(reduce_f1_switches_to_init_view) {
    FakeView view;
    state::UIState s;
    s.currentView = state::ViewId::SETTINGS;
    const auto next = state::reduce(s, press(KeyId::F1), &view);
    CHECK(next.currentView == state::ViewId::INIT);
    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_f2_switches_to_settings_view) {
    FakeView view;
    const auto next = state::reduce(state::UIState{}, press(KeyId::F2), &view);
    CHECK(next.currentView == state::ViewId::SETTINGS);
    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_consumes_unbound_function_keys_without_effect) {
    FakeView view;
    state::UIState s;
    s.currentView = state::ViewId::SETTINGS;
    for (uint8_t usage = static_cast<uint8_t>(KeyId::F3);
         usage <= static_cast<uint8_t>(KeyId::F12); ++usage) {
        const auto next = state::reduce(s, press(static_cast<KeyId>(usage)), &view);
        CHECK(next.currentView == state::ViewId::SETTINGS);
    }
    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_reserves_function_keys_regardless_of_modifiers) {
    FakeView view;
    state::UIState s;
    s.currentView = state::ViewId::SETTINGS;
    const auto next = state::reduce(s, press(KeyId::F1, mod::SHIFT), &view);
    CHECK(next.currentView == state::ViewId::SETTINGS);   // modified: consumed, no effect
    CHECK_EQ(view.eventsSeen, 0);                          // and never reaches the view
}

TEST(reduce_space_toggles_playing_and_sends_commands) {
    FakeView view;
    commands::testing::reset();

    state::UIState stopped;
    stopped.playing = false;
    const auto started = state::reduce(stopped, press(KeyId::SPACE), &view);
    CHECK(started.playing);

    const auto stoppedAgain = state::reduce(started, press(KeyId::SPACE), &view);
    CHECK(!stoppedAgain.playing);

    CHECK_EQ(commands::testing::sentCommands().size(), 2);
    CHECK(commands::testing::sentCommands()[0].cmd == commands::Command::PLAY);
    CHECK(commands::testing::sentCommands()[1].cmd == commands::Command::STOP);
    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_absorbs_release_and_repeat_of_reserved_keys) {
    FakeView view;
    state::UIState s;
    s.playing = false;

    const auto afterRelease = state::reduce(s, events::Event::keyReleased(KeyId::SPACE, mod::NONE, 0), &view);
    CHECK(!afterRelease.playing);

    const auto afterHeld = state::reduce(s, events::Event::keyHeld(KeyId::SPACE, mod::NONE, 0), &view);
    CHECK(!afterHeld.playing);

    CHECK_EQ(view.eventsSeen, 0);
}

TEST(reduce_delegates_unreserved_keys_to_the_active_view) {
    FakeView view;
    const auto next = state::reduce(state::UIState{}, press(KeyId::UP), &view);
    CHECK_EQ(next.value, 42);
    CHECK_EQ(view.eventsSeen, 1);
    CHECK(view.lastKey == KeyId::UP);
}

TEST(reduce_returns_state_unchanged_without_an_active_view) {
    state::UIState s;
    s.value = 7;
    const auto next = state::reduce(s, press(KeyId::UP), nullptr);
    CHECK_EQ(next.value, 7);
}
```

Add `test_reducer.cpp` to the `add_executable` list in `tests/CMakeLists.txt`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake -S tests -B tests/build && cmake --build tests/build -j && ./tests/build/genseq_tests`
Expected: FAIL — `reduce_f1_switches_to_init_view`, `reduce_f2_switches_to_settings_view`, `reduce_space_toggles_playing_and_sends_commands` and the reservation tests all fail, because `reduce()` currently forwards everything to the view.

- [ ] **Step 3: Add the global layer**

In `src/ui/state/Reducer.cpp`, add an anonymous namespace above `reduce` (keep the existing setter functions where they are):

```cpp
namespace {

constexpr uint8_t F1_USAGE  = static_cast<uint8_t>(KeyId::F1);
constexpr uint8_t F12_USAGE = static_cast<uint8_t>(KeyId::F12);

// F1 -> INIT, F2 -> SETTINGS. Keys past the end of this table are reserved
// for future views: consumed, traced, but without effect.
constexpr ViewId FUNCTION_KEY_VIEWS[] = { ViewId::INIT, ViewId::SETTINGS };

bool isFunctionKey(KeyId id) {
    const uint8_t usage = static_cast<uint8_t>(id);
    return usage >= F1_USAGE && usage <= F12_USAGE;
}

// Reserved by key, independent of modifiers, so shift+F1 is swallowed too
// and modified function keys stay available for future global bindings.
bool isReserved(KeyId id) {
    return id == KeyId::SPACE || isFunctionKey(id);
}

void applyGlobal(UIState& state, KeyId id) {
    if (id == KeyId::SPACE) {
        setPlaying(state, !state.playing);
        return;
    }

    const size_t index = static_cast<size_t>(static_cast<uint8_t>(id) - F1_USAGE);
    if (index < (sizeof(FUNCTION_KEY_VIEWS) / sizeof(FUNCTION_KEY_VIEWS[0]))) {
        setCurrentView(state, FUNCTION_KEY_VIEWS[index]);
    } else {
        printf("F%u pressed - no view bound\n", static_cast<unsigned>(index + 1));
    }
}

} // namespace
```

Then replace `reduce`:

```cpp
UIState reduce(const UIState& state, const events::Event& event, ui::IView* activeView) {
    if (isReserved(event.data.key.id)) {
        UIState next = state;
        if (event.type == events::EventType::KEY_PRESSED &&
            event.data.key.mods == mod::NONE) {
            applyGlobal(next, event.data.key.id);
        }
        return next;    // consumed either way - never reaches a view
    }

    if (activeView) {
        return activeView->handleEvent(state, event);
    }
    return state;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake -S tests -B tests/build && cmake --build tests/build -j && ./tests/build/genseq_tests`
Expected: PASS — `22 tests, 0 failed`.

- [ ] **Step 5: Verify the firmware still builds**

Run: `cmake --build build -j`
Expected: succeeds.

- [ ] **Step 6: Commit**

```bash
git add src/ui/state/Reducer.cpp tests
git commit -m "feat(ui/state): reserve F1-F12 and space as global bindings

reduce() now consumes reserved keys ahead of view delegation: F1 and F2
select views, space toggles transport, F3-F12 are reserved for future
views. Reservation is by key regardless of modifiers, so shift+F1 cannot
leak to a view."
```

---

### Task 4: The keyboard decoder

**Files:**
- Create: `src/ui/hardware/KeyboardDecoder.h`, `src/ui/hardware/KeyboardDecoder.cpp`
- Create: `tests/test_decoder.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Event` factories, `KeyId`, `mod` from Tasks 1–2.
- Produces:
  - `struct hardware::KeyReport { uint8_t mods; uint8_t keys[6]; }`
  - `class hardware::KeyboardDecoder` with `explicit KeyboardDecoder(EventSink)`, `void onReport(const KeyReport&, uint32_t nowMs)`, `void tick(uint32_t nowMs)`, `void onDisconnect()`
  - `using EventSink = std::function<void(const ui::events::Event&)>`
  - `KeyboardDecoder::REPEAT_DELAY_MS` (400) and `REPEAT_INTERVAL_MS` (30)

- [ ] **Step 1: Write the failing test**

Create `tests/test_decoder.cpp`:

```cpp
#include "framework.h"
#include "ui/hardware/KeyboardDecoder.h"

#include <vector>

using namespace ui;
using hardware::KeyboardDecoder;
using hardware::KeyReport;

namespace {

struct Recorder {
    std::vector<events::Event> events;

    KeyboardDecoder::EventSink sink() {
        return [this](const events::Event& e) { events.push_back(e); };
    }

    void clear() { events.clear(); }
    size_t size() const { return events.size(); }
};

// Builds a boot-protocol report from up to six usage codes.
KeyReport report(uint8_t mods, std::initializer_list<uint8_t> keys) {
    KeyReport r{};
    r.mods = mods;
    uint8_t i = 0;
    for (uint8_t k : keys) { if (i < 6) r.keys[i++] = k; }
    return r;
}

constexpr uint8_t UP_USAGE   = static_cast<uint8_t>(KeyId::UP);
constexpr uint8_t DOWN_USAGE = static_cast<uint8_t>(KeyId::DOWN);
constexpr uint8_t A_USAGE    = static_cast<uint8_t>(KeyId::A);

} // namespace

TEST(decoder_emits_press_for_a_newly_down_key) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);

    CHECK_EQ(rec.size(), 1);
    CHECK(rec.events[0].type == events::EventType::KEY_PRESSED);
    CHECK(rec.events[0].data.key.id == KeyId::UP);
    CHECK_EQ(rec.events[0].data.key.mods, mod::NONE);
    CHECK_EQ(rec.events[0].timestamp, 1000);
}

TEST(decoder_emits_release_when_a_key_disappears) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    rec.clear();
    d.onReport(report(0, {}), 1050);

    CHECK_EQ(rec.size(), 1);
    CHECK(rec.events[0].type == events::EventType::KEY_RELEASED);
    CHECK(rec.events[0].data.key.id == KeyId::UP);
}

TEST(decoder_does_not_re_emit_a_key_that_stays_down) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    rec.clear();
    d.onReport(report(0, {UP_USAGE}), 1010);

    CHECK_EQ(rec.size(), 0);
}

TEST(decoder_ignores_slot_reshuffles) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE, A_USAGE}), 1000);
    rec.clear();
    d.onReport(report(0, {A_USAGE, UP_USAGE}), 1010);   // same set, different slots

    CHECK_EQ(rec.size(), 0);
}

TEST(decoder_carries_modifiers_on_the_event) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(mod::SHIFT, {UP_USAGE}), 1000);

    CHECK_EQ(rec.size(), 1);
    CHECK_EQ(rec.events[0].data.key.mods, mod::SHIFT);
}

TEST(decoder_folds_right_hand_modifiers_onto_left) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0x20, {UP_USAGE}), 1000);   // right shift = bit 5

    CHECK_EQ(rec.size(), 1);
    CHECK_EQ(rec.events[0].data.key.mods, mod::SHIFT);
}

TEST(decoder_emits_nothing_for_a_modifier_only_change) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(mod::SHIFT, {}), 1000);

    CHECK_EQ(rec.size(), 0);
}

TEST(decoder_discards_roll_over_reports) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    rec.clear();
    d.onReport(report(0, {0x01, 0x01, 0x01, 0x01, 0x01, 0x01}), 1010);

    CHECK_EQ(rec.size(), 0);   // no phantom presses, no spurious release of UP
}

TEST(decoder_tracks_a_second_key_pressed_and_released_independently) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    rec.clear();
    d.onReport(report(0, {UP_USAGE, A_USAGE}), 1010);
    CHECK_EQ(rec.size(), 1);
    CHECK(rec.events[0].type == events::EventType::KEY_PRESSED);
    CHECK(rec.events[0].data.key.id == KeyId::A);

    rec.clear();
    d.onReport(report(0, {UP_USAGE}), 1020);
    CHECK_EQ(rec.size(), 1);
    CHECK(rec.events[0].type == events::EventType::KEY_RELEASED);
    CHECK(rec.events[0].data.key.id == KeyId::A);
}

TEST(decoder_repeats_after_the_delay_then_at_the_interval) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    rec.clear();

    d.tick(1000 + KeyboardDecoder::REPEAT_DELAY_MS - 1);
    CHECK_EQ(rec.size(), 0);

    d.tick(1000 + KeyboardDecoder::REPEAT_DELAY_MS);
    CHECK_EQ(rec.size(), 1);
    CHECK(rec.events[0].type == events::EventType::KEY_HELD);
    CHECK(rec.events[0].data.key.id == KeyId::UP);

    const uint32_t afterFirst = 1000 + KeyboardDecoder::REPEAT_DELAY_MS;
    d.tick(afterFirst + KeyboardDecoder::REPEAT_INTERVAL_MS - 1);
    CHECK_EQ(rec.size(), 1);

    d.tick(afterFirst + KeyboardDecoder::REPEAT_INTERVAL_MS);
    CHECK_EQ(rec.size(), 2);
    CHECK(rec.events[1].type == events::EventType::KEY_HELD);
}

TEST(decoder_emits_one_repeat_for_a_late_tick_not_a_burst) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    rec.clear();

    d.tick(1000 + KeyboardDecoder::REPEAT_DELAY_MS + 5000);   // 5 seconds late

    CHECK_EQ(rec.size(), 1);
}

TEST(decoder_stops_repeating_on_release) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    d.onReport(report(0, {}), 1100);
    rec.clear();

    d.tick(1000 + KeyboardDecoder::REPEAT_DELAY_MS + 1000);

    CHECK_EQ(rec.size(), 0);
}

TEST(decoder_moves_repeat_to_the_most_recently_pressed_key) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    d.onReport(report(0, {UP_USAGE, DOWN_USAGE}), 1100);
    rec.clear();

    d.tick(1100 + KeyboardDecoder::REPEAT_DELAY_MS);

    CHECK_EQ(rec.size(), 1);
    CHECK(rec.events[0].data.key.id == KeyId::DOWN);
}

TEST(decoder_stops_repeating_when_the_target_is_released_even_if_others_are_down) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    d.onReport(report(0, {UP_USAGE, DOWN_USAGE}), 1100);
    d.onReport(report(0, {UP_USAGE}), 1200);       // release the repeat target
    rec.clear();

    d.tick(1200 + KeyboardDecoder::REPEAT_DELAY_MS + 1000);

    CHECK_EQ(rec.size(), 0);   // no fallback to the still-held UP
}

TEST(decoder_repeats_carry_live_modifiers) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    d.onReport(report(mod::SHIFT, {UP_USAGE}), 1100);   // shift pressed mid-hold
    rec.clear();

    d.tick(1000 + KeyboardDecoder::REPEAT_DELAY_MS);

    CHECK_EQ(rec.size(), 1);
    CHECK_EQ(rec.events[0].data.key.mods, mod::SHIFT);
}

TEST(decoder_repeat_survives_the_millisecond_rollover) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    const uint32_t nearMax = 0xFFFFFF00;
    d.onReport(report(0, {UP_USAGE}), nearMax);
    rec.clear();

    // nearMax + 400 wraps past 0xFFFFFFFF
    d.tick(static_cast<uint32_t>(nearMax + KeyboardDecoder::REPEAT_DELAY_MS));

    CHECK_EQ(rec.size(), 1);
    CHECK(rec.events[0].type == events::EventType::KEY_HELD);
}

TEST(decoder_releases_everything_on_disconnect) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE, A_USAGE}), 1000);
    rec.clear();

    d.onDisconnect();

    CHECK_EQ(rec.size(), 2);
    CHECK(rec.events[0].type == events::EventType::KEY_RELEASED);
    CHECK(rec.events[1].type == events::EventType::KEY_RELEASED);
}

TEST(decoder_stops_repeating_after_disconnect) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {UP_USAGE}), 1000);
    d.onDisconnect();
    rec.clear();

    d.tick(1000 + KeyboardDecoder::REPEAT_DELAY_MS + 1000);

    CHECK_EQ(rec.size(), 0);
}

TEST(decoder_passes_through_keys_that_have_no_named_enumerator) {
    Recorder rec;
    KeyboardDecoder d(rec.sink());

    d.onReport(report(0, {0x49}), 1000);   // Insert - not in KeyId

    CHECK_EQ(rec.size(), 1);
    CHECK_EQ(static_cast<uint8_t>(rec.events[0].data.key.id), 0x49);
}
```

Add `test_decoder.cpp` and `${SRC}/ui/hardware/KeyboardDecoder.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake -S tests -B tests/build && cmake --build tests/build -j`
Expected: FAIL at compile time — `ui/hardware/KeyboardDecoder.h` does not exist.

- [ ] **Step 3: Write the decoder header**

Create `src/ui/hardware/KeyboardDecoder.h`:

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include "../Event.h"

namespace hardware {

// The HID boot-protocol keyboard report, minus its reserved byte.
struct KeyReport {
    uint8_t mods;       // raw HID modifier byte
    uint8_t keys[6];    // usage codes currently down, 0 = empty slot
};

// Turns a stream of HID reports into key events. Pure logic: it never reads a
// clock, so the caller supplies nowMs and tests can inject time.
class KeyboardDecoder {
public:
    static constexpr uint32_t REPEAT_DELAY_MS = 400;
    static constexpr uint32_t REPEAT_INTERVAL_MS = 30;
    static constexpr uint8_t MAX_KEYS = 6;
    static constexpr uint8_t ERROR_ROLL_OVER = 0x01;

    using EventSink = std::function<void(const ui::events::Event&)>;

    explicit KeyboardDecoder(EventSink sink);

    // Diff against the previous report; emits KEY_PRESSED / KEY_RELEASED.
    void onReport(const KeyReport& report, uint32_t nowMs);

    // Emits at most one KEY_HELD when the repeat deadline has passed. Must be
    // called regularly: a held key produces no further HID traffic.
    void tick(uint32_t nowMs);

    // Releases everything still down and clears all state.
    void onDisconnect();

private:
    EventSink sink;
    uint8_t downKeys[MAX_KEYS];
    uint8_t mods;
    ui::KeyId repeatKey;        // KeyId::NONE when nothing is repeating
    uint32_t nextRepeatMs;
    uint32_t lastNowMs;

    bool isDown(uint8_t usage) const;
    static uint8_t foldMods(uint8_t raw);
};

} // namespace hardware
```

- [ ] **Step 4: Write the decoder implementation**

Create `src/ui/hardware/KeyboardDecoder.cpp`:

```cpp
#include "KeyboardDecoder.h"

namespace hardware {

KeyboardDecoder::KeyboardDecoder(EventSink sink)
    : sink(std::move(sink)),
      downKeys{},
      mods(ui::mod::NONE),
      repeatKey(ui::KeyId::NONE),
      nextRepeatMs(0),
      lastNowMs(0)
{}

// Right-hand modifiers live in the high nibble; fold them onto their
// left-hand twins so shift is shift wherever it came from.
uint8_t KeyboardDecoder::foldMods(uint8_t raw)
{
    return static_cast<uint8_t>((raw & 0x0F) | ((raw >> 4) & 0x0F));
}

bool KeyboardDecoder::isDown(uint8_t usage) const
{
    for (uint8_t i = 0; i < MAX_KEYS; ++i) {
        if (downKeys[i] == usage) return true;
    }
    return false;
}

void KeyboardDecoder::onReport(const KeyReport& report, uint32_t nowMs)
{
    // More keys down than the boot protocol can express: every slot reads
    // ErrorRollOver. Discard the whole report rather than read six phantoms.
    bool allRollOver = true;
    for (uint8_t i = 0; i < MAX_KEYS; ++i) {
        if (report.keys[i] != ERROR_ROLL_OVER) { allRollOver = false; break; }
    }
    if (allRollOver) return;

    lastNowMs = nowMs;
    mods = foldMods(report.mods);

    // Releases: down before, absent now.
    for (uint8_t i = 0; i < MAX_KEYS; ++i) {
        const uint8_t usage = downKeys[i];
        if (usage == 0) continue;

        bool stillDown = false;
        for (uint8_t j = 0; j < MAX_KEYS; ++j) {
            if (report.keys[j] == usage) { stillDown = true; break; }
        }
        if (stillDown) continue;

        const ui::KeyId id = static_cast<ui::KeyId>(usage);
        downKeys[i] = 0;
        if (id == repeatKey) repeatKey = ui::KeyId::NONE;
        sink(ui::events::Event::keyReleased(id, mods, nowMs));
    }

    // Presses: present now, not down before. The last one wins the repeat.
    for (uint8_t i = 0; i < MAX_KEYS; ++i) {
        const uint8_t usage = report.keys[i];
        if (usage == 0 || isDown(usage)) continue;

        for (uint8_t j = 0; j < MAX_KEYS; ++j) {
            if (downKeys[j] == 0) { downKeys[j] = usage; break; }
        }

        const ui::KeyId id = static_cast<ui::KeyId>(usage);
        repeatKey = id;
        nextRepeatMs = nowMs + REPEAT_DELAY_MS;
        sink(ui::events::Event::keyPressed(id, mods, nowMs));
    }
}

void KeyboardDecoder::tick(uint32_t nowMs)
{
    lastNowMs = nowMs;
    if (repeatKey == ui::KeyId::NONE) return;

    // Wrap-safe deadline test: survives the 32-bit millisecond rollover.
    if (static_cast<int32_t>(nowMs - nextRepeatMs) < 0) return;

    // Schedule forward from now rather than accumulating, so a late tick
    // delays the next repeat instead of discharging a burst.
    nextRepeatMs = nowMs + REPEAT_INTERVAL_MS;
    sink(ui::events::Event::keyHeld(repeatKey, mods, nowMs));
}

void KeyboardDecoder::onDisconnect()
{
    for (uint8_t i = 0; i < MAX_KEYS; ++i) {
        if (downKeys[i] == 0) continue;
        sink(ui::events::Event::keyReleased(
            static_cast<ui::KeyId>(downKeys[i]), mods, lastNowMs));
        downKeys[i] = 0;
    }

    mods = ui::mod::NONE;
    repeatKey = ui::KeyId::NONE;
    nextRepeatMs = 0;
}

} // namespace hardware
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake -S tests -B tests/build && cmake --build tests/build -j && ./tests/build/genseq_tests`
Expected: PASS — `41 tests, 0 failed`.

- [ ] **Step 6: Verify the firmware still builds**

Run: `cmake --build build -j`
Expected: succeeds. `KeyboardDecoder.cpp` is picked up by the existing `GLOB_RECURSE` and compiles for ARM as readily as for the host.

- [ ] **Step 7: Commit**

```bash
git add src/ui/hardware/KeyboardDecoder.h src/ui/hardware/KeyboardDecoder.cpp tests
git commit -m "feat(ui/hw): add KeyboardDecoder with HID report diffing and auto-repeat

Pure logic with an injected clock: set-based report diffing that tolerates
slot reshuffles, ErrorRollOver reports discarded whole, right-hand modifiers
folded onto left, last-pressed-key auto-repeat at 400ms/30ms scheduled
forward from now, wrap-safe deadlines, and disconnect releasing everything
still down."
```

---

### Task 5: TinyUSB host wiring

The only task whose deliverable needs hardware to verify.

**Files:**
- Create: `src/ui/hardware/UsbKeyboard.h`, `src/ui/hardware/UsbKeyboard.cpp`, `src/config/tusb_config.h`
- Modify: `CMakeLists.txt`, `src/ui/UIController.h`, `src/ui/UIController.cpp`

**Interfaces:**
- Consumes: `KeyboardDecoder`, `KeyReport` from Task 4; `state::getStateManager()` from the existing code.
- Produces: `class hardware::UsbKeyboard` with `UsbKeyboard()`, `void initialize()`, `void update()`, `static UsbKeyboard* instance()`, `void handleReport(const uint8_t*, uint16_t)`, `void handleDisconnect()`.

- [ ] **Step 1: Add the TinyUSB host configuration**

Create `src/config/tusb_config.h`:

```c
#pragma once

// CFG_TUSB_MCU, CFG_TUSB_OS and CFG_TUSB_DEBUG are supplied by the Pico SDK's
// tinyusb_common_base target. Defining them here causes redefinition errors.

#define CFG_TUH_ENABLED     1
#define CFG_TUH_RPI_PIO_USB 0   // native USB port, not Pico-PIO-USB

#define CFG_TUH_HUB         1   // keyboards are frequently behind a hub
#define CFG_TUH_HID         4   // HID interfaces, not devices
#define CFG_TUH_CDC         0
#define CFG_TUH_MSC         0
#define CFG_TUH_VENDOR      0

#define CFG_TUH_DEVICE_MAX  (CFG_TUH_HUB ? 5 : 4)
#define CFG_TUH_ENUMERATION_BUFSIZE 256
```

- [ ] **Step 2: Update the build**

In `CMakeLists.txt`, add the two TinyUSB libraries and remove `hardware_adc` (`Potentiometer.cpp` was its only user, and it is gone):

```cmake
target_link_libraries(genseq
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_uart
    hardware_gpio
    hardware_i2c
    hardware_dma
    tinyusb_host
    tinyusb_board
)
```

And add `src/config` to the include path so TinyUSB's unqualified `#include "tusb_config.h"` resolves:

```cmake
target_include_directories(genseq PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/src/config
        ${CMAKE_CURRENT_LIST_DIR}/src/generated
)
```

Leave `pico_enable_stdio_uart(genseq 1)` and `pico_enable_stdio_usb(genseq 0)` exactly as they are.

- [ ] **Step 3: Write the USB keyboard adapter**

Create `src/ui/hardware/UsbKeyboard.h`:

```cpp
#pragma once

#include <cstdint>
#include "KeyboardDecoder.h"

namespace hardware {

// TinyUSB host adapter. Owns the decoder, supplies it with reports and a
// millisecond clock, and dispatches decoded events into the StateManager.
class UsbKeyboard {
public:
    UsbKeyboard();

    void initialize();
    void update();      // pumps tuh_task() and the decoder's repeat timer

    // Entry points for the TinyUSB C callbacks.
    static UsbKeyboard* instance();
    void handleReport(const uint8_t* report, uint16_t len);
    void handleDisconnect();

private:
    KeyboardDecoder decoder;
};

} // namespace hardware
```

Create `src/ui/hardware/UsbKeyboard.cpp`:

```cpp
#include "UsbKeyboard.h"

#include "tusb.h"
#include "pico/time.h"
#include "../state/StateManager.h"
#include <cstdio>

namespace hardware {
namespace {

UsbKeyboard* g_instance = nullptr;

uint32_t nowMs() { return to_ms_since_boot(get_absolute_time()); }

} // namespace

UsbKeyboard::UsbKeyboard()
    : decoder([](const ui::events::Event& event) {
          ui::state::getStateManager().dispatch(event);
      })
{
    g_instance = this;
}

UsbKeyboard* UsbKeyboard::instance() { return g_instance; }

void UsbKeyboard::initialize()
{
    // Boot protocol gives a fixed 8-byte report, so no descriptor parsing.
    tuh_hid_set_default_protocol(HID_PROTOCOL_BOOT);
    tuh_init(0);
    printf("USB host initialized - waiting for a keyboard\n");
}

void UsbKeyboard::update()
{
    tuh_task();
    decoder.tick(nowMs());
}

void UsbKeyboard::handleReport(const uint8_t* report, uint16_t len)
{
    if (len < sizeof(hid_keyboard_report_t)) return;

    const auto* kb = reinterpret_cast<const hid_keyboard_report_t*>(report);

    KeyReport converted{};
    converted.mods = kb->modifier;
    for (uint8_t i = 0; i < KeyboardDecoder::MAX_KEYS; ++i) {
        converted.keys[i] = kb->keycode[i];
    }

    decoder.onReport(converted, nowMs());
}

void UsbKeyboard::handleDisconnect() { decoder.onDisconnect(); }

} // namespace hardware

extern "C" {

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx,
                      uint8_t const* desc_report, uint16_t desc_len)
{
    (void)desc_report;
    (void)desc_len;

    if (tuh_hid_interface_protocol(dev_addr, idx) != HID_ITF_PROTOCOL_KEYBOARD) {
        printf("HID device mounted (addr %u idx %u) - not a keyboard, ignoring\n",
               static_cast<unsigned>(dev_addr), static_cast<unsigned>(idx));
        return;
    }

    printf("Keyboard mounted (addr %u idx %u)\n",
           static_cast<unsigned>(dev_addr), static_cast<unsigned>(idx));
    tuh_hid_receive_report(dev_addr, idx);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t idx)
{
    printf("HID device unmounted (addr %u idx %u)\n",
           static_cast<unsigned>(dev_addr), static_cast<unsigned>(idx));
    if (auto* keyboard = hardware::UsbKeyboard::instance()) {
        keyboard->handleDisconnect();
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx,
                                uint8_t const* report, uint16_t len)
{
    if (tuh_hid_interface_protocol(dev_addr, idx) == HID_ITF_PROTOCOL_KEYBOARD) {
        if (auto* keyboard = hardware::UsbKeyboard::instance()) {
            keyboard->handleReport(report, len);
        }
    }

    // Re-arm: without this no further reports arrive.
    tuh_hid_receive_report(dev_addr, idx);
}

} // extern "C"
```

- [ ] **Step 4: Wire it into the UI controller**

In `src/ui/UIController.h`, add the include and the member:

```cpp
#include "hardware/UsbKeyboard.h"
...
    std::unique_ptr<hardware::UsbKeyboard> keyboard;
```

In `src/ui/UIController.cpp`, create and initialize the keyboard at the END of `initialize()` — after the views are registered and the state listener is subscribed, so no event can arrive before there is a view to handle it:

```cpp
    // Input last: events must not arrive before the views are registered.
    keyboard = std::make_unique<hardware::UsbKeyboard>();
    keyboard->initialize();

    printf("UI Controller initialized\n");
```

And pump it in `update()`:

```cpp
void UIController::update()
{
    keyboard->update();
    led->update();
    ledMatrix->update();
}
```

- [ ] **Step 5: Verify the firmware builds**

Run: `cmake --build build -j`
Expected: succeeds, producing `build/genseq.uf2`. If `tusb_config.h` is reported as not found, confirm `src/config` reached `target_include_directories`; if macros are reported as redefined, confirm `tusb_config.h` does not define `CFG_TUSB_MCU`/`CFG_TUSB_OS`/`CFG_TUSB_DEBUG`.

- [ ] **Step 6: Verify the host tests still pass**

Run: `cmake -S tests -B tests/build && cmake --build tests/build -j && ./tests/build/genseq_tests`
Expected: PASS — `41 tests, 0 failed`. Nothing in this task touches tested logic; this guards against an accidental edit.

- [ ] **Step 7: Verify on device**

Flash `build/genseq.uf2` (hold BOOTSEL while connecting, copy the file to the mounted drive). Then power the Pico via VSYS — **not** through the USB port, which now acts as a host — and attach a keyboard through a micro-USB OTG adapter. Watch the UART1 trace (TX on GPIO 8, 115200 baud).

Confirm each of:
1. `USB host initialized - waiting for a keyboard`, then `Keyboard mounted (addr 1 idx 0)` on plug-in.
2. `F1` and `F2` switch views — the matrix shows the number on `INIT` and the `SET` label on `SETTINGS`.
3. `space` toggles play/stop: the LED starts blinking, and `Sending command: 1` / `Sending command: 2` appear in the trace.
4. Tapping `up`/`down` moves the number by 1; holding for about half a second starts a smooth ramp.
5. Holding `up` and then pressing `shift` mid-hold switches the ramp to steps of 10.
6. The number stops at 99 and at 0.
7. `F5` prints `F5 pressed - no view bound` and changes nothing.
8. Unplugging mid-hold stops the ramp immediately, and `HID device unmounted` appears.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/config/tusb_config.h \
        src/ui/hardware/UsbKeyboard.h src/ui/hardware/UsbKeyboard.cpp \
        src/ui/UIController.h src/ui/UIController.cpp
git commit -m "feat(ui/hw): drive the UI from a USB keyboard over TinyUSB host

Adds UsbKeyboard, a thin TinyUSB host adapter that requests boot protocol,
converts hid_keyboard_report_t into KeyReport, supplies the decoder with a
millisecond clock, and dispatches decoded events into the StateManager.
The keyboard is created last during UIController::initialize() so no event
can arrive before the views are registered."
```

---

## Verification Summary

| What | Command | Expected |
|---|---|---|
| Host tests | `cmake -S tests -B tests/build && cmake --build tests/build -j && ./tests/build/genseq_tests` | `41 tests, 0 failed` |
| Firmware | `cmake --build build -j` | succeeds, `build/genseq.uf2` produced |
| Device | Task 5, Step 7 | all 8 checks pass |
