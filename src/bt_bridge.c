#include "bt_bridge.h"
#include <btstack.h>
#include <string.h>
#include "joycon_translator.h"

static uint16_t jc_left_ctrl_cid = 0, jc_left_intr_cid = 0;
static uint16_t jc_right_ctrl_cid = 0, jc_right_intr_cid = 0;
static bd_addr_t jc_left_addr, jc_right_addr;
static bool found_left = false, found_right = false;

// Flags to safely queue the handshake transmission
static bool jc_left_handshake_pending = false;
static bool jc_right_handshake_pending = false;

static btstack_packet_callback_registration_t hci_event_callback_registration;

// Joy-Con handshake command to enable Standard Full Mode (0x30)
static const uint8_t JC_ENABLE_FULL_MODE[] = {
    0xA2, 0x01, 0x00, // HID DATA OUTPUT + Report 0x01 + Packet Counter
    0x00, 0x01, 0x40, 0x40, // Left Rumble Base
    0x00, 0x01, 0x40, 0x40, // Right Rumble Base
    0x03, 0x30 // Subcommand 0x03 (Set Input Mode), Arg 0x30 (Standard Full)
};

static void send_handshake(uint16_t cid) {
    l2cap_send(cid, (uint8_t*)JC_ENABLE_FULL_MODE, sizeof(JC_ENABLE_FULL_MODE));
}

static void bt_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    if (packet_type == HCI_EVENT_PACKET) {
        switch (hci_event_packet_get_type(packet)) {
            
            case BTSTACK_EVENT_STATE:
                if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
                    gap_inquiry_start(0); // Start scanning indefinitely
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

                    if (strcmp(name, "Joy-Con (L)") == 0 && !found_left) {
                        found_left = true;
                        bd_addr_copy(jc_left_addr, addr);
                        l2cap_create_channel(bt_packet_handler, addr, PSM_HID_CONTROL, 0xffff, &jc_left_ctrl_cid);
                    } 
                    else if (strcmp(name, "Joy-Con (R)") == 0 && !found_right) {
                        found_right = true;
                        bd_addr_copy(jc_right_addr, addr);
                        l2cap_create_channel(bt_packet_handler, addr, PSM_HID_CONTROL, 0xffff, &jc_right_ctrl_cid);
                    }
                }
                break;
            }

            case L2CAP_EVENT_INCOMING_CONNECTION: {
                // CRITICAL FIX: The `channel` parameter is always 0 here.
                // We must extract the actual local CID from the packet to accept it!
                uint16_t local_cid = l2cap_event_incoming_connection_get_local_cid(packet);
                l2cap_accept_connection(local_cid);
                break;
            }

            case L2CAP_EVENT_CHANNEL_OPENED: {
                if (l2cap_event_channel_opened_get_status(packet)) return;
                
                uint16_t psm = l2cap_event_channel_opened_get_psm(packet);
                bd_addr_t addr;
                l2cap_event_channel_opened_get_address(packet, addr);
                uint16_t local_cid = l2cap_event_channel_opened_get_local_cid(packet);

                bool is_left = (bd_addr_cmp(addr, jc_left_addr) == 0);

                if (psm == PSM_HID_CONTROL) {
                    if (is_left) jc_left_ctrl_cid = local_cid;
                    else jc_right_ctrl_cid = local_cid;
                    
                    // Proceed to open Interrupt channel
                    l2cap_create_channel(bt_packet_handler, addr, PSM_HID_INTERRUPT, 0xffff, 
                        is_left ? &jc_left_intr_cid : &jc_right_intr_cid);
                } 
                else if (psm == PSM_HID_INTERRUPT) {
                    if (is_left) {
                        jc_left_intr_cid = local_cid;
                        jc_left_handshake_pending = true;
                    } else {
                        jc_right_intr_cid = local_cid;
                        jc_right_handshake_pending = true;
                    }
                    // CRITICAL FIX: Do not fire l2cap_send() blindly. Request permission.
                    l2cap_request_can_send_now_event(local_cid);
                }
                break;
            }

            case L2CAP_EVENT_CAN_SEND_NOW: {
                // Fired when the BTstack baseband buffer has space to safely write our handshake
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
    l2cap_init();
    l2cap_register_service(bt_packet_handler, PSM_HID_CONTROL, 0xffff, LEVEL_2);
    l2cap_register_service(bt_packet_handler, PSM_HID_INTERRUPT, 0xffff, LEVEL_2);
    hci_event_callback_registration.callback = &bt_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // CRITICAL FIX: Explicitly wake up the CYW43 Bluetooth Controller
    hci_power_control(HCI_POWER_ON);
}