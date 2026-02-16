# Hardware Configuration

## Overview

The GenSeq hardware configuration defines all pin assignments, supported components, and wiring diagrams. This guide covers everything needed to connect the hardware components correctly.

## Pin Assignments

### Current Pin Configuration

```cpp
// From src/config/pins.h

// BUTTONS (A-F)
#define BUTTON_A_PIN 20
#define BUTTON_B_PIN 21
#define BUTTON_C_PIN 0  // TODO: assign actual pin
#define BUTTON_D_PIN 0  // TODO: assign actual pin
#define BUTTON_E_PIN 0  // TODO: assign actual pin
#define BUTTON_F_PIN 0  // TODO: assign actual pin

// ENCODER
#define ENCODER_PIN_A 14  // Encoder phase A
#define ENCODER_PIN_B 15  // Encoder phase B
#define ENCODER_PIO pio0  // PIO block 0
#define ENCODER_SM 0      // State machine 0

// MIDI UART
#define MIDI_UART uart1       // UART block 1
#define MIDI_UART_PIN_TX 8    // MIDI transmit pin
#define MIDI_UART_PIN_RX 9    // MIDI receive pin

// POTENTIOMETER (ADC)
#define POT_PIN 26            // 10kΩ linear pot on ADC0

// STATUS LED
#define LED_PIN 25            // Onboard LED
```

### Pin Map Diagram

```
Raspberry Pi Pico Pinout
========================

          3V3  (40) [ ] (39)  GND
          GPIO2 (35) [ ] (38)  GPIO3
          GPIO4 (34) [ ] (37)  GPIO5  ← I2C SCL (Display)
          GPIO6 (33) [ ] (36)  GPIO7
          GPIO8 (32) [ ] (31)  GPIO9  ← MIDI RX
  MIDI TX → GPIO10(31) [ ] (30)  GPIO11
          GPIO12(30) [ ] (29)  GPIO13
 Encoder A → GPIO14(29) [ ] (28)  GPIO13 ← Encoder B
          GPIO16(27) [ ] (27)  GPIO17
          GPIO18(26) [ ] (26)  GPIO19
Button E → GPIO20(25) [ ] (25)  GPIO21 ← Button A
          GPIO22(24) [ ] (24)  GPIO23
          GND   (23) [ ] (23)  VSYS
          VBUS  (22) [ ] (22)  GND
          3V3   (21) [ ] (21)  3V3
          LED   (20) [ ] (20)  GND
```

## Hardware Components

### 1. LCD Display

**Specification**: HD44780 with FC113 I2C controller

**Connection**:
```
Pico      →  Display Module
GPIO4     →  SDA
GPIO5     →  SCL
3V3       →  VCC
GND       →  GND
```

**I2C Address**: 0x27 (configurable via jumpers)

**Library**: Custom driver in `src/ui/hardware/driver/LCD_I2C.cpp`

### 2. Rotary Encoder

**Specification**: Quadrature encoder with push button

**Connection**:
```
Pico      →  Encoder
GPIO14    →  Phase A
GPIO15    →  Phase B
GPIO20    →  Button
3V3       →  VCC
GND       →  GND
```

**Implementation**: PIO-based for interrupt-free reading

### 3. MIDI Interface

**Specification**: 5-pin DIN MIDI connector

**Circuit**:
```
Pico TX → 220Ω → MIDI Pin 5
MIDI Pin 4 → 220Ω → Pico RX
MIDI Pin 2 → GND
```

**Isolation**: Opto-isolator recommended for MIDI input

### 4. Button Matrix

**Specification**: 16 buttons in 8x2 matrix

**Connection Pattern**:
```
Rows: GPIO22, GPIO21, GPIO20, GPIO19
Cols: GPIO18, GPIO17, GPIO16, GPIO15
```

**Scanning**: Row-by-row scanning with debouncing

### 5. Potentiometer

**Specification**: 10kΩ linear potentiometer

**Connection**:
```
Pico      →  Potentiometer
GPIO26    →  Wiper (middle pin) via RC low-pass filter
3V3       →  One outer pin
GND       →  Other outer pin
```

**RC Low-Pass Filter**:
```
POT_WIPER ─── 1kΩ ─── GPIO26 (ADC0)
                  │
                100nF
                  │
                 GND
```
- **R**: 1kΩ, **C**: 100nF (cutoff ≈ 1.6 kHz)
- Filters high-frequency noise while preserving responsiveness for manual control

**ADC**: 12-bit (0–4095), read via `hardware_adc`

**Noise Filtering**: RC low-pass filter on input + EMA software smoothing (α=1/4) + threshold-based change detection (±4 LSBs)

### 6. LED Matrix

**Specification**: 16x2 WS2812B LED strip

**Connection**:
```
Pico GPIO23 → LED Data In
5V          → LED VCC
GND         → LED GND
```

**Driver**: DMA-based for smooth updates

## Wiring Diagram

### Complete Wiring

```
┌─────────────────────────────────────────────────────┐
│                   PICO BOARD                        │
│                                                     │
│    [USB]                                           │
│                                                     │
│  GPIO4 ────────┐                                   │
│  GPIO5 ────────┼───[ I2C LCD ]                     │
│  3V3 ─────────┘                                   │
│  GND ─────────────────────────────────────────────┐ │
│                                                   │ │
│  GPIO14 ──────┐                                   │ │
│  GPIO15 ──────┼───[ ENCODER ]                     │ │
│  GPIO20 ──────┘                                   │ │
│                                                   │ │
│  GPIO8 ───┐                                        │ │
│  GPIO9 ───┼───[ MIDI CIRCUIT ]                     │ │
│  GND ─────┘                                        │ │
│                                                   │ │
│  GPIO23 ─────────[ LED STRIP ]────────────────────┘ │
│                                                     │
│  [OTHER GPIO] ────[ BUTTON MATRIX ]                │
│                                                     │
└─────────────────────────────────────────────────────┘
```

