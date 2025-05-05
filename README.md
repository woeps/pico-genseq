# GenSeq
A generative hardware MIDI sequencer based on the Raspberry Pi Pico. (RP2040)

## Project Architecture
- both cores are used to separate
    - user interface (e.g. encoder, switches, buttons, displays)
    - sequencer logic
- the ui core sends command messages to the sequencer core, which then adapts it's sequencing behaviour accordingly (e.g. play, stop, change bpm, etc.)
- the sequencer logic is implemented in the `sequencer` directory
    - sequencer data uses ticks
    - there are 2 different data types, which are stored and processed separated from each other:
        - pitch set (note height, velocity)
        - rhythm set (thiming, gate on/off & length)
    - actual note on/off messages are derived from above data, as well as playmode (defining the order) - this combination of data should be refered to as a pattern
    - different patterns can use the same pitch/rhythm set
    - patterns can be played in parallel
    - patterns can be active or inactive
    - only active patterns are played
    - midi messages are sent over the 2nd UART port
- the user interface is implemented in the `ui` directory
    - main components of the UI are:
        - buttons
        - encoder
            - read with an PIO implementation using intterupts
        - display (HD44780 with FC113 controller)
        - leds
- the main file (`genseq.cpp`) combines both into a single program
- great care should be taken to keep the code easily extendable in the future

## Features
- start/stop button: toggles sequencer playback (playing all patterns in parallel)
- In the first iteration, Rhythm sets are generated based on euclidean circles
    - in later iterations manual step editing should be possible

## Building
- `cd ./build`
- `cmake ..`
- `ninja clean` - optionally
- `ninja`

## Flashing
- either: copy `genseq.uf2` to the device
- or: use debugger (PicoProbe)

## Debugging
- in VSCode, press F5 / select `Pico Debug (Cortex-Debug)` in Run & Debug Toolbar

### TODO

- [*] extract all parts of sequencer into own files
- [ ] move logic to select next step into play_mode.cpp (out of sequencer.cpp)