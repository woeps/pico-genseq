# UI System Guide

The UI runs on core0. Input comes from a USB keyboard (TinyUSB HID host) or, in bench builds,
from the host-control serial channel. Output is the 16x16 WS2812 LED matrix and the board LED.
The user-facing behaviour of every view is described in the [User Manual](../user/user-manual.md).

## Data flow

```
UsbKeyboard / KeyboardDecoder        HostControl ("H:key …", bench builds)
            │ Event (KEY_PRESSED / KEY_RELEASED / KEY_HELD, key id, modifiers)
            ▼
StateManager::dispatch(event)
            │
            ▼
reduce(state, event, activeView)     src/ui/state/Reducer.cpp
  1. Ctrl+S          → state.saveRequested (the UI loop performs the save)
  2. Space, F1-F12   → global handling, never reaches a view
  3. go-to picker / Ctrl+G in set views
  4. otherwise       → activeView->handleEvent(state, event)
            │  views and reducer functions mutate UIState IN PLACE and send commands to core1
            ▼
listener → UIController::onStateChanged → view switch (onExit/onEnter) → activeView->render(state)
                                          → load pixel redraw → play LED
```

`UIState` is well over 1 KB and core0's stack is 2 KB, so it is never copied by value: views get a
reference and change it in place, and the reducer functions take `UIState&`. Tests use the
value-style adapters in `tests/helpers.h`.

## Views

| ViewId | Key | Class | Shows / edits |
|---|---|---|---|
| `SETTINGS` | F1 | `SettingsView` | BPM |
| `INSTRUMENTS` | F2 (boot view) | `InstrumentsView` | channel, mute and pattern assignment of each Instrument |
| `PATTERNS` | F3 | `PatternsView` | pattern grid, stream selection, set references, Instrument membership markers |
| `GATE_SET` | F4 | `GateSetView` | the gate set the selected pattern references |
| `PITCH_SET` | F5 | `PitchSetView` | the note set the selected pattern references |
| `VELOCITY_SET` | F6 | `VelocitySetView` | the velocity set the selected pattern references |

The function-key table is `FUNCTION_KEY_VIEWS` in `Reducer.cpp`. F4-F6 go through
`openSetView()`, which also points the set view at the referenced slot and begins the draft edit;
Enter on a stream in the PATTERNS view uses the same function. F7-F12 and modified function keys
are consumed without effect. Leaving a set view discards its uncommitted draft
(`setCurrentView()`).

The set views share `SetViewCommon.*` for the slot map, the users row, the refusal words and the
go-to picker.

## View interface

```cpp
// src/ui/views/IView.h
class IView {
public:
    virtual void onEnter() {}                       // usually clears the matrix
    virtual void onExit() {}
    virtual void handleEvent(state::UIState& state, const events::Event& event);  // in place
    virtual void render(const state::UIState& state) = 0;  // draw from state only
};
```

`render()` runs after every state change. It must draw every pixel it owns (also the "off" ones),
because the matrix is only cleared in `onEnter()`.

### Reserved pixels

| Pixel | Owner |
|---|---|
| rows 0-4, columns 0-11 | the view's three-glyph label |
| (13..15, 0) | editor status pixels (dirty, in scale, gate step) |
| (15, 4) | the load indicator, drawn by `UIController` after every render; no view may use it |
| top-left pixel of a pattern cell | the cursor / selection in the PATTERNS and INSTRUMENTS grids (white); a cell's state must stay readable from its other three pixels |
| whole matrix | save banners and the boot banner (`UIController::updateSaveBanner`) for 1.5 s |

Colours are `0xXXGGRRBB`.

## UIController responsibilities

`src/ui/UIController.*`:

- creates the hardware, the views and the persistence stack, restores the saved setup at boot and
  replays it to core1 (`restoreSequencerFromState`);
- runs the UI loop: keyboard, host control, pending save, banners, load pixel and fault mirror,
  LED and matrix refresh;
- drives the board LED: blinking while playing, banner patterns while a banner is shown;
- mirrors core1's timing fault into `UIState` (`mirrorTimingFault` in `LoadIndicator.h`), which
  also marks the transport stopped so the next Space is a Play that acknowledges the fault;
- draws the load pixel: off, amber (a MIDI byte waited 10 ms or more in the last 2 s), red (fault).

## Adding a view

1. Add a `ViewId` before `COUNT` in `UIState.h` (keep the order of `FUNCTION_KEY_VIEWS` in mind).
2. Create `src/ui/views/MyView.{h,cpp}` implementing `IView`; edit state only through reducer
   functions that send the matching commands.
3. Construct it in `UIController::initialize()` and register it in the `views` array.
4. Bind it to a function key in `FUNCTION_KEY_VIEWS` if it is a top-level view, and add its name
   to `viewName()` in `HostControl.cpp`.
5. Add it to `tests/CMakeLists.txt` and to `tools/manual_screens/build.sh`, write view tests with
   the fake `LedMatrix` in `tests/stubs/`, and add a screen to the manual with the generator.

## Testing

- `tests/`: views, reducer, set libraries, Instruments, load indicator and keyboard decoder on the
  host (`tests/stubs/` replaces the commands and the hardware).
- `tools/manual_screens/build.sh` renders the manual's screens from the same view code.
- On hardware, a `-DGENSEQ_HOST_CONTROL=ON` build accepts `H:key`, `H:view`, `H:dump` and more on
  the UART0 console; see `HostControl.h` for the command list.

## Guidelines

- Keep the UI loop short: the measured worst UI gap is about 15 ms (a save), the limit is 50 ms.
- Never block on core1 and never read sequencer state except the published telemetry words.
- One cross-core command per user action where possible; several dependent commands go inside a
  `BATCH_BEGIN` / `BATCH_END` frame.
