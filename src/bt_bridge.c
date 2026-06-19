#include "bt_bridge.h"
#include <btstack.h>
#include <string.h>
#include "joycon_translator.h"

// Expose our custom USB Serial logger from main.c
extern void cdc_printf(const char *format, ...);

static uint16_t jc_left_ctrl_cid = 0, jc_left_intr_cid = 0;
static uint16_t jc_right_ctrl_cid = 0, jc_right_intr_cid = 0;
static bd_addr_t jc_left_addr, jc_right_addr;
static bool found_left = false, found_right = false;

static bool jc_left_handshake_pending = false;
static bool jc_right_handshake_pending = false;

static btstack_packet_callback_registration_t hci_event_callback_registration;

// Forward declare the handler so check_device_name can use it
static void bt_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

// Joy-Con handshake command to enable Standard Full Mode (0x30)
static const uint8_t JC_ENABLE_FULL_MODE[] = {
    0xA2, 0x01, 0x00, // HID DATA OUTPUT + Report 0x01 + Packet Counter
    0x00, 0x01, 0x40, 0x40, // Left Rumble Base
    0x00, 0x01, 0x40, 0x40, // Right Rumble Base
    0x03, 0x30 // Subcommand 0x03 (Set Input Mode), Arg 0x30 (Standard Full)
};

static void send_handshake(uint16_t cid) {
    l2cap_send(cid, (uint8_t*)JC_ENABLE_FULL_MODE, sizeof(JC_ENABLE_FULL_MODE));
    cdc_printf("-> Sent Standard Full Mode command to CID 0x%04x\n", cid);
}

static void check_device_name(bd_addr_t addr, const char* name) {
    if (strcmp(name, "Joy-Con (L)") == 0 && !found_left) {
        found_left = true;
        bd_addr_copy(jc_left_addr, addr);
        cdc_printf("Found Left Joy-Con: %s. Connecting...\n", bd_addr_to_str(addr));
        l2cap_create_channel(bt_packet_handler, addr, PSM_HID_CONTROL, 0xffff, &jc_left_ctrl_cid);
    } 
    else if (strcmp(name, "Joy-Con (R)") == 0 && !found_right) {
        found_right = true;
        bd_addr_copy(jc_right_addr, addr);
        cdc_printf("Found Right Joy-Con: %s. Connecting...\n", bd_addr_to_str(addr));
        l2cap_create_channel(bt_packet_handler, addr, PSM_HID_CONTROL, 0xffff, &jc_right_ctrl_cid);
    }
}

