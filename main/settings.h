#pragma once

#include <stddef.h>
#include <stdint.h>

struct LedPalette {
    uint32_t starting;
    uint32_t online;
    uint32_t on_battery;
    uint32_t low_battery;
    uint32_t fault;
    uint32_t disconnected;
    uint8_t brightness;
};

void settings_init();
LedPalette settings_led_palette();
bool settings_set_led_palette(const LedPalette &palette);
bool settings_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size);
bool settings_set_wifi_credentials(const char *ssid, const char *password);
