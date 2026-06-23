#pragma once

#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_PRINTF_HEXDUMP
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT

#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE 256
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4
#define MAX_NR_HCI_CONNECTIONS 2
#define MAX_NR_L2CAP_CHANNELS 6 
#define MAX_NR_L2CAP_SERVICES 3 
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_WHITELIST_ENTRIES 2

#define MAX_NR_SERVICE_RECORD_ITEMS 4 

// For real-time HID, deep queues equal deep lag. We WANT stale packets 
// to be instantly overwritten at the baseband level.
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3  
#define HCI_HOST_ACL_PACKET_LEN 256
#define HCI_HOST_ACL_PACKET_NUM 3        
#define HCI_HOST_SCO_PACKET_LEN 0
#define HCI_HOST_SCO_PACKET_NUM 0

// Required by the Pico SDK to compile btstack_link_key_db_tlv.c. 
// We actively clear this DB in bt_bridge.c to force clean pairing.
#define NVM_NUM_LINK_KEYS 4
#define NVM_NUM_DEVICE_DB_ENTRIES 4