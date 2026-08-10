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
    bool blink_starting;
    bool blink_online;
    bool blink_on_battery;
    bool blink_low_battery;
    bool blink_fault;
    bool blink_disconnected;
};

struct NetworkSettings {
    uint32_t magic;
    bool dhcp;
    char ip[16];
    char gateway[16];
    char netmask[16];
    char dns[16];
};

static constexpr uint32_t NETWORK_SETTINGS_MAGIC = 0x4550474e;

void settings_init();
LedPalette settings_led_palette();
bool settings_set_led_palette(const LedPalette &palette);
bool settings_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size);
bool settings_set_wifi_credentials(const char *ssid, const char *password);
const char *settings_device_name();
bool settings_set_device_name(const char *name);
NetworkSettings settings_network();
bool settings_set_network(const NetworkSettings &network);
