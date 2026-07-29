#include "settings.h"

#include "nvs.h"
#include "nvs_flash.h"

static LedPalette palette = {
    .starting = 0x2060ff,
    .online = 0x00b85a,
    .on_battery = 0xff9d00,
    .low_battery = 0xff3000,
    .fault = 0xff0000,
    .disconnected = 0x6b7280,
    .brightness = 32,
};

void settings_init() {
    nvs_flash_init();
    nvs_handle_t nvs;
    if (nvs_open("guardian", NVS_READONLY, &nvs) != ESP_OK) return;
    size_t size = sizeof(palette);
    nvs_get_blob(nvs, "led_palette", &palette, &size);
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
        if (ssid_result == ESP_OK && pass_result == ESP_OK && ssid[0] != '\0') return true;
    }

    // Importación única desde el formato NVS usado por las versiones de desarrollo.
    if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK) return false;
    size_t legacy_ssid_size = ssid_size;
    size_t legacy_password_size = password_size;
    esp_err_t ssid_result = nvs_get_str(nvs, "ssid", ssid, &legacy_ssid_size);
    esp_err_t pass_result = nvs_get_str(nvs, "password", password, &legacy_password_size);
    nvs_close(nvs);
    if (ssid_result != ESP_OK || pass_result != ESP_OK || ssid[0] == '\0') return false;
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
