#include <stdio.h>
#include <stdarg.h>
#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>
#include <btstack_run_loop.h>
#include "tusb.h"
#include "bt_bridge.h"
#include "joycon_translator.h"

// Helper function to safely print to our custom CDC interface and UART
void cdc_printf(const char *format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Print to hardware UART
    printf("%s", buffer); 
    
    // Print to USB Serial if a host terminal is connected
    if (tud_cdc_connected()) {
        tud_cdc_write(buffer, len);
        tud_cdc_write_flush();
    }
}

static btstack_data_source_t usb_ds;
static void usb_poll_process(btstack_data_source_t *ds, btstack_data_source_callback_type_t callback_type) {
    tud_task();
}

int main() {
    // Initialize CYW43 BEFORE TinyUSB/stdio to prevent boot timeouts on the USB host.
    // NOTE: cyw43_arch_init() internally handles btstack_memory_init() and 
    // btstack_run_loop_init() within the Pico SDK.
    if (cyw43_arch_init()) return -1;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    // Initialize TinyUSB manually because we disabled stdio_usb
    tusb_init();
    stdio_init_all();
    joycon_init();

    cdc_printf("\n\n--- Pico 2W Joy-Con Bridge Booted ---\n");

    // Initialize Bluetooth profiles and Power-On HCI
    bt_bridge_init();

    // Attach USB to the run loop and execute
    btstack_run_loop_set_data_source_handler(&usb_ds, &usb_poll_process);
    btstack_run_loop_enable_data_source_callbacks(&usb_ds, DATA_SOURCE_CALLBACK_POLL);
    btstack_run_loop_add_data_source(&usb_ds);

    btstack_run_loop_execute();
    return 0;
}