# USB Keyboard Input — Design

**Date:** 2026-08-21
**Branch:** `feature/usb-keyboard-input`
**Status:** Approved for planning

## Goal

Remove the GPIO/ADC input wiring (buttons and potentiometer) and replace it with a
USB HID keyboard attached to the Pico's native USB port. The reducer dispatches on
key identity plus modifiers.

Output hardware (`Led`, `LedMatrix`), the views, `UIState`, `StateManager`, the
command layer and the sequencer are unchanged. `Encoder`, `Display` and `LCD_I2C`
are referenced by nothing today and stay exactly as they are.

## Decisions

| Decision | Choice | Rationale |
|---|---|---|
| USB host transport | Native USB port via `tinyusb_host` | `pico_enable_stdio_usb` is already `0` (tracing runs on uart0, GPIO 0, 115200 baud), so the port is free. No new dependency, no PIO state machine, no GPIO cost. |
| Key representation | `enum class KeyId : uint8_t` whose values are HID usage codes, plus a `combo(KeyId, mods)` packing helper | Compiler-checked `switch` labels; combos become distinct `case` labels that cannot collide. |
| Event vocabulary | `KEY_PRESSED`, `KEY_RELEASED`, `KEY_HELD` | Keeps the existing naming; `KEY_HELD` carries auto-repeat semantics, which is what replaces the potentiometer as a continuous control. |
| Global bindings | Handled in `reduce()` before delegating to the active view | `F1`–`F12` and `space` mean the same thing everywhere; one owner avoids reimplementing them per view. |
| Verification | Host-side test target plus on-device smoke test | The report-diff and repeat-timing state machine is the risky part and is painful to verify against a real keyboard. |

## Keymap

| Binding | Effect | Scope |
|---|---|---|
| `F1` | Switch to `INIT` view | Global |
| `F2` | Switch to `SETTINGS` view | Global |
| `F3`–`F12` | Reserved for future views; consumed, traced, no effect | Global |
| `space` | Toggle play/stop (`setPlaying`) | Global |
| `up` / `down` | `value` ±1 | `InitView` |
| `shift+up` / `shift+down` | `value` ±10 | `InitView` |

BPM adjustment is deliberately out of scope; it will belong to a future view.
The former bindings (buttons A–F setting `value` 0–5, the pot sweeping `value`
0–99, hold-F entering settings) are removed, superseded by the sweep and `F2`.

## Architecture

The input layer splits along the line of what can be tested off-device.

```
USB keyboard
  -> tuh_task()  /  tuh_hid_report_received_cb
  -> UsbKeyboard        hid_keyboard_report_t -> KeyReport, now = to_ms_since_boot()
  -> KeyboardDecoder    onReport() / tick()   -> ui::events::Event
  -> StateManager::dispatch()
  -> reduce()           global map (F1-F12, space) first, else activeView->handleEvent()
  -> UIState -> listener -> UIController::onStateChanged -> activeView->render()
```

Everything from `dispatch()` rightward keeps its current shape. `UsbKeyboard`
dispatches into the same `state::getStateManager().dispatch()` entry point that
`Button` uses today.

### `hardware/KeyboardDecoder` (pure logic, no Pico or TinyUSB headers)

```cpp
struct KeyReport {          // the HID boot-protocol keyboard report
    uint8_t mods;           // ctrl/shift/alt/gui bitmask
    uint8_t keys[6];        // up to 6 concurrent usage codes, 0 = empty slot
};

class KeyboardDecoder {
public:
    using EventSink = std::function<void(const events::Event&)>;
    explicit KeyboardDecoder(EventSink sink);

    void onReport(const KeyReport& report, uint32_t nowMs);
    void tick(uint32_t nowMs);
    void onDisconnect();
};
```

The decoder owns the previous report, the set of keys currently down, and the
repeat timer. It never reads a clock; the caller supplies `nowMs`. `tick()` is
separate from `onReport()` because auto-repeat must fire when no report arrives —
a held key generates no further HID traffic.

### `hardware/UsbKeyboard` (Pico-facing adapter, deliberately thin)

