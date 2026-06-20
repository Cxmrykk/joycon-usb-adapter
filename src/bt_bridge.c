#include "bt_bridge.h"
#include <btstack.h>
#include <string.h>
#include "joycon_translator.h"

extern void cdc_printf(const char *format, ...);

typedef enum {
    JC_STATE_NONE = 0,
    JC_STATE_REQ_INFO,
    JC_STATE_SET_MODE,
    JC_STATE_SET_LED,
    JC_STATE_RUNNING
} jc_state_t;

static uint16_t jc_left_ctrl_cid = 0, jc_left_intr_cid = 0;
static uint16_t jc_right_ctrl_cid = 0, jc_right_intr_cid = 0;
static bd_addr_t jc_left_addr, jc_right_addr;
static bool found_left = false, found_right = false;

static jc_state_t jc_left_state = JC_STATE_NONE;
static jc_state_t jc_right_state = JC_STATE_NONE;
static bool jc_left_pending = false;
static bool jc_right_pending = false;

static uint8_t packet_counter = 0;

static btstack_packet_callback_registration_t hci_event_callback_registration;
static void bt_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

// --- State Machine Helpers ---

static void jc_advance_state(bool is_left, jc_state_t next_state) {
    if (is_left) {
        jc_left_state = next_state;
        jc_left_pending = true;
        if (jc_left_intr_cid) l2cap_request_can_send_now_event(jc_left_intr_cid);
    } else {
        jc_right_state = next_state;
        jc_right_pending = true;
        if (jc_right_intr_cid) l2cap_request_can_send_now_event(jc_right_intr_cid);
    }
}

