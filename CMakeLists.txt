cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD "pico2_w")
endif()

set(PICO_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/pico-sdk)
include(pico_sdk_import.cmake)
project(pico2w_joycon_bridge C CXX ASM)

set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(${PROJECT_NAME}
    src/main.c
    src/usb_descriptors.c
    src/bt_bridge.c
    src/joycon_translator.c
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE
    pico_stdlib
    pico_cyw43_arch_none
    pico_btstack_classic
    pico_btstack_cyw43
    tinyusb_device
    tinyusb_board
)

# CRITICAL FIX: Disable Pico SDK's internal TinyUSB hooks to prevent conflicts 
# with our custom composite descriptor. UART is kept for hardware debugging.
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 1)

pico_add_extra_outputs(${PROJECT_NAME})