#pragma once

#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT
#define ENABLE_L2CAP_ENHANCED_RETRANSMISSION_MODE

#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE 256
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4 // Required for Pico W / 2 W SPI DMA

#define MAX_NR_HCI_CONNECTIONS 2
#define MAX_NR_L2CAP_CHANNELS 6 // 2 channels (Control/Int) per Joycon
#define MAX_NR_L2CAP_SERVICES 2 // HID Control & HID Interrupt
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_WHITELIST_ENTRIES 2

// Flow control buffers required by the Pico W CYW43 driver
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 0
#define HCI_HOST_ACL_PACKET_LEN 256
#define HCI_HOST_ACL_PACKET_NUM 3
#define HCI_HOST_SCO_PACKET_LEN 0
#define HCI_HOST_SCO_PACKET_NUM 0

// Number of paired devices/keys to store in Pico's non-volatile flash memory
#define NVM_NUM_LINK_KEYS 4
#define NVM_NUM_DEVICE_DB_ENTRIES 4