Initializes TinyUSB host, owns a `KeyboardDecoder`, and exposes `update()`, which
calls `tuh_task()` then `decoder.tick(to_ms_since_boot(get_absolute_time()))`.
The TinyUSB C callbacks (`tuh_hid_mount_cb`, `tuh_hid_report_received_cb`,
`tuh_hid_umount_cb`) live in its `.cpp` and forward into the decoder. Mount filters
on `HID_ITF_PROTOCOL_KEYBOARD` and requests boot protocol, so reports arrive as a
fixed `hid_keyboard_report_t` with no report-descriptor parsing.

## Types

### `src/ui/Types.h`

`ButtonId`, `PotId`, `BUTTON_COUNT` and `POT_COUNT` are removed.

```cpp
namespace ui {

// Values ARE HID usage codes - the decoder casts straight from the report.
enum class KeyId : uint8_t {
    NONE = 0x00,
    A = 0x04, B = 0x05, C = 0x06, /* ... */ Z = 0x1D,
    NUM_1 = 0x1E, /* ... */ NUM_9 = 0x26, NUM_0 = 0x27,
    ENTER = 0x28, ESCAPE = 0x29, BACKSPACE = 0x2A, TAB = 0x2B, SPACE = 0x2C,
    MINUS = 0x2D, EQUAL = 0x2E,
    F1 = 0x3A, F2 = 0x3B, /* ... */ F12 = 0x45,
    RIGHT = 0x4F, LEFT = 0x50, DOWN = 0x51, UP = 0x52,
};

namespace mod {
    constexpr uint8_t NONE  = 0;
    constexpr uint8_t CTRL  = 1 << 0;   // matches the HID modifier byte
    constexpr uint8_t SHIFT = 1 << 1;
    constexpr uint8_t ALT   = 1 << 2;
    constexpr uint8_t GUI   = 1 << 3;
}

constexpr uint16_t combo(KeyId id, uint8_t mods = mod::NONE) {
    return (static_cast<uint16_t>(mods) << 8) | static_cast<uint8_t>(id);
}

} // namespace ui
```

Because the enum values are usage codes, the decoder does
`static_cast<KeyId>(report.keys[i])` with no lookup table and no filtering — a key
that is not enumerated still reaches the reducer as its numeric value; it simply
has no name yet.

The modifier constants mirror the HID modifier byte's low nibble. Right-hand
modifiers occupy the high nibble and are folded onto the same four bits, so
right-shift and left-shift are indistinguishable to the reducer.

### `src/ui/Event.h`

```cpp
enum class EventType : uint8_t { KEY_PRESSED, KEY_RELEASED, KEY_HELD };

struct Event {
    EventType type;
    uint32_t  timestamp;
    union {
        struct { KeyId id; uint8_t mods; } key;
    } data;

    static Event keyPressed (KeyId id, uint8_t mods, uint32_t ts);
    static Event keyReleased(KeyId id, uint8_t mods, uint32_t ts);
    static Event keyHeld    (KeyId id, uint8_t mods, uint32_t ts);
};
```

The POD-with-union shape is preserved, so `dispatch()` and `handleEvent()`
signatures are unaffected. The factories now take a timestamp: the field already
exists and is never set today, and the decoder has `nowMs` in hand.

### `src/ui/KeyNames.{h,cpp}`

`const char* toName(KeyId)` returning `"space"`, `"f1"`, `"up"`, and `"?"` for
anything unnamed. Not used by the reducer — it exists for `printf` tracing and
future display of a binding.

## Decoder semantics

**Report diffing.** `keys[6]` is a set, not a sequence; a keyboard may reshuffle
slots between reports with nothing changing. `onReport` compares membership: a code
present now but not before is a press, present before but not now is a release.
Multiple presses in one report are emitted in slot order. A report whose slots are
all `0x01` (`ErrorRollOver`) is discarded whole rather than read as six phantom
presses.

**Modifiers.** Modifiers live in `report.mods`, never in `keys[6]`, so pressing
`shift` alone emits nothing — it arms the next event. Every emitted event carries
the modifier byte as it stands at that moment.

**Auto-repeat.** Only the most recently pressed key repeats, matching standard PC
behavior and keeping sweeps predictable when a stray key is also down. Pressing a
new key moves the repeat target and restarts the delay. Releasing the target stops
repeat entirely, with no fallback to other held keys.

```
REPEAT_DELAY_MS    = 400   // press to first repeat
REPEAT_INTERVAL_MS = 30    // ~33 events/sec while held
```