static void bt_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    if (packet_type == HCI_EVENT_PACKET) {
        switch (hci_event_packet_get_type(packet)) {
            
            case BTSTACK_EVENT_STATE:
                if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                    cdc_printf("Bluetooth Baseband Working. Scanning for Joy-Cons...\n");
                    // Start periodic scanning
                    gap_inquiry_periodic_start(0x03, 0x05, 0x04); 
                }
                break;

            case GAP_EVENT_INQUIRY_RESULT: {
                bd_addr_t addr;
                gap_event_inquiry_result_get_bd_addr(packet, addr);
                
                if (gap_event_inquiry_result_get_name_available(packet)) {
                    char name[32];
                    int len = gap_event_inquiry_result_get_name_len(packet);
                    memcpy(name, gap_event_inquiry_result_get_name(packet), len);
                    name[len] = 0;
                    check_device_name(addr, name);
                } else {
                    // Joy-Cons do NOT broadcast their name in the EIR packet during pairing mode.
                    // We must filter by Peripheral Class of Device (0x0500) and manually request the name.
                    uint32_t cod = gap_event_inquiry_result_get_class_of_device(packet);
                    if ((cod & 0x1F00) == 0x0500) {
                        uint8_t page_scan_rep_mode = gap_event_inquiry_result_get_page_scan_repetition_mode(packet);
                        uint16_t clock_offset = gap_event_inquiry_result_get_clock_offset(packet) | 0x8000;
                        gap_remote_name_request(addr, page_scan_rep_mode, clock_offset);
                    }
                }
                break;
            }

            case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE: {
                bd_addr_t addr;
                hci_event_remote_name_request_complete_get_bd_addr(packet, addr);
                if (hci_event_remote_name_request_complete_get_status(packet) == 0) {
                    // BTstack guarantees this string is safely null-terminated
                    const char* name = (const char*)hci_event_remote_name_request_complete_get_remote_name(packet);
                    check_device_name(addr, name);
                }
                break;
            }

            // --- SECURITY AND PAIRING EVENTS ---
            case HCI_EVENT_PIN_CODE_REQUEST: {
                bd_addr_t event_addr;
                hci_event_pin_code_request_get_bd_addr(packet, event_addr);
                cdc_printf("PIN Code Requested for %s. Replying with 0000...\n", bd_addr_to_str(event_addr));
                gap_pin_code_response_binary(event_addr, (uint8_t*)"0000", 4);
                break;
            }
            case HCI_EVENT_USER_CONFIRMATION_REQUEST: {
                bd_addr_t event_addr;
                hci_event_user_confirmation_request_get_bd_addr(packet, event_addr);
                cdc_printf("SSP User Confirmation Requested for %s. Auto-accepting...\n", bd_addr_to_str(event_addr));
                gap_ssp_confirmation_response(event_addr);
                break;
            }

            // --- CONNECTION EVENTS ---
            case L2CAP_EVENT_INCOMING_CONNECTION: {
                bd_addr_t addr;
                l2cap_event_incoming_connection_get_address(packet, addr);
                uint16_t local_cid = l2cap_event_incoming_connection_get_local_cid(packet);
                
                bool is_left = (bd_addr_cmp(addr, jc_left_addr) == 0);
                bool is_right = (bd_addr_cmp(addr, jc_right_addr) == 0);

                // If a sleeping Joy-Con wakes up and connects to us, assign it!
                if (!is_left && !is_right) {
                    if (!found_left) {
                        found_left = true;
                        bd_addr_copy(jc_left_addr, addr);
                        cdc_printf("Incoming HID connection assigned to Left Joy-Con: %s\n", bd_addr_to_str(addr));
                    } else if (!found_right) {
                        found_right = true;
                        bd_addr_copy(jc_right_addr, addr);
                        cdc_printf("Incoming HID connection assigned to Right Joy-Con: %s\n", bd_addr_to_str(addr));
                    } else {
                        cdc_printf("Unknown device %s tried to connect. Declining.\n", bd_addr_to_str(addr));
                        l2cap_decline_connection(local_cid);
                        break;
                    }
                }
                
                cdc_printf("Accepting L2CAP connection from %s...\n", bd_addr_to_str(addr));
                l2cap_accept_connection(local_cid);
                break;
            }

            case L2CAP_EVENT_CHANNEL_OPENED: {
                if (l2cap_event_channel_opened_get_status(packet)) {
                    cdc_printf("Failed to open L2CAP channel!\n");
                    return;
                }
                
                uint16_t psm = l2cap_event_channel_opened_get_psm(packet);
                bd_addr_t addr;
                l2cap_event_channel_opened_get_address(packet, addr);
                uint16_t local_cid = l2cap_event_channel_opened_get_local_cid(packet);
                bool incoming = l2cap_event_channel_opened_get_incoming(packet);

                bool is_left = (bd_addr_cmp(addr, jc_left_addr) == 0);

                if (psm == PSM_HID_CONTROL) {
                    cdc_printf("HID Control Opened for %s (Incoming: %d)\n", is_left ? "Left" : "Right", incoming);
                    if (is_left) jc_left_ctrl_cid = local_cid;
                    else jc_right_ctrl_cid = local_cid;
                    
                    // If we initiated the connection, we must also open the interrupt channel.
                    // If THEY initiated, we wait for them to open the interrupt channel.
                    if (!incoming) {
                        cdc_printf("Initiating HID Interrupt channel...\n");
                        l2cap_create_channel(bt_packet_handler, addr, PSM_HID_INTERRUPT, 0xffff, 
                            is_left ? &jc_left_intr_cid : &jc_right_intr_cid);
                    }
                } 
                else if (psm == PSM_HID_INTERRUPT) {
                    cdc_printf("HID Interrupt Opened for %s. Requesting handshake transmit.\n", is_left ? "Left" : "Right");
                    if (is_left) {
                        jc_left_intr_cid = local_cid;
                        jc_left_handshake_pending = true;
                    } else {
                        jc_right_intr_cid = local_cid;
                        jc_right_handshake_pending = true;
                    }
                    l2cap_request_can_send_now_event(local_cid);
                }
                break;
            }

            case L2CAP_EVENT_CAN_SEND_NOW: {
                uint16_t local_cid = l2cap_event_can_send_now_get_local_cid(packet);
                if (local_cid == jc_left_intr_cid && jc_left_handshake_pending) {
                    send_handshake(local_cid);
                    jc_left_handshake_pending = false;
                } else if (local_cid == jc_right_intr_cid && jc_right_handshake_pending) {
                    send_handshake(local_cid);
                    jc_right_handshake_pending = false;
                }
                break;
            }
        }
    } 
    else if (packet_type == L2CAP_DATA_PACKET) {
        if (channel == jc_left_intr_cid) joycon_parse_l2cap_report(packet, true);
        else if (channel == jc_right_intr_cid) joycon_parse_l2cap_report(packet, false);
    }
}

void bt_bridge_init(void) {
    // 1. Setup GAP Policies (Classic Bluetooth)
    gap_set_security_level(LEVEL_1); 
    gap_connectable_control(1);      // Accept incoming connections from sleeping Joy-Cons
    gap_discoverable_control(0);
    
    // 2. Setup Secure Simple Pairing (SSP)
    gap_ssp_set_enable(1);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_authentication_requirement(SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_GENERAL_BONDING);
    
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH | LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_page_scan_type(PAGE_SCAN_MODE_INTERLACED);

    hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);

    // 3. Initialize L2CAP and register services
    sdp_init(); 
    l2cap_init();
    
    l2cap_register_service(bt_packet_handler, PSM_HID_CONTROL, 0xffff, LEVEL_1);
    l2cap_register_service(bt_packet_handler, PSM_HID_INTERRUPT, 0xffff, LEVEL_1);
    
    // 4. Register Event Handler
    hci_event_callback_registration.callback = &bt_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // 5. Explicitly wake up the CYW43 Bluetooth Controller
    hci_power_control(HCI_POWER_ON);
}