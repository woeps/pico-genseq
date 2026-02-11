# GenSeq

A generative hardware MIDI sequencer based on the Raspberry Pi Pico (RP2040).

:warning: This project is still in development. It may take some (more) time to get to a stable state. :warning:

## Goal

GenSeq is a hardware MIDI sequencer that creates generative musical patterns based on separate sequences of pitches and rhythms. It provides real-time MIDI output with precise timing, an intuitive user interface, and extensible architecture for future enhancements.

## Key Features

- **Dual-core architecture** - Separates UI and sequencer logic for responsive performance
- **Generative sequencing** - Creates patterns using Euclidean rhythm algorithms
- **Real-time MIDI output** - Precise timing with MIDI clock support
- **Interactive UI** - LCD display, rotary encoder, and button matrix
- **Pattern management** - Multiple patterns with shared pitch/rhythm sets
- **Extensible design** - Clean architecture for easy feature additions

## Documentation

For comprehensive documentation, see the [docs/](docs/) directory:
- [Architecture Deep Dive](docs/architecture/architecture-deep-dive.md) - Technical details
- [Build & Flash Guide](docs/technical/build-flash.md) - Detailed instructions
- [Dependencies & Setup](docs/reference/dependencies.md) - Development environment
- [Troubleshooting](docs/reference/troubleshooting.md) - Common issues

## Building
- `mkdir build`
- `cd ./build`
- `cmake .. -G Ninja`
- `ninja clean` - optionally
- `ninja`

## Flashing
- either: copy `build/genseq.uf2` to the device
- or: use debugger ([PicoProbe](docs/technical/build-flash.md#method-2-picoprobe-flashing-advanced))

## Debugging
- in VSCode, press F5 / select `Pico Debug (Cortex-Debug)` in Run & Debug Toolbar

## Resources

- Midi Clock (tempo) analyzer: https://disasterarea.io/midiclock

## TODO

See [TODO.md](TODO.md) for detailed task list and implementation notes.

Key upcoming features:
- [ ] Manual step editing for rhythm sets
- [ ] UI configuration for settings (Euclidean pulses, length, etc.)
- [ ] Decouple MIDI from sequencer core
- [ ] Move step selection logic to play_mode.cpp

