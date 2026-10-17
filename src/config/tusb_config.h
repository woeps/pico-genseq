#pragma once

// CFG_TUSB_MCU, CFG_TUSB_OS and CFG_TUSB_DEBUG are supplied by the Pico SDK's
// tinyusb_common_base target. Defining them here causes redefinition errors.

#define CFG_TUH_ENABLED     1
#define CFG_TUH_RPI_PIO_USB 0   // native USB port, not Pico-PIO-USB

#define CFG_TUH_HUB         1   // keyboards are frequently behind a hub
#define CFG_TUH_HID         4   // HID interfaces, not devices
#define CFG_TUH_CDC         0
#define CFG_TUH_MSC         0
#define CFG_TUH_VENDOR      0

#define CFG_TUH_DEVICE_MAX  (CFG_TUH_HUB ? 5 : 4)
#define CFG_TUH_ENUMERATION_BUFSIZE 256
