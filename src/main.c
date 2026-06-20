#include <stdio.h>
#include <stdarg.h>
#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>

#include <btstack_run_loop.h>

#include "tusb.h"
#include "bt_bridge.h"
#include "joycon_translator.h"

void cdc_printf(const char *format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    printf("%s", buffer); 
    
    uint32_t avail = tud_cdc_write_available();
    if (avail > 0) {
        tud_cdc_write(buffer, len > avail ? avail : len);
        tud_cdc_write_flush();
    }
}

static btstack_data_source_t usb_ds;
static void usb_poll_process(btstack_data_source_t *ds, btstack_data_source_callback_type_t callback_type) {
    tud_task();
    joycon_task();
}

static btstack_timer_source_t heartbeat_timer;
static void heartbeat_handler(btstack_timer_source_t *ts) {
    static bool led_state = true;
    led_state = !led_state;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
    
    btstack_run_loop_set_timer(ts, 2500); 
    btstack_run_loop_add_timer(ts);
}

int main() {
    stdio_init_all();
    tusb_init();
    joycon_init();

    uint32_t t = time_us_32();
    while (time_us_32() - t < 3000000) {
        tud_task();
    }

    cdc_printf("\n\n--- Pico 2W Joy-Con Bridge Booted ---\n");

    if (cyw43_arch_init()) {
        cdc_printf("CRITICAL ERROR: Failed to initialize CYW43 architecture!\n");
        return -1;
    }

    // Explicitly disable the Wi-Fi architecture.
    // The CYW43439 shares a single physical antenna. If Wi-Fi is active, 
    // it routinely steals the antenna from Bluetooth, causing packet buffering.
    cyw43_arch_disable_sta_mode();
    cyw43_arch_disable_ap_mode();

    bt_bridge_init();

    btstack_run_loop_set_data_source_handler(&usb_ds, &usb_poll_process);
    btstack_run_loop_enable_data_source_callbacks(&usb_ds, DATA_SOURCE_CALLBACK_POLL);
    btstack_run_loop_add_data_source(&usb_ds);

    btstack_run_loop_set_timer_handler(&heartbeat_timer, heartbeat_handler);
    btstack_run_loop_set_timer(&heartbeat_timer, 2500);
    btstack_run_loop_add_timer(&heartbeat_timer);

    btstack_run_loop_execute();
    return 0;
}