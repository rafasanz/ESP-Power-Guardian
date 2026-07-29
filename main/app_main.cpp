#include "esp_log.h"

#include "guardian_state.h"
#include "nut_server.h"
#include "settings.h"
#include "status_led.h"
#include "usb_monitor.h"
#include "web_server.h"
#include "wifi_manager.h"

extern "C" void app_main() {
    settings_init();
    guardian_state_init();
    status_led_start();
    usb_monitor_start();
    wifi_manager_start();
    web_server_start();
    nut_server_start();
    ESP_LOGI("guardian", "ESP Power Guardian %s iniciado", EPG_VERSION);
}
