#ifndef GENSEQ_PINS_H
#define GENSEQ_PINS_H

// PIN ASSIGNMENTS & CONFIG

#define LED_PIN 25
#define LED_MATRIX_PIN 16

// UART MIDI
#define MIDI_UART uart1    // Using UART1 for MIDI
#define MIDI_UART_PIN_TX 8 // MIDI_UART_TX
#define MIDI_UART_PIN_RX 9 // MIDI_UART_RX

// External FRAM config store (MB85RS2MTA, task 9.3/10.3). SPI1, kept OFF the
// QSPI/XIP bus so a save is ordinary SPI traffic that never disables XIP (the
// whole reason the FRAM save is transparent vs the internal-flash freeze).
// Verified clash-free against MIDI (GP8/9), LED matrix (GP16) and the LCD I2C.
#define FRAM_SPI       spi1
#define FRAM_SPI_PIN_SCK  10 // SPI1 SCK  -> FRAM SCK
#define FRAM_SPI_PIN_MOSI 11 // SPI1 TX   -> FRAM SI  (MOSI)
#define FRAM_SPI_PIN_MISO 12 // SPI1 RX   <- FRAM SO  (MISO)
#define FRAM_SPI_PIN_CS   13 // software CS (active low)

#endif // GENSEQ_PINS_H
