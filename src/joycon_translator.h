#pragma once
#include <stdint.h>
#include <stdbool.h>

void joycon_init(void);
void joycon_task(void);
void joycon_parse_l2cap_report(const uint8_t* report, bool is_left);