static void send_subcmd(uint16_t cid, uint8_t subcmd, const uint8_t *args, uint8_t arg_len) {
    uint8_t buf[64];
    buf[0] = 0xA2; // HID DATA OUTPUT
    buf[1] = 0x01; // Report ID
    buf[2] = packet_counter++;
    if (packet_counter > 0x0F) packet_counter = 0;
    
    // 8 bytes rumble data (neutral)
    uint8_t rumble[8] = {0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
    memcpy(&buf[3], rumble, 8);
    
    buf[11] = subcmd;
    if (arg_len > 0 && args != NULL) {
        memcpy(&buf[12], args, arg_len);
    }
    
    l2cap_send(cid, buf, 12 + arg_len);
    cdc_printf("-> Sent Subcmd 0x%02x to CID 0x%04x\n", subcmd, cid);
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

    // STRICT RADIO LOCKDOWN: Stop scanning & connectability to dedicate antenna to HID data
    if (found_left && found_right) {
        cdc_printf("Both Joy-Cons found! Locking down radio bandwidth.\n");
        gap_inquiry_stop();
        gap_connectable_control(0);
        gap_discoverable_control(0);
    }
}

static void bt_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    if (packet_type == HCI_EVENT_PACKET) {
        switch (hci_event_packet_get_type(packet)) {
            
            case BTSTACK_EVENT_STATE:
                if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                    cdc_printf("Bluetooth Baseband Working. Scanning for Joy-Cons...\n");
                    // CRITICAL FIX: Use manual 1-shot inquiry instead of periodic background sweeps
                    gap_inquiry_start(0x05); 
                }
                break;

            case GAP_EVENT_INQUIRY_COMPLETE:
                // If the inquiry finished but we don't have both, manually fire it again.
                // This gives us complete control over when the radio is allowed to scan.
                if (!found_left || !found_right) {
                    gap_inquiry_start(0x05);
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
                    const char* name = (const char*)hci_event_remote_name_request_complete_get_remote_name(packet);
                    check_device_name(addr, name);
                }
                break;
            }

            case HCI_EVENT_PIN_CODE_REQUEST: {
                bd_addr_t event_addr;
                hci_event_pin_code_request_get_bd_addr(packet, event_addr);
                gap_pin_code_response_binary(event_addr, (uint8_t*)"0000", 4);
                break;
            }

            case HCI_EVENT_USER_CONFIRMATION_REQUEST: {
                bd_addr_t event_addr;
                hci_event_user_confirmation_request_get_bd_addr(packet, event_addr);
                gap_ssp_confirmation_response(event_addr);
                break;
            }

            case L2CAP_EVENT_INCOMING_CONNECTION: {
                bd_addr_t addr;
                l2cap_event_incoming_connection_get_address(packet, addr);
                uint16_t local_cid = l2cap_event_incoming_connection_get_local_cid(packet);
                
                bool is_left = (bd_addr_cmp(addr, jc_left_addr) == 0);
                bool is_right = (bd_addr_cmp(addr, jc_right_addr) == 0);

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
                        l2cap_decline_connection(local_cid);
                        break;
                    }
                }
                
                if (found_left && found_right) {
                    gap_inquiry_stop();
                    gap_connectable_control(0);
                }

                l2cap_accept_connection(local_cid);
                break;
            }

            case L2CAP_EVENT_CHANNEL_OPENED: {
                if (l2cap_event_channel_opened_get_status(packet)) return;
                
                uint16_t psm = l2cap_event_channel_opened_get_psm(packet);
                bd_addr_t addr;
                l2cap_event_channel_opened_get_address(packet, addr);
                uint16_t local_cid = l2cap_event_channel_opened_get_local_cid(packet);
                bool incoming = l2cap_event_channel_opened_get_incoming(packet);
                bool is_left = (bd_addr_cmp(addr, jc_left_addr) == 0);

                if (psm == PSM_HID_CONTROL) {
                    if (is_left) jc_left_ctrl_cid = local_cid;
                    else jc_right_ctrl_cid = local_cid;
                    
                    if (!incoming) {
                        l2cap_create_channel(bt_packet_handler, addr, PSM_HID_INTERRUPT, 0xffff, 
                            is_left ? &jc_left_intr_cid : &jc_right_intr_cid);
                    }
                } 
                else if (psm == PSM_HID_INTERRUPT) {
                    if (is_left) jc_left_intr_cid = local_cid;
                    else jc_right_intr_cid = local_cid;
                    jc_advance_state(is_left, JC_STATE_REQ_INFO);
                }
                break;
            }
            
            case L2CAP_EVENT_CHANNEL_CLOSED: {
                uint16_t local_cid = l2cap_event_channel_closed_get_local_cid(packet);
                if (local_cid == jc_left_ctrl_cid || local_cid == jc_left_intr_cid) {
                    cdc_printf("Left Joy-Con disconnected. Restarting scan...\n");
                    found_left = false;
                    jc_left_state = JC_STATE_NONE;
                    jc_left_ctrl_cid = 0; jc_left_intr_cid = 0;
                    gap_connectable_control(1);
                    gap_inquiry_start(0x05);
                } 
                else if (local_cid == jc_right_ctrl_cid || local_cid == jc_right_intr_cid) {
                    cdc_printf("Right Joy-Con disconnected. Restarting scan...\n");
                    found_right = false;
                    jc_right_state = JC_STATE_NONE;
                    jc_right_ctrl_cid = 0; jc_right_intr_cid = 0;
                    gap_connectable_control(1);
                    gap_inquiry_start(0x05);
                }
                break;
            }

            case L2CAP_EVENT_CAN_SEND_NOW: {
                uint16_t local_cid = l2cap_event_can_send_now_get_local_cid(packet);
                
                if (local_cid == jc_left_intr_cid && jc_left_pending) {
                    jc_left_pending = false;
                    if (jc_left_state == JC_STATE_REQ_INFO) {
                        send_subcmd(local_cid, 0x02, NULL, 0);
                    } else if (jc_left_state == JC_STATE_SET_MODE) {
                        uint8_t arg = 0x30; 
                        send_subcmd(local_cid, 0x03, &arg, 1);
                    } else if (jc_left_state == JC_STATE_SET_LED) {
                        uint8_t arg = 0x01; 
                        send_subcmd(local_cid, 0x30, &arg, 1);
                    }
                } 
                else if (local_cid == jc_right_intr_cid && jc_right_pending) {
                    jc_right_pending = false;
                    if (jc_right_state == JC_STATE_REQ_INFO) {
                        send_subcmd(local_cid, 0x02, NULL, 0);
                    } else if (jc_right_state == JC_STATE_SET_MODE) {
                        uint8_t arg = 0x30;
                        send_subcmd(local_cid, 0x03, &arg, 1);
                    } else if (jc_right_state == JC_STATE_SET_LED) {
                        uint8_t arg = 0x02; 
                        send_subcmd(local_cid, 0x30, &arg, 1);
                    }
                }
                break;
            }
        }
    } 
    else if (packet_type == L2CAP_DATA_PACKET) {
        bool is_left = (channel == jc_left_intr_cid);
        bool is_right = (channel == jc_right_intr_cid);

        if (is_left || is_right) {
            uint8_t report_id = packet[1]; 
            
            // Subcommand ACK
            if (report_id == 0x21 && size >= 16) {
                uint8_t subcmd_replied = packet[15];
                jc_state_t current_state = is_left ? jc_left_state : jc_right_state;
                
                if (current_state == JC_STATE_REQ_INFO && subcmd_replied == 0x02) {
                    jc_advance_state(is_left, JC_STATE_SET_MODE);
                } else if (current_state == JC_STATE_SET_MODE && subcmd_replied == 0x03) {
                    jc_advance_state(is_left, JC_STATE_SET_LED);
                } else if (current_state == JC_STATE_SET_LED && subcmd_replied == 0x30) {
                    if (is_left) jc_left_state = JC_STATE_RUNNING;
                    else jc_right_state = JC_STATE_RUNNING;
                    cdc_printf("*** %s Joy-Con Fully Initialized and Running! ***\n", is_left ? "Left" : "Right");
                }
            } 
            else {
                // Pass standard controller reports to the translator
                joycon_parse_l2cap_report(packet, is_left);
            }
        }
    }
}

void bt_bridge_init(void) {
    gap_set_security_level(LEVEL_1); 
    gap_connectable_control(1);
    gap_discoverable_control(0);
    
    gap_ssp_set_enable(1);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_authentication_requirement(SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_GENERAL_BONDING);
    
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
    gap_set_page_scan_type(PAGE_SCAN_MODE_INTERLACED);

    hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);

    sdp_init(); 
    l2cap_init();
    
    l2cap_register_service(bt_packet_handler, PSM_HID_CONTROL, 0xffff, LEVEL_1);
    l2cap_register_service(bt_packet_handler, PSM_HID_INTERRUPT, 0xffff, LEVEL_1);
    
    hci_event_callback_registration.callback = &bt_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    hci_power_control(HCI_POWER_ON);
}