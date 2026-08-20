#include "UsbKeyboard.h"

#include "tusb.h"
#include "pico/time.h"
#include "../state/StateManager.h"
#include "../KeyNames.h"
#include <cstdio>

namespace hardware {
namespace {

UsbKeyboard* g_instance = nullptr;

uint32_t nowMs() { return to_ms_since_boot(get_absolute_time()); }

} // namespace

UsbKeyboard::UsbKeyboard()
    : decoder([](const ui::events::Event& event) {
          if (event.type == ui::events::EventType::KEY_PRESSED ||
              event.type == ui::events::EventType::KEY_RELEASED) {
              printf("%s %s (mods 0x%02x)\n",
                     event.type == ui::events::EventType::KEY_PRESSED ? "pressed" : "released",
                     ui::toName(event.data.key.id),
                     static_cast<unsigned>(event.data.key.mods));
          }
          ui::state::getStateManager().dispatch(event);
      })
{
    g_instance = this;
}

UsbKeyboard* UsbKeyboard::instance() { return g_instance; }

void UsbKeyboard::initialize()
{
    // Boot protocol gives a fixed 8-byte report, so no descriptor parsing.
    tuh_hid_set_default_protocol(HID_PROTOCOL_BOOT);
    if (!tuh_init(0)) {
        printf("Failed to initialize USB host\n");
        return;
    }
    printf("USB host initialized - waiting for a keyboard\n");
}

void UsbKeyboard::update()
{
    tuh_task();
    decoder.tick(nowMs());
}

void UsbKeyboard::handleReport(const uint8_t* report, uint16_t len)
{
    if (len < sizeof(hid_keyboard_report_t)) return;

    const auto* kb = reinterpret_cast<const hid_keyboard_report_t*>(report);

    KeyReport converted{};
    converted.mods = kb->modifier;
    for (uint8_t i = 0; i < KeyboardDecoder::MAX_KEYS; ++i) {
        converted.keys[i] = kb->keycode[i];
    }

    decoder.onReport(converted, nowMs());
}

void UsbKeyboard::handleDisconnect() { decoder.onDisconnect(); }

} // namespace hardware

extern "C" {

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t idx,
                      uint8_t const* desc_report, uint16_t desc_len)
{
    (void)desc_report;
    (void)desc_len;

    if (tuh_hid_interface_protocol(dev_addr, idx) != HID_ITF_PROTOCOL_KEYBOARD) {
        printf("HID device mounted (addr %u idx %u) - not a keyboard, ignoring\n",
               static_cast<unsigned>(dev_addr), static_cast<unsigned>(idx));
        return;
    }

    printf("Keyboard mounted (addr %u idx %u)\n",
           static_cast<unsigned>(dev_addr), static_cast<unsigned>(idx));
    if (!tuh_hid_receive_report(dev_addr, idx)) {
        printf("Failed to arm HID report (addr %u idx %u)\n",
               static_cast<unsigned>(dev_addr), static_cast<unsigned>(idx));
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t idx)
{
    printf("HID device unmounted (addr %u idx %u)\n",
           static_cast<unsigned>(dev_addr), static_cast<unsigned>(idx));
    if (auto* keyboard = hardware::UsbKeyboard::instance()) {
        keyboard->handleDisconnect();
    }
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t idx,
                                uint8_t const* report, uint16_t len)
{
    if (tuh_hid_interface_protocol(dev_addr, idx) == HID_ITF_PROTOCOL_KEYBOARD) {
        if (auto* keyboard = hardware::UsbKeyboard::instance()) {
            keyboard->handleReport(report, len);
        }
    }

    // Re-arm: without this no further reports arrive.
    if (!tuh_hid_receive_report(dev_addr, idx)) {
        printf("Failed to re-arm HID report (addr %u idx %u)\n",
               static_cast<unsigned>(dev_addr), static_cast<unsigned>(idx));
    }
}

} // extern "C"
