#include "joycon_translator.h"
#include "usb_descriptors.h"
#include "tusb.h"

// The single, merged gamepad state
static custom_gamepad_report_t combined_report;

// Flag to decouple BT reception from USB transmission
static bool report_pending = false;

void joycon_init(void) {
    // Center the sticks explicitly on boot
    combined_report.buttons = 0;
    combined_report.hat = 0; 
    combined_report.x = 2048;
    combined_report.y = 2048;
    combined_report.z = 2048;
    combined_report.rz = 2048;
    report_pending = false;
}

void joycon_task(void) {
    // Only attempt to send if a report is pending AND the USB endpoint is ready
    if (report_pending && tud_hid_ready()) {
        usb_send_gamepad_report(&combined_report);
        report_pending = false;
    }
}

void joycon_parse_l2cap_report(const uint8_t* data, bool is_left) {
    // 0xA1 is HID DATA input header.
    if (data[0] != 0xA1) return;

    // Joy-Cons blast 0x3F (Simple HID mode) reports during initialization 
    if (data[1] == 0x3F) return;

    // Report ID 0x30 is Standard Full Mode (60Hz tracking data).
    if (data[1] != 0x30) return;

    // Fixed absolute offsets for the 0x30 report payload
    uint8_t right_btns = data[4];
    uint8_t shared_btns = data[5];
    uint8_t left_btns = data[6];

    if (is_left) {
        // Left Stick (12-bit)
        uint16_t lx = data[7] | ((data[8] & 0x0F) << 8);
        uint16_t ly = (data[8] >> 4) | (data[9] << 4);
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

        // --- Left Joy-Con Mapping to PC Layout ---
        if (left_btns & 0x40) combined_report.buttons |= (1 << 6);   else combined_report.buttons &= ~(1 << 6);   // L -> Bit 6 (USB_BTN_TL)
        if (left_btns & 0x80) combined_report.buttons |= (1 << 8);   else combined_report.buttons &= ~(1 << 8);   // ZL -> Bit 8 (USB_BTN_TL2)
        
        if (shared_btns & 0x01) combined_report.buttons |= (1 << 10); else combined_report.buttons &= ~(1 << 10); // Minus -> Bit 10 (Select)
        if (shared_btns & 0x08) combined_report.buttons |= (1 << 13); else combined_report.buttons &= ~(1 << 13); // L3 -> Bit 13 (ThumbL)
        if (shared_btns & 0x20) combined_report.buttons |= (1 << 19); else combined_report.buttons &= ~(1 << 19); // Capture -> Bit 19
        
        // SL / SR Inner Rail Buttons
        if (left_btns & 0x20) combined_report.buttons |= (1 << 15);  else combined_report.buttons &= ~(1 << 15);  // Left SL -> Bit 15
        if (left_btns & 0x10) combined_report.buttons |= (1 << 16);  else combined_report.buttons &= ~(1 << 16);  // Left SR -> Bit 16

    } else {
        // Right Stick (12-bit)
        uint16_t rx = data[10] | ((data[11] & 0x0F) << 8);
        uint16_t ry = (data[11] >> 4) | (data[12] << 4);
        combined_report.z = rx;
        combined_report.rz = 4095 - ry; // Invert Y

        // --- Right Joy-Con Mapping to PC Layout (With Rotation) ---
        // PC A (Bottom/South) = Joy-Con B
        if (right_btns & 0x04) combined_report.buttons |= (1 << 0); else combined_report.buttons &= ~(1 << 0); 
        // PC B (Right/East) = Joy-Con A
        if (right_btns & 0x08) combined_report.buttons |= (1 << 1); else combined_report.buttons &= ~(1 << 1); 
        // PC X (Left/West) = Joy-Con Y
        if (right_btns & 0x01) combined_report.buttons |= (1 << 3); else combined_report.buttons &= ~(1 << 3); 
        // PC Y (Top/North) = Joy-Con X
        if (right_btns & 0x02) combined_report.buttons |= (1 << 4); else combined_report.buttons &= ~(1 << 4); 
        
        if (right_btns & 0x40) combined_report.buttons |= (1 << 7);  else combined_report.buttons &= ~(1 << 7);  // R -> Bit 7 (USB_BTN_TR)
        if (right_btns & 0x80) combined_report.buttons |= (1 << 9);  else combined_report.buttons &= ~(1 << 9);  // ZR -> Bit 9 (USB_BTN_TR2)
        
        if (shared_btns & 0x02) combined_report.buttons |= (1 << 11); else combined_report.buttons &= ~(1 << 11); // Plus -> Bit 11 (Start)
        if (shared_btns & 0x04) combined_report.buttons |= (1 << 14); else combined_report.buttons &= ~(1 << 14); // R3 -> Bit 14 (ThumbR)
        if (shared_btns & 0x10) combined_report.buttons |= (1 << 12); else combined_report.buttons &= ~(1 << 12); // Home -> Bit 12 (Mode)

        // SL / SR Inner Rail Buttons
        if (right_btns & 0x10) combined_report.buttons |= (1 << 17); else combined_report.buttons &= ~(1 << 17); // Right SL -> Bit 17
        if (right_btns & 0x20) combined_report.buttons |= (1 << 18); else combined_report.buttons &= ~(1 << 18); // Right SR -> Bit 18
    }

    // Flag the report to be dispatched safely on the next run-loop poll tick
    report_pending = true;
}