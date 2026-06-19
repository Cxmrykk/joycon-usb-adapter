#include "tusb.h"
#include "usb_descriptors.h"
#include <string.h>

// Define the interface layout for a Composite Device
enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_HID,
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN)

// Endpoint mapping
#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82
#define EPNUM_HID         0x83

uint8_t const desc_hid_report[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Gamepad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    // 16 Buttons
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (1)
    0x29, 0x10,        //   Usage Maximum (16)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x95, 0x10,        //   Report Count (16)
    0x75, 0x01,        //   Report Size (1)
    0x81, 0x02,        //   Input (Data, Var, Abs)
    // Hat Switch (D-Pad)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x39,        //   Usage (Hat switch)
    0x15, 0x01,        //   Logical Minimum (1)
    0x25, 0x08,        //   Logical Maximum (8)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x65, 0x14,        //   Unit (Eng Rot:Angular Pos)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x42,        //   Input (Data, Var, Abs, Null State)
    0x75, 0x04,        //   Report Size (4) padding
    0x95, 0x01,        //   Report Count (1) padding
    0x81, 0x03,        //   Input (Const, Var, Abs)
    // 4 Analog Axes
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x32,        //   Usage (Z)
    0x09, 0x35,        //   Usage (Rz)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x0F,  //   Logical Maximum (4095)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data, Var, Abs)
    0xC0               // End Collection
};

tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0xEF, // MISC Class (Required for Composite Devices)
    .bDeviceSubClass    = 0x02, 
    .bDeviceProtocol    = 0x01, // Interface Association Descriptor (IAD)
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0xCafe, 
    .idProduct          = 0x4001, // Changed PID so PC sees it as a "new" device
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x80, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 5, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 1)
};

char const* string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: English
    "Custom",                   // 1: Manufacturer
    "Joy-Con Bridge",           // 2: Product
    "123456",                   // 3: Serial
    "CDC Serial Debug",         // 4: CDC Interface Name
    "HID Gamepad"               // 5: HID Interface Name
};

uint8_t const* tud_descriptor_device_cb(void) { return (uint8_t const*) &desc_device; }
uint8_t const* tud_descriptor_configuration_cb(uint8_t index) { return desc_configuration; }
uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance) { return desc_hid_report; }

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    static uint16_t _desc_str[32];
    uint8_t chr_count = (index == 0) ? 1 : strlen(string_desc_arr[index]);
    if (index == 0) memcpy(&_desc_str[1], string_desc_arr[0], 2);
    else for (uint8_t i = 0; i < chr_count; i++) _desc_str[1 + i] = string_desc_arr[index][i];
    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
    return _desc_str;
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) { return 0; }
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {}

// Missing function restored here:
void usb_send_gamepad_report(custom_gamepad_report_t *report) {
    if (tud_hid_ready()) {
        tud_hid_report(1, report, sizeof(custom_gamepad_report_t));
    }
}