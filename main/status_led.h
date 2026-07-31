#pragma once

#include <stdbool.h>
#include <stdint.h>

void status_led_start();
void status_led_test(uint32_t color, bool blink, uint32_t duration_ms);

