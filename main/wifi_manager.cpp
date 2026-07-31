#include "wifi_manager.h"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "settings.h"

static const char *TAG = "wifi";
static bool connected;
static char ip_address[16] = "0.0.0.0";
static bool time_sync_started;
static uint8_t ap_client_count;
static esp_netif_t *station_netif;
static NetworkSettings requested_network = {};
static bool static_ip_pending;
static bool static_ip_applied;
static bool credentials_configured;
static bool recovery_ap_enabled;
static bool connection_watchdog_running;
static WifiScanResult scan_results[12] = {};
static size_t scan_result_count;
static volatile WifiScanState scan_state = WifiScanState::idle;

static bool collect_scan_results() {
    scan_result_count = 0;
    uint16_t count = static_cast<uint16_t>(
        sizeof(scan_results) / sizeof(scan_results[0]));
    wifi_ap_record_t records[sizeof(scan_results) / sizeof(scan_results[0])] = {};
    esp_err_t result = esp_wifi_scan_get_ap_records(&count, records);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "No se pudieron leer las redes detectadas: %s",
                 esp_err_to_name(result));
        return false;
    }

    for (uint16_t i = 0; i < count; ++i) {
        const char *ssid = reinterpret_cast<const char *>(records[i].ssid);
        if (!ssid[0]) continue;

        bool duplicate = false;
        for (size_t saved = 0; saved < scan_result_count; ++saved) {
            if (strcmp(scan_results[saved].ssid, ssid) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        strlcpy(scan_results[scan_result_count].ssid, ssid,
                sizeof(scan_results[scan_result_count].ssid));
        scan_results[scan_result_count].rssi = records[i].rssi;
        ++scan_result_count;
    }
    ESP_LOGI(TAG, "%u redes detectadas para la interfaz web",
             static_cast<unsigned>(scan_result_count));
    return true;
}

static void start_time_sync() {
    if (time_sync_started) return;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, const_cast<char *>("pool.ntp.org"));
    esp_sntp_init();
    time_sync_started = true;
}

static void set_recovery_ap(bool enabled) {
    wifi_mode_t current_mode = WIFI_MODE_NULL;
    wifi_mode_t target_mode = enabled ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    esp_err_t mode_result = esp_wifi_get_mode(&current_mode);
    if (mode_result == ESP_OK && current_mode == target_mode) {
        recovery_ap_enabled = enabled;
        return;
    }
    esp_err_t result =
        esp_wifi_set_mode(target_mode);
    if (result == ESP_OK) {
        recovery_ap_enabled = enabled;
        if (!enabled) ap_client_count = 0;
        ESP_LOGI(TAG, "AP de recuperación %s",
                 enabled ? "activado" : "desactivado");
    } else {
        ESP_LOGE(TAG, "No se pudo %s el AP de recuperación: %s",
                 enabled ? "activar" : "desactivar", esp_err_to_name(result));
    }
}

static void connected_with_ip(const char *address) {
    strlcpy(ip_address, address, sizeof(ip_address));
    connected = true;
    start_time_sync();
    if (credentials_configured) set_recovery_ap(false);
    ESP_LOGI(TAG, "Conectado: %s", ip_address);
}

