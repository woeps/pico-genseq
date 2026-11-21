# GenSeq

A live-performance generative hardware MIDI sequencer based on the Raspberry Pi Pico (RP2040).

:warning: This project is still in development and is not yet performance-ready. :warning:

## Goal

GenSeq helps electronic musicians improvise complete sequences during a live performance without a computer. It combines multiple independent patterns with performer-controlled generation, layering, muting, and transitions.

The current prototype uses a USB keyboard to develop the interaction model and a 16×16 LED matrix as its visual interface. Dedicated hardware controls will replace the keyboard after the workflow has been validated; no LCD is planned.

See the [GenSeq v1 Product Charter](docs/product/genseq-v1-product-charter.md) for the approved product direction and scope.

## Current Features

- **Dual-core architecture** — separates UI work from sequencer execution.
- **Multi-pattern sequencing** — supports up to 15 independently configurable patterns.
- **Shared sets** — gate, note and velocity sets live in libraries of 16 each; a pattern references one of each, and several patterns can share a set.
- **Instruments** — up to 16 Instruments, each with its own MIDI channel, mute, and a selection of patterns; a pattern can play on several channels at once.
- **Generative parameters** — step probability, scale constraint, velocity humanization, deterministic seeds and stream coupling.
- **Euclidean rhythms** — configurable steps, pulses, rotation, note division, and gate length.
- **Pitch and velocity play orders** — forwards, backwards, pendulum, and deterministic random traversal.
- **Live pattern editing** — instrument, pattern, gate, pitch, and velocity views provide immediate auditioning with commit and undo behavior; F1-F6 jump through the hierarchy.
- **Serial MIDI output** — notes on up to 16 channels plus MIDI Clock, Start and Stop through one UART-based DIN output, buffered so bursts never break the beat, with a load indicator and a fail-closed overload stop.
- **Prototype keyboard control** — uses a USB HID boot-protocol keyboard through TinyUSB host mode.
- **LED matrix interface** — presents the UI on a 16×16 WS2812 matrix.
- **FRAM persistence** — Ctrl+S saves the whole setup to an external SPI FRAM in a versioned, CRC-protected A/B record without interrupting playback; older save formats still load.
- **Host test harness** — exercises the sequencer, MIDI output, persistence and UI without the Pico hardware; a host-control serial channel drives hardware tests without a keyboard.

## Current Limitations

- The USB keyboard is a prototyping interface, not the intended final control surface.
- The LED matrix is the only planned display; older LCD and encoder references describe a previous direction.
- Connectivity is currently limited to one serial MIDI output; MIDI input is a possible later extension.
- A pattern cannot yet be stopped on its own from the keyboard (use an Instrument route or mute).
- Scenes, arrangement and projects (milestone 5) are not implemented.

## Documentation

Online documentation is available at [https://woeps.github.io/pico-genseq/](https://woeps.github.io/pico-genseq/).

For comprehensive documentation, see the [docs/](docs/) directory:

- [GenSeq v1 Product Charter](docs/product/genseq-v1-product-charter.md) — approved product vision, audience, principles, scope, and performance model
- [Project Roadmap](ROADMAP.md) — milestone order, outcomes, and dependencies
- [User Manual](docs/user/user-manual.md) — views, keys and how to play the device
- [Architecture Deep Dive](docs/architecture/architecture-deep-dive.md) — technical details
- [Build & Flash Guide](docs/technical/build-flash.md) — detailed instructions
- [Dependencies & Setup](docs/reference/dependencies.md) — development environment
- [Troubleshooting](docs/reference/troubleshooting.md) — common issues

## Building

```bash
mkdir build
cd build
cmake .. -G Ninja
ninja
```

Use `ninja clean` when a clean rebuild is required.

## Flashing

Either:

- Copy `build/genseq.uf2` to the device.
- Use a debugger such as [PicoProbe](docs/technical/build-flash.md#method-2-picoprobe-flashing-advanced).

## Debugging

In VS Code, press <kbd>F5</kbd> or select **Pico Debug (Cortex-Debug)** in the Run and Debug toolbar.

## Resources

- [MIDI Clock analyzer](https://disasterarea.io/midiclock)

## Roadmap

See [`ROADMAP.md`](ROADMAP.md) for planned milestones and their current status. Detailed requirements and implementation plans should follow the approved [product charter](docs/product/genseq-v1-product-charter.md).
