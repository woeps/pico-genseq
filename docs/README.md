# GenSeq Documentation

Welcome to the GenSeq documentation. This section provides comprehensive guides for developing, building, and understanding the GenSeq MIDI sequencer project.

## Quick Start

- [Repository Structure](architecture/repository-structure.md) - Understand the project organization
- [Dependencies & Requirements](reference/dependencies.md) - Set up your development environment
- [Build & Flash](technical/build-flash.md) - Build and flash the firmware

## Core Documentation

### Architecture
- [Repository Structure](architecture/repository-structure.md) - Project layout and dual-core architecture
- [Architecture Deep Dive](architecture/architecture-deep-dive.md) - Technical details of the system design
- [Hardware Configuration](architecture/hardware-config.md) - Pin assignments and wiring diagrams

### Development
- [CMake Configuration](development/cmake-configuration.md) - Build system setup and common operations
- [Development Workflow](development/development-workflow.md) - Coding standards and best practices
- [UI System Guide](development/ui-system-guide.md) - User interface architecture and components

### Technical
- [MIDI Implementation](technical/midi-implementation.md) - MIDI output and timing details
- [Build & Flash Guide](technical/build-flash.md) - Build instructions and flashing procedures

### Reference
- [Dependencies & Requirements](reference/dependencies.md) - Software and hardware requirements
- [Troubleshooting FAQ](reference/troubleshooting.md) - Common issues and solutions

## Project Overview

GenSeq is a generative hardware MIDI sequencer based on the Raspberry Pi Pico (RP2040). It features:

- **Dual-core architecture** separating UI and sequencing
- **Real-time MIDI output** with precise timing
- **Generative patterns** using Euclidean rhythms
- **Extensible UI** with LCD display and LED matrix
- **Full MIDI implementation** including clock output

## Getting Started

1. **Set up environment** - Follow [Dependencies & Requirements](reference/dependencies.md)
2. **Build the project** - Use the [Build & Flash Guide](technical/build-flash.md)
3. **Understand the architecture** - Read [Repository Structure](architecture/repository-structure.md)
4. **Start developing** - Follow the [Development Workflow](development/development-workflow.md)

## Documentation Structure

```
docs/
├── README.md                    # This file
├── architecture/                # Architecture documentation
│   ├── repository-structure.md  # Project organization
│   ├── architecture-deep-dive.md # Technical architecture
│   └── hardware-config.md       # Hardware documentation
├── development/                 # Development guides
│   ├── cmake-configuration.md   # Build system details
│   ├── development-workflow.md  # Development guide
│   └── ui-system-guide.md       # UI architecture
├── technical/                   # Technical documentation
│   ├── build-flash.md          # Build and flash procedures
│   └── midi-implementation.md   # MIDI system details
└── reference/                   # Reference materials
    ├── dependencies.md          # Setup requirements
    └── troubleshooting.md      # Common issues
```

## Contributing to Documentation

When adding new features:
1. Update relevant documentation sections
2. Add new pages for major features
3. Update pin assignments in [Hardware Configuration](architecture/hardware-config.md)
4. Document new commands in [Architecture Deep Dive](architecture/architecture-deep-dive.md)

## Resources

- [Main Project README](../README.md)
- [Pico SDK Documentation](https://raspberrypi.github.io/pico-sdk-doxygen/)
- [RP2040 Datasheet](https://datasheets.raspberrypi.org/rp2040/rp2040-datasheet.pdf)
- [MIDI Specification](https://www.midi.org/specifications)

## Support

If you encounter issues:
1. Check the [Troubleshooting FAQ](reference/troubleshooting.md)
2. Search existing GitHub issues
3. Create a new issue with detailed information
4. Include debug output and hardware details
