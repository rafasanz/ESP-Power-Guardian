#include "esp_log.h"
#include "esp_ota_ops.h"

#include "guardian_state.h"
#include "nut_server.h"
#include "settings.h"
#include "status_led.h"
#include "usb_monitor.h"
#include "web_server.h"
#include "wifi_manager.h"

extern "C" void app_main() {
    ESP_LOGI("guardian", "Arrancando ESP Power Guardian %s", EPG_VERSION);
    esp_ota_mark_app_valid_cancel_rollback();
    settings_init();
    guardian_state_init();
    status_led_start();
    ESP_LOGI("guardian", "Iniciando red");
    wifi_manager_start();
    web_server_start();
    nut_server_start();
    ESP_LOGI("guardian", "Red y servicios iniciados; preparando USB Host");
    usb_monitor_start();
    ESP_LOGI("guardian", "ESP Power Guardian %s iniciado", EPG_VERSION);
}
