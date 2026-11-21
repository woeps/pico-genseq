# Hardware Configuration

The current prototype is a Raspberry Pi Pico (RP2040) with a USB keyboard, a 16x16 LED matrix, a
DIN MIDI output and an external FRAM for the saved setup. There is no LCD, encoder, button or
potentiometer in the current firmware; the classes for them in `src/ui/hardware/` are leftovers
of an earlier prototype.

## Pin assignments (`src/config/pins.h`)

| GPIO | Function | Notes |
|---|---|---|
| 0 / 1 | UART0 TX / RX | stdio console and the host-control channel (`H:` commands), usually through the debug probe's UART bridge at 115200 baud |
| 8 | UART1 TX → MIDI OUT | 31,250 baud, `MIDI_UART_PIN_TX` |
| 9 | UART1 RX | configured, unused (no MIDI input yet) |
| 10 | SPI1 SCK → FRAM SCK | `FRAM_SPI_PIN_SCK` |
| 11 | SPI1 TX → FRAM SI | `FRAM_SPI_PIN_MOSI` |
| 12 | SPI1 RX ← FRAM SO | `FRAM_SPI_PIN_MISO` |
| 13 | FRAM CS (software, active low) | `FRAM_SPI_PIN_CS` |
| 16 | WS2812 data → LED matrix | `LED_MATRIX_PIN`, driven by PIO + DMA |
| 25 | board LED | `LED_PIN`; blinks while playing, shows save banners |
| USB | TinyUSB host for the keyboard | the Pico's USB port is a host port; the console goes through UART0 (see below) |
| SWD | debug probe | flashing and debugging |

Optional timing trace pins (`GENSEQ_TRACE_*_PIN`, off by default) are described in
[Performance Instrumentation](../development/performance-instrumentation.md).

## Components

### USB keyboard

Any USB HID boot-protocol keyboard. The Pico runs TinyUSB in host mode (`UsbKeyboard`,
`KeyboardDecoder`), so the keyboard plugs into the Pico's micro-USB port through an OTG adapter.
The keyboard needs 5 V on VBUS: power the board through VBUS (which also feeds VSYS), not through
VSYS alone. Details and bring-up checks:
[USB Keyboard Bring-up](../technical/usb-keyboard-bringup.md).

### LED matrix

A 16x16 WS2812 matrix (256 pixels) on GPIO 16. `LedMatrix` keeps a frame buffer and sends it by
DMA only when a pixel changed. Colours are `0xXXGGRRBB`. Power the matrix from 5 V, sized for
its current draw.

### MIDI output

A standard 5-pin DIN MIDI OUT driven from UART1 TX (GPIO 8) at 31,250 baud, through the two series
resistors of the MIDI 1.0 electrical specification for a 3.3 V sender. See [MIDI Implementation](../technical/midi-implementation.md).

### FRAM

An MB85RS2MTA SPI FRAM (256 KB) on SPI1. The firmware uses an 8 KB region split into two 4 KB A/B
slots for the transactional save. Saving to FRAM does not touch the flash, so it does not stop the
cores or the MIDI output. Wire it with short leads; the driver checks the device ID first
(bring-up firmware: `-DGENSEQ_BUILD_FRAM_BRINGUP=ON`).

### Debug probe

A second Pico running Debugprobe (CMSIS-DAP) gives SWD flashing and debugging plus a UART bridge
to GPIO 0/1. All hardware tests in `test/bench/` and the spec task files use this route:

```bash
openocd -f interface/cmsis-dap.cfg -c "adapter speed 5000" -f target/rp2040.cfg \
        -c "init; halt; program build/genseq.elf verify; shutdown"
```

After flashing, reboot with a power-cycle or with the watchdog sequence documented in
`test/bench/README.md`; OpenOCD's own `reset run` can leave the two cores in a bad state on this
setup.

## Adding hardware

1. Add the pins to `src/config/pins.h` and check them against the table above.
2. Put the driver in `src/ui/hardware/` (core0) or keep it out of the core1 path entirely; core1
   runs the sequencer and must not wait on peripherals.
3. Never share a peripheral between the cores without a documented hand-off.
