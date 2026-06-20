#include "joycon_translator.h"
#include "usb_descriptors.h"
#include "tusb.h"

static custom_gamepad_report_t combined_report;
static bool report_pending = false;

void joycon_init(void) {
    combined_report.buttons = 0;
    combined_report.hat = 0; 
    combined_report.x = 2048;
    combined_report.y = 2048;
    combined_report.z = 2048;
    combined_report.rz = 2048;
    report_pending = false;
}

void joycon_task(void) {
    // If a burst of Bluetooth packets arrived while the 1ms USB poll endpoint
    // was locked, this drains the single, newest, coalesced state instantly.
    if (report_pending && tud_hid_ready()) {
        usb_send_gamepad_report(&combined_report);
        report_pending = false;
    }
}

void joycon_parse_l2cap_report(const uint8_t* data, bool is_left) {
    if (data[0] != 0xA1) return;
    if (data[1] == 0x3F) return;
    if (data[1] != 0x30) return;

    uint8_t right_btns = data[4];
    uint8_t shared_btns = data[5];
    uint8_t left_btns = data[6];

    if (is_left) {
        uint16_t lx = data[7] | ((data[8] & 0x0F) << 8);
        uint16_t ly = (data[8] >> 4) | (data[9] << 4);
        combined_report.x = lx;
        combined_report.y = (~ly) & 0x0FFF;

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
        else combined_report.hat = 0;

        if (left_btns & 0x40) combined_report.buttons |= (1 << 6);   else combined_report.buttons &= ~(1 << 6); 
        if (left_btns & 0x80) combined_report.buttons |= (1 << 8);   else combined_report.buttons &= ~(1 << 8); 
        if (shared_btns & 0x01) combined_report.buttons |= (1 << 10); else combined_report.buttons &= ~(1 << 10);
        if (shared_btns & 0x08) combined_report.buttons |= (1 << 13); else combined_report.buttons &= ~(1 << 13); 
        if (shared_btns & 0x20) combined_report.buttons |= (1 << 19); else combined_report.buttons &= ~(1 << 19); 
        if (left_btns & 0x20) combined_report.buttons |= (1 << 15);  else combined_report.buttons &= ~(1 << 15);  
        if (left_btns & 0x10) combined_report.buttons |= (1 << 16);  else combined_report.buttons &= ~(1 << 16);  

    } else {
        uint16_t rx = data[10] | ((data[11] & 0x0F) << 8);
        uint16_t ry = (data[11] >> 4) | (data[12] << 4);
        combined_report.z = rx;
        combined_report.rz = (~ry) & 0x0FFF;

        if (right_btns & 0x04) combined_report.buttons |= (1 << 0); else combined_report.buttons &= ~(1 << 0); 
        if (right_btns & 0x08) combined_report.buttons |= (1 << 1); else combined_report.buttons &= ~(1 << 1); 
        if (right_btns & 0x01) combined_report.buttons |= (1 << 3); else combined_report.buttons &= ~(1 << 3); 
        if (right_btns & 0x02) combined_report.buttons |= (1 << 4); else combined_report.buttons &= ~(1 << 4); 
        if (right_btns & 0x40) combined_report.buttons |= (1 << 7);  else combined_report.buttons &= ~(1 << 7);  
        if (right_btns & 0x80) combined_report.buttons |= (1 << 9);  else combined_report.buttons &= ~(1 << 9);  
        if (shared_btns & 0x02) combined_report.buttons |= (1 << 11); else combined_report.buttons &= ~(1 << 11);
        if (shared_btns & 0x04) combined_report.buttons |= (1 << 14); else combined_report.buttons &= ~(1 << 14);
        if (shared_btns & 0x10) combined_report.buttons |= (1 << 12); else combined_report.buttons &= ~(1 << 12); 
        if (right_btns & 0x10) combined_report.buttons |= (1 << 17); else combined_report.buttons &= ~(1 << 17); 
        if (right_btns & 0x20) combined_report.buttons |= (1 << 18); else combined_report.buttons &= ~(1 << 18); 
    }

    if (tud_hid_ready()) {
        usb_send_gamepad_report(&combined_report);
        report_pending = false; 
    } else {
        // Keeps intermediate frames dropping, only preserving the absolute newest frame.
        report_pending = true;
    }
}