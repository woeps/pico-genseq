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
- **Independent musical streams** — each pattern combines a gate sequence, pitch sequence, and velocity sequence.
- **Euclidean rhythms** — configurable steps, pulses, rotation, note division, and gate length.
- **Pitch and velocity play orders** — forwards, backwards, pendulum, and deterministic random traversal.
- **Live pattern editing** — pattern, gate, pitch, and velocity views provide immediate auditioning with commit and undo behavior.
- **Serial MIDI output** — sends MIDI notes through one UART-based MIDI output.
- **Prototype keyboard control** — uses a USB HID boot-protocol keyboard through TinyUSB host mode.
- **LED matrix interface** — presents the UI on a 16×16 WS2812 matrix.
- **Flash persistence** — explicitly saves and restores sequencer configuration using a versioned, CRC-protected record.
- **Host test harness** — exercises core MIDI and persistence behavior without requiring the Pico hardware.

## Current Limitations

- The USB keyboard is a prototyping interface, not the intended final control surface.
- The LED matrix is the only planned display; older LCD and encoder references describe a previous direction.
- MIDI transport and clock behavior are not yet complete or performance-validated.
- Connectivity is currently limited to one serial MIDI output; MIDI input is a possible later extension.
- Real-time safety, note ownership, cross-core communication, and timing under stalls remain subjects of the performance-foundation milestone.

## Documentation

Online documentation is available at [https://woeps.github.io/pico-genseq/](https://woeps.github.io/pico-genseq/).

For comprehensive documentation, see the [docs/](docs/) directory:

- [GenSeq v1 Product Charter](docs/product/genseq-v1-product-charter.md) — approved product vision, audience, principles, scope, and performance model
- [Project Roadmap](ROADMAP.md) — milestone order, outcomes, and dependencies
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
