#include "wifi_manager.h"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "settings.h"

static const char *TAG = "wifi";
static bool connected;
static char ip_address[16] = "0.0.0.0";

static void event_handler(void *, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        connected = false;
        strcpy(ip_address, "0.0.0.0");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(data);
        snprintf(ip_address, sizeof(ip_address), IPSTR, IP2STR(&event->ip_info.ip));
        connected = true;
        ESP_LOGI(TAG, "Conectado: %s", ip_address);
    }
}

void wifi_manager_start() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, nullptr));

    char ssid[33] = {};
    char password[65] = {};
    bool have_credentials = settings_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password));

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ap_name[33];
    snprintf(ap_name, sizeof(ap_name), "ESP-Power-Guardian-%02X%02X", mac[4], mac[5]);

    wifi_config_t ap = {};
    strlcpy(reinterpret_cast<char *>(ap.ap.ssid), ap_name, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(ap_name);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(have_credentials ? WIFI_MODE_APSTA : WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));

    if (have_credentials) {
        wifi_config_t station = {};
        strlcpy(reinterpret_cast<char *>(station.sta.ssid), ssid, sizeof(station.sta.ssid));
        strlcpy(reinterpret_cast<char *>(station.sta.password), password, sizeof(station.sta.password));
        station.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    if (have_credentials) esp_wifi_connect();
    ESP_LOGI(TAG, "Punto de configuración: %s", ap_name);
}

bool wifi_manager_connected() {
    return connected;
}

const char *wifi_manager_ip() {
    return ip_address;
}