`tick()` emits at most one `KEY_HELD` per call and then sets
`nextRepeat = nowMs + REPEAT_INTERVAL_MS`. Scheduling forward from now, rather than
accumulating from the previous deadline, means a late tick delays the next repeat
instead of discharging a burst of catch-up events into `value`. The UI loop runs on
a 1 ms `sleep_ms`, so the drift this trades away is immaterial.

Deadline comparisons use `static_cast<int32_t>(nowMs - nextRepeat) >= 0` so a hold
spanning the 32-bit millisecond rollover (~49 days) does not stall repeats.

**Intended consequence.** Because `KEY_HELD` carries live modifiers, pressing
`shift` during a hold flips the repeat stream from `combo(KeyId::UP)` to
`combo(KeyId::UP, mod::SHIFT)` mid-sweep: `value` ramps at ±1, and on pressing
shift it continues at ±10 without lifting the key.

**Disconnect.** `onDisconnect()` emits `KEY_RELEASED` for everything still down and
clears all state, so unplugging mid-hold cannot leave a key stuck or a repeat
running.

## Reducer

```cpp
UIState reduce(const UIState& state, const events::Event& event, ui::IView* activeView)
{
    if (isReserved(event.data.key.id)) {
        UIState next = state;
        if (event.type == events::EventType::KEY_PRESSED &&
            event.data.key.mods == mod::NONE) {
            applyGlobal(next, event.data.key.id);
        }
        return next;                      // consumed either way
    }
    return activeView ? activeView->handleEvent(state, event) : state;
}
```

Reservation is by key, independent of modifiers, so `shift+F1` is swallowed rather
than leaking to a view. This keeps modified F-keys available for future global
bindings instead of letting a view claim them. Only a bare `KEY_PRESSED` acts; the
matching release and any repeats are absorbed silently. `F1`–`F12` are usage codes
`0x3A`–`0x45`, contiguous, so `isReserved` is a range test on the cast value plus a
`SPACE` comparison.

The global map is a table indexed by function-key number:

```cpp
constexpr ViewId FUNCTION_KEY_VIEWS[] = { ViewId::INIT, ViewId::SETTINGS };
// F1 -> INIT, F2 -> SETTINGS; F3-F12 fall off the end -> consumed, traced, ignored
```

Adding a view later means adding a `ViewId` and one entry here. `space` calls the
existing `setPlaying(next, !state.playing)`, giving that function its first caller;
`InitView::render` already blinks the LED off `state.playing`.

`ViewId` gains a `COUNT` sentinel. `StateManager::views` (today a hardcoded
`std::array<IView*, 2>`) and `UIController::views` (today sized
`static_cast<size_t>(ViewId::SETTINGS) + 1`) both switch to it, so adding a view no
longer means editing an array size in two files and hoping they agree.

`setValue` already exists in `Reducer.cpp`, is not declared in `Reducer.h`, and has
no callers. It gets declared, and the 0–99 clamp lives inside it so coarse steps
saturate at the ends rather than running `value` negative.

## Views

`InitView::handleEvent` reduces to the sweep, since `space` and the F-keys never
reach it:

```cpp
if (event.type != KEY_PRESSED && event.type != KEY_HELD) return state;

state::UIState newState = state;
switch (combo(event.data.key.id, event.data.key.mods)) {
    case combo(KeyId::UP):               state::setValue(newState, state.value + 1);  break;
    case combo(KeyId::UP,   mod::SHIFT): state::setValue(newState, state.value + 10); break;
    case combo(KeyId::DOWN):             state::setValue(newState, state.value - 1);  break;
    case combo(KeyId::DOWN, mod::SHIFT): state::setValue(newState, state.value - 10); break;
    default: break;
}
return newState;
```

`KEY_PRESSED` and `KEY_HELD` are handled identically, which is what makes a hold
ramp.

`SettingsView::handleEvent` remains the stub it is. It now receives `up`/`down` and
ignores them; its TODO is preserved. Wiring settings up is separate work.

## File changes

**Deleted:** `src/ui/hardware/Button.{h,cpp}`, `src/ui/hardware/Potentiometer.{h,cpp}`.

**New:** `src/ui/hardware/KeyboardDecoder.{h,cpp}`, `src/ui/hardware/UsbKeyboard.{h,cpp}`,
`src/ui/KeyNames.{h,cpp}`, `src/config/tusb_config.h`, the `tests/` tree.