static void connection_watchdog_task(void *) {
    vTaskDelay(pdMS_TO_TICKS(12000));
    while (credentials_configured && !connected) {
        set_recovery_ap(true);
        if (scan_state != WifiScanState::running) {
            esp_err_t result = esp_wifi_connect();
            if (result != ESP_OK) {
                ESP_LOGW(TAG, "Reintento Wi-Fi no iniciado: %s",
                         esp_err_to_name(result));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
    connection_watchdog_running = false;
    vTaskDelete(nullptr);
}

static void start_connection_watchdog() {
    if (!credentials_configured || connection_watchdog_running) return;
    connection_watchdog_running =
        xTaskCreate(connection_watchdog_task, "wifi_recovery", 3072, nullptr,
                    3, nullptr) == pdPASS;
}

static void event_handler(void *, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_DISCONNECTED) {
            connected = false;
            strcpy(ip_address, "0.0.0.0");
            ESP_LOGW(TAG, "La conexión a la red guardada se ha interrumpido");
            start_connection_watchdog();
        } else if (id == WIFI_EVENT_AP_STACONNECTED) {
            if (ap_client_count < UINT8_MAX) ++ap_client_count;
            ESP_LOGI(TAG, "Cliente conectado al AP (%u)",
                     static_cast<unsigned>(ap_client_count));
        } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
            if (ap_client_count > 0) --ap_client_count;
            ESP_LOGI(TAG, "Cliente desconectado del AP (%u)",
                     static_cast<unsigned>(ap_client_count));
        } else if (id == WIFI_EVENT_SCAN_DONE) {
            scan_state = WifiScanState::ready;
            ESP_LOGI(TAG, "Escaneo Wi-Fi finalizado");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(data);
        if (static_ip_pending && !static_ip_applied && station_netif) {
            esp_err_t result = esp_netif_dhcpc_stop(station_netif);
            if (result == ESP_OK ||
                result == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
                esp_netif_ip_info_t info = {};
                esp_netif_str_to_ip4(requested_network.ip, &info.ip);
                esp_netif_str_to_ip4(requested_network.gateway, &info.gw);
                esp_netif_str_to_ip4(requested_network.netmask, &info.netmask);
                static_ip_applied = true;
                result = esp_netif_set_ip_info(station_netif, &info);
                if (result == ESP_OK) {
                    esp_netif_dns_info_t dns = {};
                    dns.ip.type = ESP_IPADDR_TYPE_V4;
                    esp_netif_str_to_ip4(requested_network.dns, &dns.ip.u_addr.ip4);
                    esp_err_t dns_result =
                        esp_netif_set_dns_info(station_netif, ESP_NETIF_DNS_MAIN, &dns);
                    if (dns_result != ESP_OK) {
                        ESP_LOGW(TAG, "IP fija aplicada, pero no el DNS: %s",
                                 esp_err_to_name(dns_result));
                    }
                    static_ip_pending = false;
                    ESP_LOGI(TAG, "IP fija aplicada tras conectar por DHCP: %s",
                             requested_network.ip);
                    connected_with_ip(requested_network.ip);
                    return;
                }
                static_ip_applied = false;
                ESP_LOGE(TAG, "No se pudo sustituir DHCP por la IP fija: %s",
                         esp_err_to_name(result));
            } else {
                ESP_LOGE(TAG, "No se pudo detener DHCP tras conectar: %s",
                         esp_err_to_name(result));
            }
            static_ip_pending = false;
            esp_err_t fallback = esp_netif_dhcpc_start(station_netif);
            if (fallback != ESP_OK &&
                fallback != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
                ESP_LOGE(TAG, "No se pudo mantener DHCP como recuperación: %s",
                         esp_err_to_name(fallback));
            }
        }
        char assigned_ip[16] = {};
        snprintf(assigned_ip, sizeof(assigned_ip), IPSTR,
                 IP2STR(&event->ip_info.ip));
        connected_with_ip(assigned_ip);
    }
}

void wifi_manager_start() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    station_netif = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, nullptr));

    char ssid[33] = {};
    char password[65] = {};
    bool have_credentials = settings_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password));
    credentials_configured = have_credentials;
    requested_network = settings_network();
    static_ip_pending = have_credentials && !requested_network.dhcp;
    static_ip_applied = false;

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
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));

    if (have_credentials) {
        wifi_config_t station = {};
        strlcpy(reinterpret_cast<char *>(station.sta.ssid), ssid, sizeof(station.sta.ssid));
        strlcpy(reinterpret_cast<char *>(station.sta.password), password, sizeof(station.sta.password));
        station.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    }
    recovery_ap_enabled = !have_credentials;
    ESP_ERROR_CHECK(esp_wifi_start());

    if (have_credentials) {
        esp_err_t connect_result = esp_wifi_connect();
        if (connect_result != ESP_OK) {
            ESP_LOGW(TAG, "No se pudo iniciar la conexión a la red guardada: %s",
                     esp_err_to_name(connect_result));
        }
        start_connection_watchdog();
    }
    ESP_LOGI(TAG, "Punto de configuración: %s (%s)", ap_name,
             recovery_ap_enabled ? "activo" : "en espera");
}

bool wifi_manager_connected() {
    return connected;
}

const char *wifi_manager_ip() {
    return ip_address;
}

bool wifi_manager_ap_active() {
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK) return false;
    return mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA;
}

WifiScanState wifi_manager_start_scan() {
    if (scan_state == WifiScanState::running ||
        scan_state == WifiScanState::ready) {
        return scan_state;
    }

    wifi_scan_config_t scan = {};
    scan.show_hidden = false;
    scan.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan.scan_time.active.min = 30;
    scan.scan_time.active.max = 80;
    scan_state = WifiScanState::running;
    esp_err_t result = esp_wifi_scan_start(&scan, false);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo iniciar el escaneo Wi-Fi: %s",
                 esp_err_to_name(result));
        scan_state = WifiScanState::failed;
        return scan_state;
    }
    ESP_LOGI(TAG, "Escaneo Wi-Fi asíncrono iniciado");
    return scan_state;
}

WifiScanState wifi_manager_scan_state() {
    return scan_state;
}

size_t wifi_manager_scan_results(WifiScanResult *results, size_t capacity) {
    if (scan_state != WifiScanState::ready) return 0;
    if (!collect_scan_results()) {
        scan_state = WifiScanState::failed;
        return 0;
    }
    size_t count = scan_result_count < capacity ? scan_result_count : capacity;
    if (results && capacity > 0) {
        memcpy(results, scan_results, count * sizeof(WifiScanResult));
    }
    scan_state = WifiScanState::idle;
    return count;
}

