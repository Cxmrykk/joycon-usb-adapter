#pragma once
#include <stdint.h>

// Custom HID struct matching our descriptor
typedef struct __attribute__((packed)) {
    uint32_t buttons; // 32 Buttons
    uint8_t  hat;     // 8-way D-pad (0 = centered)
    uint16_t x;       // Left Stick X (0-4095)
    uint16_t y;       // Left Stick Y (0-4095)
    uint16_t z;       // Right Stick X (0-4095) - Reverted to Z for DirectInput compatibility
    uint16_t rz;      // Right Stick Y (0-4095) - Reverted to Rz for DirectInput compatibility
} custom_gamepad_report_t;

void usb_send_gamepad_report(custom_gamepad_report_t *report);