**Modified:** `src/ui/Types.h`, `src/ui/Event.h`, `src/ui/state/Reducer.{h,cpp}`,
`src/ui/state/UIState.h`, `src/ui/state/StateManager.h`, `src/ui/views/InitView.cpp`,
`src/ui/UIController.{h,cpp}`, `src/ui/ui.{h,cpp}`, `src/genseq.cpp`,
`src/ui/hardware/HardwareConfig.h`, `src/config/pins.h`, `CMakeLists.txt`.

`UI`'s constructor and `createUITask` drop `buttonPins` and `potPin`, keeping
`ledPin` and `ledMatrixPin`. `HardwareConfig` loses the same two fields. `pins.h`
loses `BUTTON_A_PIN`–`BUTTON_F_PIN` and `POT_PIN`.

`Encoder`, `Display`, `LCD_I2C`, `Led`, `LedMatrix` and everything under `driver/`
are untouched.

## Build

`src/config/tusb_config.h` — TinyUSB includes this unqualified, so `src/config`
joins `target_include_directories`:

```c
#define CFG_TUH_ENABLED     1
#define CFG_TUH_RPI_PIO_USB 0      // native port
#define CFG_TUH_HUB         1
#define CFG_TUH_HID         4      // interfaces, not devices
#define CFG_TUH_DEVICE_MAX  (CFG_TUH_HUB ? 5 : 4)
```

`CMakeLists.txt`: add `tinyusb_host` and `tinyusb_board` to
`target_link_libraries`, drop `hardware_adc` (`Potentiometer.cpp` was its only
user), add the `src/config` include path. `pico_enable_stdio_usb` stays `0` —
tracing continues over uart0 (GPIO 0, 115200 baud) and the native USB port
belongs to the host stack.
The existing `file(GLOB_RECURSE UI_SOURCES ...)` picks up the new sources with no
edit.

## Tests

`tests/` is a standalone CMake project built with the host compiler, not a target
inside the firmware build: the top-level `CMakeLists.txt` calls `pico_sdk_init()`
before anything else, so a native target cannot live there.

```
tests/
  CMakeLists.txt
  framework.h              ~40 lines: CHECK/CHECK_EQ macros, a registry, a reporting main
  stubs/pico/multicore.h   empty - satisfies commands/command.h
  stubs/command_stub.cpp   records sendCommand() calls so tests can assert PLAY/STOP
  stubs/hardware_stub.cpp  no-op Led / LedMatrix bodies
  test_decoder.cpp
  test_reducer.cpp
```

No external dependency — a hand-rolled harness rather than vendored Catch2 or
doctest, since every assertion here is an equality check on a small value.
`Led.h` and `LedMatrix.h` are Pico-free, so `InitView.cpp` compiles on the host
against the stub bodies and the real view mapping is under test.

**Decoder:** press and release across slot reshuffles; a key held while another is
pressed and released; `ErrorRollOver` reports discarded; disconnect releasing
everything still down; with an injected `nowMs`, the first `KEY_HELD` at 400 ms and
subsequent ones at 30 ms; a late `tick()` yielding one event rather than a burst;
repeat surviving the 32-bit millisecond rollover.

**Reducer:** `F1`/`F2` switching views; `F3`–`F12` and `shift+F1` consumed without
effect; `space` toggling `playing` and emitting the right command; reserved keys
never reaching a `FakeView`; unreserved keys always reaching it.

**`InitView`:** `up`/`down` at ±1; `shift` variants at ±10; `KEY_HELD` behaving as
`KEY_PRESSED`; the 0–99 clamp holding at both ends.

**On-device.** Nothing above exercises TinyUSB itself. Flash, attach a keyboard
through an OTG adapter with the Pico powered via VSYS (not the USB port), and
confirm enumeration in the UART trace, `F1`/`F2` switching views, `space` blinking
the LED, and a held `up` ramping the number on the matrix.

## Out of scope

- BPM control (a future view owns it)
- `SettingsView` behavior beyond remaining a stub
- `Encoder`, `Display` and `LCD_I2C`, which stay unreferenced and unmodified
- Non-boot-protocol keyboards and consumer-control/media keys
- Key remapping at runtime or persisted keymaps
