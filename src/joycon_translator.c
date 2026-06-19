#include "joycon_translator.h"
#include "usb_descriptors.h"

// The single, merged gamepad state
static custom_gamepad_report_t combined_report;

void joycon_init(void) {
    // Center the sticks explicitly on boot
    combined_report.buttons = 0;
    combined_report.hat = 0; // Centered
    combined_report.x = 2048;
    combined_report.y = 2048;
    combined_report.z = 2048;
    combined_report.rz = 2048;
}

void joycon_parse_l2cap_report(const uint8_t* data, bool is_left) {
    // 0xA1 is HID DATA input header. Report ID 0x30 is Standard Full Mode
    if (data[0] != 0xA1 || data[1] != 0x30) return;

    const uint8_t* report = &data[2]; // Shift to actual payload
    uint8_t right_btns = report[3];
    uint8_t shared_btns = report[4];
    uint8_t left_btns = report[5];

    if (is_left) {
        // Left Stick (12-bit)
        uint16_t lx = report[6] | ((report[7] & 0x0F) << 8);
        uint16_t ly = (report[7] >> 4) | (report[8] << 4);
        combined_report.x = lx;
        combined_report.y = 4095 - ly; // Invert Y so UP is 0 in standard HID

        // D-Pad to Hat Switch Conversion
        bool d_up = left_btns & 0x02, d_down = left_btns & 0x01;
        bool d_left = left_btns & 0x08, d_right = left_btns & 0x04;
        if (d_up && d_right) combined_report.hat = 2;
        else if (d_up && d_left) combined_report.hat = 8;
        else if (d_down && d_right) combined_report.hat = 4;
        else if (d_down && d_left) combined_report.hat = 6;
        else if (d_up) combined_report.hat = 1;
        else if (d_down) combined_report.hat = 5;
        else if (d_left) combined_report.hat = 7;
        else if (d_right) combined_report.hat = 3;
        else combined_report.hat = 0; // Centered

        // Left Buttons
        if (left_btns & 0x40) combined_report.buttons |= (1 << 4); // L
        else combined_report.buttons &= ~(1 << 4);
        if (left_btns & 0x80) combined_report.buttons |= (1 << 6); // ZL
        else combined_report.buttons &= ~(1 << 6);
        if (left_btns & 0x20) combined_report.buttons |= (1 << 14); // SL
        else combined_report.buttons &= ~(1 << 14);
        if (shared_btns & 0x01) combined_report.buttons |= (1 << 8); // Minus
        else combined_report.buttons &= ~(1 << 8);
        if (shared_btns & 0x08) combined_report.buttons |= (1 << 10); // L3
        else combined_report.buttons &= ~(1 << 10);
        if (shared_btns & 0x20) combined_report.buttons |= (1 << 13); // Capture
        else combined_report.buttons &= ~(1 << 13);

    } else {
        // Right Stick (12-bit)
        uint16_t rx = report[9] | ((report[10] & 0x0F) << 8);
        uint16_t ry = (report[10] >> 4) | (report[11] << 4);
        combined_report.z = rx;
        combined_report.rz = 4095 - ry; // Invert Y

        // Right Buttons
        if (right_btns & 0x08) combined_report.buttons |= (1 << 0); // A
        else combined_report.buttons &= ~(1 << 0);
        if (right_btns & 0x04) combined_report.buttons |= (1 << 1); // B
        else combined_report.buttons &= ~(1 << 1);
        if (right_btns & 0x01) combined_report.buttons |= (1 << 2); // X
        else combined_report.buttons &= ~(1 << 2);
        if (right_btns & 0x02) combined_report.buttons |= (1 << 3); // Y
        else combined_report.buttons &= ~(1 << 3);
        if (right_btns & 0x40) combined_report.buttons |= (1 << 5); // R
        else combined_report.buttons &= ~(1 << 5);
        if (right_btns & 0x80) combined_report.buttons |= (1 << 7); // ZR
        else combined_report.buttons &= ~(1 << 7);
        if (right_btns & 0x10) combined_report.buttons |= (1 << 15); // SR
        else combined_report.buttons &= ~(1 << 15);
        if (shared_btns & 0x02) combined_report.buttons |= (1 << 9); // Plus
        else combined_report.buttons &= ~(1 << 9);
        if (shared_btns & 0x04) combined_report.buttons |= (1 << 11); // R3
        else combined_report.buttons &= ~(1 << 11);
        if (shared_btns & 0x10) combined_report.buttons |= (1 << 12); // Home
        else combined_report.buttons &= ~(1 << 12);
    }

    // Instantly push state to PC
    usb_send_gamepad_report(&combined_report);
}