## Adding New Hardware

### 1. Define Pins

Add to `src/config/pins.h`:
```cpp
// New hardware pins
#define NEW_DEVICE_PIN 26
#define NEW_DEVICE_IRQ_PIN 27
```

### 2. Create Driver

Create files in `src/ui/hardware/driver/`:
```cpp
// new_device.h
class NewDevice {
public:
    NewDevice(uint8_t pin);
    void init();
    void update();
    int read();
private:
    uint8_t _pin;
};
```

### 3. Update CMakeLists.txt

Add to CMakeLists.txt if needed:
```cmake
target_link_libraries(genseq
    # ... existing libraries ...
    hardware_spi  # If using SPI
)
```

### 4. Integrate with UI

Add to UI controller:
```cpp
// In ui.cpp
NewDevice newDevice(NEW_DEVICE_PIN);

void updateUI() {
    newDevice.update();
    // ... other updates
}
```

## PIO Programs

### Encoder Reader PIO

Located at `src/ui/hardware/driver/encoder.pio`:

```pio
.program encoder_reader
.side_set 1 opt

; Read encoder state change
loop:
    in pins, 2        ; Read both encoder pins
    mov x, isr        ; Save to X
    jmp x != y, changed ; Check if changed
    jmp loop          ; No change, continue
    
changed:
    mov y, x          ; Save new state
    irq 0             ; Signal change
    jmp loop          ; Continue
```

### WS2812 LED PIO

Generated from `src/ui/hardware/driver/ws2812.pio`:
- Handles timing-critical LED data
- Uses DMA for efficient transfers
- Supports RGB color format

## Hardware Abstraction Layer

### Interfaces

All hardware implements standard interfaces:

```cpp
// Input devices
class IInput {
public:
    virtual void update() = 0;
    virtual bool hasChanged() = 0;
    virtual int getValue() = 0;
};

// Output devices
class IOutput {
public:
    virtual void update() = 0;
    virtual void setValue(int value) = 0;
};
```

### Hardware Configuration

Configuration in `src/ui/hardware/driver/HardwareConfig.h`:
```cpp
struct HardwareConfig {
    // Display settings
    static const uint8_t DISPLAY_WIDTH = 16;
    static const uint8_t DISPLAY_HEIGHT = 2;
    
    // LED settings
    static const uint16_t LED_COUNT = 32;
    static const uint8_t LED_BRIGHTNESS = 128;
    
    // Debounce settings
    static const uint8_t DEBOUNCE_MS = 50;
};
```

## Power Considerations

### Power Requirements

- **Pico Board**: 40mA @ 5V
- **LCD Display**: 20mA @ 5V
- **LED Strip**: 60mA per LED @ 5V (full brightness white)
- **MIDI Interface**: 5mA @ 5V
- **Total**: ~2A @ 5V with all LEDs at full brightness

### Power Supply Recommendations

1. **USB Power**: Sufficient for development (500mA)
2. **External Supply**: Required for full LED usage
3. **Decoupling**: 100µF capacitor near LED strip
4. **Protection**: 500mA polyfuse on 5V line

## Signal Integrity

### I2C Considerations

- Pull-up resistors: 4.7kΩ on SDA/SCL
- Cable length: < 1m recommended
- Speed: 100kHz (standard mode)

### MIDI Considerations

- Isolation: Opto-isolator on input
- Termination: Not required for short cables
- Cable length: Up to 15m with proper shielding

### GPIO Considerations

- 3.3V logic levels
- Input protection: Series resistors for external signals
- Output current: Max 16mA per pin

### ADC Considerations

- RC low-pass filter (1kΩ + 100nF) on potentiometer input to reduce high-frequency noise
- Cutoff frequency: ~1.6 kHz
- Source impedance kept below RP2040 recommended maximum (~100kΩ)

## Troubleshooting Hardware

### Common Issues

1. **Display not working**:
   - Check I2C address (use I2C scanner)
   - Verify pull-up resistors
   - Check wiring connections

2. **Encoder jitter**:
   - Add hardware debouncing (0.1µF capacitor)
   - Increase software debounce time
   - Check for noise sources

3. **MIDI not sending**:
   - Verify MIDI wiring
   - Check baud rate (31250)
   - Test with MIDI monitor

4. **LED flickering**:
   - Check power supply
   - Verify ground connection
   - Reduce brightness

### Testing Procedures

1. **Continuity Test**: Verify all connections
2. **Voltage Test**: Check power at each component
3. **Signal Test**: Use logic analyzer for timing
4. **Isolation Test**: Test components individually

## Future Hardware Expansion

### Planned Additions

1. **Storage**: SD card for pattern storage
2. **Connectivity**: USB MIDI host mode
3. **Expansion**: More buttons and LEDs
4. **Sensors**: CV inputs for modulation

### Pin Availability

Free pins for expansion:
- GPIO26, GPIO27, GPIO28, GPIO29
- ADC pins available for analog input

## Summary

The hardware configuration is designed for:
- Clean signal routing
- Proper power distribution
- Easy expansion
- Reliable operation

All pin assignments are documented and easily modifiable in the source code. The hardware abstraction layer makes it simple to add new components or modify existing ones.
