#include "settings.h"

#include "nvs.h"
#include "nvs_flash.h"

static int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool decode_legacy_form_value(char *value) {
    char *read = value;
    char *write = value;
    bool changed = false;
    while (*read) {
        if (read[0] == '%' && read[1] && read[2]) {
            int high = hex_value(read[1]);
            int low = hex_value(read[2]);
            if (high >= 0 && low >= 0) {
                *write++ = static_cast<char>((high << 4) | low);
                read += 3;
                changed = true;
                continue;
            }
        }
        if (*read == '+') {
            *write++ = ' ';
            ++read;
            changed = true;
            continue;
        }
        *write++ = *read++;
    }
    *write = '\0';
    return changed;
}

static LedPalette palette = {
    .starting = 0x0000ff,
    .online = 0x00b85a,
    .on_battery = 0xff9d00,
    .low_battery = 0xff3000,
    .fault = 0xff0000,
    .disconnected = 0x6b7280,
    .brightness = 32,
    .blink_starting = false,
    .blink_online = false,
    .blink_on_battery = false,
    .blink_low_battery = true,
    .blink_fault = true,
    .blink_disconnected = false,
};

static NetworkSettings network = {
    .magic = NETWORK_SETTINGS_MAGIC,
    .dhcp = true,
    .ip = "192.168.1.118",
    .gateway = "192.168.1.1",
    .netmask = "255.255.255.0",
    .dns = "192.168.1.1",
};

void settings_init() {
    nvs_flash_init();
    nvs_handle_t nvs;
    if (nvs_open("guardian", NVS_READONLY, &nvs) != ESP_OK) return;
    size_t size = 0;
    if (nvs_get_blob(nvs, "led_palette", nullptr, &size) == ESP_OK) {
        if (size == sizeof(palette)) {
            nvs_get_blob(nvs, "led_palette", &palette, &size);
        } else {
            struct LegacyLedPalette {
                uint32_t starting, online, on_battery, low_battery, fault, disconnected;
                uint8_t brightness;
            } legacy = {};
            if (size == sizeof(legacy) &&
                nvs_get_blob(nvs, "led_palette", &legacy, &size) == ESP_OK) {
                palette.starting = legacy.starting;
                palette.online = legacy.online;
                palette.on_battery = legacy.on_battery;
                palette.low_battery = legacy.low_battery;
                palette.fault = legacy.fault;
                palette.disconnected = legacy.disconnected;
                palette.brightness = legacy.brightness;
            }
        }
    }
    NetworkSettings stored_network = {};
    size = sizeof(stored_network);
    if (nvs_get_blob(nvs, "network", &stored_network, &size) == ESP_OK &&
        stored_network.magic == NETWORK_SETTINGS_MAGIC) {
        network = stored_network;
    }
    nvs_close(nvs);
}

LedPalette settings_led_palette() {
    return palette;
}

bool settings_set_led_palette(const LedPalette &value) {
    palette = value;
    nvs_handle_t nvs;
    if (nvs_open("guardian", NVS_READWRITE, &nvs) != ESP_OK) return false;
    esp_err_t result = nvs_set_blob(nvs, "led_palette", &palette, sizeof(palette));
    if (result == ESP_OK) result = nvs_commit(nvs);
    nvs_close(nvs);
    return result == ESP_OK;
}

bool settings_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size) {
    nvs_handle_t nvs;
    if (nvs_open("guardian", NVS_READONLY, &nvs) == ESP_OK) {
        size_t current_ssid_size = ssid_size;
        size_t current_password_size = password_size;
        esp_err_t ssid_result = nvs_get_str(nvs, "wifi_ssid", ssid, &current_ssid_size);
        esp_err_t pass_result = nvs_get_str(nvs, "wifi_pass", password, &current_password_size);
        nvs_close(nvs);
        if (ssid_result == ESP_OK && pass_result == ESP_OK && ssid[0] != '\0') {
            bool migrated = decode_legacy_form_value(ssid);
            migrated = decode_legacy_form_value(password) || migrated;
            if (migrated) settings_set_wifi_credentials(ssid, password);
            return true;
        }
    }

    // Importación única desde el formato NVS usado por las versiones de desarrollo.
    if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK) return false;
    size_t legacy_ssid_size = ssid_size;
    size_t legacy_password_size = password_size;
    esp_err_t ssid_result = nvs_get_str(nvs, "ssid", ssid, &legacy_ssid_size);
    esp_err_t pass_result = nvs_get_str(nvs, "password", password, &legacy_password_size);
    nvs_close(nvs);
    if (ssid_result != ESP_OK || pass_result != ESP_OK || ssid[0] == '\0') return false;
    decode_legacy_form_value(ssid);
    decode_legacy_form_value(password);
    settings_set_wifi_credentials(ssid, password);
    return true;
}

bool settings_set_wifi_credentials(const char *ssid, const char *password) {
    nvs_handle_t nvs;
    if (nvs_open("guardian", NVS_READWRITE, &nvs) != ESP_OK) return false;
    esp_err_t result = nvs_set_str(nvs, "wifi_ssid", ssid);
    if (result == ESP_OK) result = nvs_set_str(nvs, "wifi_pass", password);
    if (result == ESP_OK) result = nvs_commit(nvs);
    nvs_close(nvs);
    return result == ESP_OK;
}

NetworkSettings settings_network() {
    return network;
}

bool settings_set_network(const NetworkSettings &value) {
    network = value;
    network.magic = NETWORK_SETTINGS_MAGIC;
    nvs_handle_t nvs;
    if (nvs_open("guardian", NVS_READWRITE, &nvs) != ESP_OK) return false;
    esp_err_t result = nvs_set_blob(nvs, "network", &network, sizeof(network));
    if (result == ESP_OK) result = nvs_commit(nvs);
    nvs_close(nvs);
    return result == ESP_OK;
}
