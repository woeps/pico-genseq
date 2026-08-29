# USB Keyboard Bring-up & Verification Guide

## What This Is

GenSeq's UI is driven by a USB HID keyboard connected to the Pico's native
USB port, which runs in **host** mode via TinyUSB. This guide walks through
bringing up a freshly flashed board with a keyboard attached and verifying
that input actually works end to end. Use it any time you flash new firmware
onto hardware and need to confirm the keyboard path is alive, or when
diagnosing "the keyboard does nothing" reports.

It applies to the `genseq.uf2` firmware built from this repository's
`CMakeLists.txt` (the Debug configuration in particular — see
[Debug vs. Release](#debug-vs-release-diagnostics) below).

## Hardware Setup

### Power via VBUS (read this first)

The most common bring-up failure is a power mistake, not a firmware bug.

**Apply 5 V to pin 40 (VBUS).** On a Pico, VBUS wires directly to the
micro-USB connector, and VSYS is fed *from* VBUS through a Schottky diode
oriented VBUS → VSYS. Powering VBUS therefore powers both the keyboard
(through the connector) and the Pico itself (through that diode into VSYS) —
no separate VSYS feed is needed.

Do **not** power the board from VSYS alone while expecting a plain USB-OTG
adapter to power the keyboard. VSYS does not back-feed VBUS, a plain adapter
supplies no power of its own, the keyboard never enumerates, and the trace
sits at `waiting for a keyboard` forever — which looks exactly like a
firmware bug but is a wiring problem.

Two other options that work:
- Power VSYS as before **and** separately jumper 5 V onto VBUS.
- Use a **powered** USB hub between the OTG adapter and the keyboard — the
  hub supplies the keyboard's power itself. This is also the fastest route
  to a first successful enumeration if you're unsure about your wiring.

### Connecting the keyboard

Attach the keyboard to the Pico's native USB port through a micro-USB OTG
adapter (directly, or via a hub as above).

## Serial Console

Trace output goes to **uart0, TX on GPIO 0, 115200 baud**. This is the Pico's
default stdio UART.

Do not confuse this with MIDI: MIDI TX runs on a separate link, **uart1, GPIO
8, at 31250 baud**. Watching GPIO 8 for keyboard trace will show nothing
useful.

## Pre-Flight: Expected Boot Lines

Before plugging a keyboard in, confirm the console shows:

```
GenSeq MIDI Sequencer starting...
USB host initialized - waiting for a keyboard
```

If these lines don't appear, the problem is the serial hookup (wrong UART,
wrong baud, bad wiring, wrong port), not USB. Fix that before troubleshooting
anything keyboard-related.

## Functional Checks

With the keyboard connected, confirm each of the following:

1. `USB host initialized - waiting for a keyboard`, then
   `Keyboard mounted (addr 1 idx 0)` on plug-in.
2. `F1` and `F2` switch views — the matrix shows the number on `INIT` and the
   `SET` label on `SETTINGS`.
3. `space` toggles play/stop: the LED starts blinking, and
   `Sending command: 1` / `Sending command: 2` appear in the trace.
4. Tapping `up`/`down` moves the number by 1; holding for about half a second
   starts a smooth ramp.
5. Holding `up` and then pressing `shift` mid-hold switches the ramp to steps
   of 10.
6. The number stops at 99 and at 0.
7. `F5` prints `F5 pressed - no view bound` and changes nothing.
8. Unplugging mid-hold stops the ramp immediately, and
   `HID device unmounted` appears.
9. **Replug**: unplug, wait, replug, and confirm everything above still
   works. This exercises state clearing on disconnect followed by a fresh
   mount and re-arm.
10. **Stuck-modifier check**: hold `shift`, unplug mid-hold, replug, then
    press `up`. Confirm it steps by 1 and not by 10 — i.e. the modifier state
    was actually cleared on disconnect, not just the held key.

## Troubleshooting

### "Not a keyboard, ignoring" for an obvious keyboard

If a device that is clearly a keyboard prints:

```
HID device mounted (addr N idx M) - not a keyboard, ignoring
```

it does not declare the HID boot subclass, and TinyUSB reports its interface
protocol as `HID_ITF_PROTOCOL_NONE` rather than `HID_ITF_PROTOCOL_KEYBOARD`.
Try a plainer keyboard — gaming keyboards with heavy onboard firmware are the
usual offenders. Cheap membrane keyboards almost always work.

### Composite devices (multiple mount lines)

Most modern keyboards expose two or more HID interfaces (typically the
keyboard itself plus a consumer/media-key interface). Expect **multiple**
`mounted` lines on plug-in — only one of them is the actual keyboard
interface. Confirm typing still works despite the extra interfaces; this
exercises the interface-index filtering that decides which reports are
treated as keyboard input.

### Going through a hub

Hub support is compiled in but not exercised by any automated test. Plug the
keyboard in through a hub and confirm it still enumerates and works. A
**powered** hub also sidesteps the VBUS power issue described above, so it's
a good option if you don't want to wire 5 V directly to pin 40.

### Debug vs. Release diagnostics

The firmware built from this repository's default configuration is a Debug
build, which enables TinyUSB's own internal debug logging on the same UART.
This is genuinely useful during bring-up — TinyUSB will print its own error
diagnostics if something goes wrong at the USB stack level, not just at the
application level. Do bring-up and verification with a Debug build;
switching to a Release build silently removes that diagnostic channel.
