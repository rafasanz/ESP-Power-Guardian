#include "web_server.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "guardian_state.h"
#include "settings.h"
#include "status_led.h"
#include "usb_monitor.h"
#include "wifi_manager.h"
#include "web_ui.h"

static const char *TAG = "web";

static const char PAGE[] = R"HTML(<!doctype html><html lang="es"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP Power Guardian</title><style>
:root{color-scheme:dark;--bg:#0c1522;--panel:#142235;--card:#1b2d43;--text:#eef6ff;--muted:#9db0c7;--accent:#36c3ff}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:16px system-ui,sans-serif}
main{max-width:980px;margin:auto;padding:22px}.panel{background:var(--panel);border-radius:16px;padding:18px;margin:14px 0}
h1,h2{margin:0 0 16px}.grid{display:grid;grid-template-columns:repeat(2,minmax(240px,1fr));gap:12px}
.item{background:var(--card);border-radius:12px;padding:14px}.item label{display:flex;justify-content:space-between;align-items:center;gap:12px}
input[type=color]{width:58px;height:38px;border:0;background:none}input[type=range]{width:100%}
input[type=text],input[type=password]{width:100%;padding:11px;border:1px solid #52647a;border-radius:9px;background:#0e1928;color:var(--text)}
button{padding:11px 18px;border:0;border-radius:9px;background:var(--accent);color:#00131d;font-weight:700;cursor:pointer}
.status{color:var(--muted)}footer{display:flex;justify-content:space-between;color:var(--muted);padding:8px}
footer a{color:var(--accent);text-decoration:none}@media(max-width:650px){.grid{grid-template-columns:1fr}}
</style></head><body><main><h1>ESP Power Guardian</h1>
<section class="panel"><h2>Estado</h2><p id="status" class="status">Consultando el dispositivo…</p></section>
<section class="panel"><h2>LED de estado</h2><div class="grid">
<div class="item"><label>Iniciando <input id="starting" type="color"></label></div>
<div class="item"><label>Alimentación de red <input id="online" type="color"></label></div>
<div class="item"><label>Funcionando con batería <input id="on_battery" type="color"></label></div>
<div class="item"><label>Batería baja <input id="low_battery" type="color"></label></div>
<div class="item"><label>Alarma <input id="fault" type="color"></label></div>
<div class="item"><label>SAI desconectado <input id="disconnected" type="color"></label></div>
<div class="item"><label>Brillo</label><input id="brightness" type="range" min="1" max="255"></div>
</div><p><button onclick="saveLed()">Guardar colores</button></p></section>
<section class="panel"><h2>Wi‑Fi</h2><div class="grid"><div><label>Red</label><input id="ssid" type="text"></div>
<div><label>Contraseña</label><input id="password" type="password"></div></div>
<p><button onclick="saveWifi()">Guardar y reiniciar</button></p></section>
<section class="panel"><h2>Actualización OTA</h2><input id="firmware" type="file" accept=".bin">
<p><button onclick="ota()">Instalar firmware</button></p></section>
<footer><span>Made by <a href="https://github.com/rafasanz">@rafasanz</a></span><span>)HTML" EPG_VERSION R"HTML(</span></footer>
</main><script>
const ids=['starting','online','on_battery','low_battery','fault','disconnected'];
async function load(){let s=await fetch('/api/status').then(r=>r.json());status.textContent=`${s.condition} · Wi‑Fi ${s.wifi} · IP ${s.ip} · NUT guardian@${s.ip}:3493`;
let p=await fetch('/api/led').then(r=>r.json());ids.forEach(k=>document.getElementById(k).value=p[k]);brightness.value=p.brightness}
async function saveLed(){let p={brightness:+brightness.value};ids.forEach(k=>p[k]=document.getElementById(k).value);
alert(await fetch('/api/led',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)}).then(r=>r.text()))}
async function saveWifi(){let body=new URLSearchParams({ssid:ssid.value,password:password.value});
alert(await fetch('/api/wifi',{method:'POST',body}).then(r=>r.text()))}
async function ota(){let f=firmware.files[0];if(!f)return alert('Selecciona un archivo .bin');
alert(await fetch('/api/ota',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:f}).then(r=>r.text()))}
load();
</script></body></html>)HTML";

static uint32_t color_from_hex(const char *hex) {
    if (*hex == '#') ++hex;
    return static_cast<uint32_t>(strtoul(hex, nullptr, 16)) & 0xffffff;
}

static std::string hex_color(uint32_t color) {
    char text[8];
    snprintf(text, sizeof(text), "#%06lx", static_cast<unsigned long>(color));
    return text;
}

static std::string json_escape(const char *text) {
    std::string escaped;
    if (!text) return escaped;
    while (*text) {
        const unsigned char character = static_cast<unsigned char>(*text++);
        switch (character) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (character >= 0x20) escaped += static_cast<char>(character);
                break;
        }
    }
    return escaped;
}

static esp_err_t index_handler(httpd_req_t *request) {
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, WEB_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *request) {
    GuardianSnapshot s = guardian_state_get();
    wifi_ap_record_t access_point = {};
    int rssi = esp_wifi_sta_get_ap_info(&access_point) == ESP_OK ? access_point.rssi : 0;
    int64_t uptime = esp_timer_get_time() / 1000000;
    int64_t now_ms = esp_timer_get_time() / 1000;
    int64_t age = s.last_update_ms > 0 ? (now_ms - s.last_update_ms) / 1000 : -1;
    int64_t monitoring = s.monitoring_started_ms > 0 ? (now_ms - s.monitoring_started_ms) / 1000 : 0;
    int64_t error_age = s.last_error_ms > 0 ? (now_ms - s.last_error_ms) / 1000 : -1;
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_text[18] = {};
    snprintf(mac_text, sizeof(mac_text), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    char qx_status[128] = {};
    char qx_reply[128] = {};
    usb_monitor_copy_status(qx_status, sizeof(qx_status));
    usb_monitor_copy_reply(qx_reply, sizeof(qx_reply));
    const std::string escaped_status = json_escape(qx_status);
    const std::string escaped_reply = json_escape(qx_reply);
    char json[1536];
    snprintf(json, sizeof(json),
             "{\"firmware\":\"%s\",\"condition\":\"%s\",\"wifi\":\"%s\",\"ip\":\"%s\","
             "\"mac\":\"%s\",\"recovery_ap\":%s,\"rssi\":%d,\"uptime_s\":%lld,\"nut_port\":3493,\"ups_status\":\"%s\","
             "\"usb_vid\":\"%04x\",\"usb_pid\":\"%04x\",\"qx_status\":\"%s\",\"qx_reply\":\"%s\","
             "\"data_valid\":%s,\"last_data_age_s\":%lld,\"monitoring_s\":%lld,"
             "\"last_error_age_s\":%lld,\"consecutive_failures\":%lu,\"recovery_count\":%lu,"
             "\"automatic_restarts\":%lu,\"last_usb_status\":%d,\"input_voltage\":%.1f,"
             "\"output_voltage\":%.1f,\"frequency\":%.1f,\"battery_voltage\":%.1f,"
             "\"battery\":%d,\"runtime_s\":%d,\"load\":%d}",
             EPG_VERSION, guardian_condition_name(s.condition),
             wifi_manager_connected() ? "conectado" : "sin conexión", wifi_manager_ip(),
             mac_text, wifi_manager_ap_active() ? "true" : "false", rssi,
             static_cast<long long>(uptime), guardian_nut_status(s.condition),
             s.usb_vid, s.usb_pid, escaped_status.c_str(), escaped_reply.c_str(),
             s.data_valid ? "true" : "false", static_cast<long long>(age),
             static_cast<long long>(monitoring), static_cast<long long>(error_age),
             static_cast<unsigned long>(s.consecutive_failures),
             static_cast<unsigned long>(s.recovery_count),
             static_cast<unsigned long>(s.automatic_restarts), s.last_usb_status,
             s.input_voltage, s.output_voltage, s.frequency,
             s.battery_voltage, s.battery_percent, s.runtime_seconds, s.load_percent);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, json);
}

static esp_err_t communication_history_handler(httpd_req_t *request) {
    GuardianCommunicationEvent events[10] = {};
    size_t count = guardian_communication_history(events, 10);
    std::string json = "[";
    for (size_t reverse = count; reverse > 0; --reverse) {
        const GuardianCommunicationEvent &event = events[reverse - 1];
        if (reverse != count) json += ",";
        json += "{\"epoch\":" + std::to_string(event.epoch) +
                ",\"type\":\"" + guardian_communication_event_name(event.type) +
                "\",\"detail\":" + std::to_string(event.detail) + "}";
    }
    json += "]";
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, json.data(), json.size());
}

static esp_err_t clear_communication_history_handler(httpd_req_t *request) {
    guardian_clear_communication_history();
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true}");
}

static esp_err_t outage_history_handler(httpd_req_t *request) {
    GuardianOutage outages[10] = {};
    size_t count = guardian_outage_history(outages, 10);
    std::string json = "[";
    for (size_t reverse = count; reverse > 0; --reverse) {
        const GuardianOutage &outage = outages[reverse - 1];
        if (reverse != count) json += ",";
        json += "{\"started_epoch\":" + std::to_string(outage.started_epoch) +
                ",\"ended_epoch\":" + std::to_string(outage.ended_epoch) +
                ",\"active\":" + (outage.ended_epoch == 0 ? "true" : "false") +
                ",\"condition\":\"" + guardian_condition_name(outage.condition) + "\"}";
    }
    json += "]";
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, json.data(), json.size());
}

static esp_err_t clear_outage_history_handler(httpd_req_t *request) {
    guardian_clear_outage_history();
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, "{\"ok\":true}");
}

static esp_err_t led_get_handler(httpd_req_t *request) {
    LedPalette p = settings_led_palette();
    std::string json = "{\"starting\":\"" + hex_color(p.starting) + "\",\"online\":\"" +
        hex_color(p.online) + "\",\"on_battery\":\"" + hex_color(p.on_battery) +
        "\",\"low_battery\":\"" + hex_color(p.low_battery) + "\",\"fault\":\"" +
        hex_color(p.fault) + "\",\"disconnected\":\"" + hex_color(p.disconnected) +
        "\",\"brightness\":" + std::to_string(p.brightness) +
        ",\"blink_starting\":" + (p.blink_starting ? "true" : "false") +
        ",\"blink_online\":" + (p.blink_online ? "true" : "false") +
        ",\"blink_on_battery\":" + (p.blink_on_battery ? "true" : "false") +
        ",\"blink_low_battery\":" + (p.blink_low_battery ? "true" : "false") +
        ",\"blink_fault\":" + (p.blink_fault ? "true" : "false") +
        ",\"blink_disconnected\":" + (p.blink_disconnected ? "true" : "false") + "}";
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, json.data(), json.size());
}

static bool json_string(const std::string &json, const char *key, char *out, size_t out_size) {
    std::string marker = std::string("\"") + key + "\":\"";
    size_t begin = json.find(marker);
    if (begin == std::string::npos) return false;
    begin += marker.size();
    size_t end = json.find('"', begin);
    if (end == std::string::npos || end - begin >= out_size) return false;
    memcpy(out, json.data() + begin, end - begin);
    out[end - begin] = '\0';
    return true;
}

static bool json_bool(const std::string &json, const char *key, bool fallback) {
    std::string marker = std::string("\"") + key + "\":";
    size_t begin = json.find(marker);
    if (begin == std::string::npos) return fallback;
    begin += marker.size();
    return json.compare(begin, 4, "true") == 0;
}

static esp_err_t led_post_handler(httpd_req_t *request) {
    std::string body(request->content_len, '\0');
    if (httpd_req_recv(request, body.data(), body.size()) <= 0) return ESP_FAIL;
    LedPalette p = settings_led_palette();
    char value[16];
    if (json_string(body, "starting", value, sizeof(value))) p.starting = color_from_hex(value);
    if (json_string(body, "online", value, sizeof(value))) p.online = color_from_hex(value);
    if (json_string(body, "on_battery", value, sizeof(value))) p.on_battery = color_from_hex(value);
    if (json_string(body, "low_battery", value, sizeof(value))) p.low_battery = color_from_hex(value);
    if (json_string(body, "fault", value, sizeof(value))) p.fault = color_from_hex(value);
    if (json_string(body, "disconnected", value, sizeof(value))) p.disconnected = color_from_hex(value);
    p.blink_starting = json_bool(body, "blink_starting", p.blink_starting);
    p.blink_online = json_bool(body, "blink_online", p.blink_online);
    p.blink_on_battery = json_bool(body, "blink_on_battery", p.blink_on_battery);
    p.blink_low_battery = json_bool(body, "blink_low_battery", p.blink_low_battery);
    p.blink_fault = json_bool(body, "blink_fault", p.blink_fault);
    p.blink_disconnected = json_bool(body, "blink_disconnected", p.blink_disconnected);
    size_t brightness = body.find("\"brightness\":");
    if (brightness != std::string::npos) p.brightness = atoi(body.c_str() + brightness + 13);
    bool ok = settings_set_led_palette(p);
    return httpd_resp_sendstr(request, ok ? "Colores guardados." : "No se pudo guardar.");
}

static esp_err_t led_test_handler(httpd_req_t *request) {
    std::string body(request->content_len, '\0');
    if (httpd_req_recv(request, body.data(), body.size()) <= 0) return ESP_FAIL;
    char color[16] = {};
    if (!json_string(body, "color", color, sizeof(color))) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "Color no válido.");
    }
    status_led_test(color_from_hex(color), json_bool(body, "blink", false), 5000);
    return httpd_resp_sendstr(request, "Prueba iniciada durante 5 segundos.");
}

static bool valid_ipv4(const char *value) {
    esp_ip4_addr_t address = {};
    return value[0] != '\0' && esp_netif_str_to_ip4(value, &address) == ESP_OK;
}

static int form_hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static bool form_value(const std::string &body, const char *key,
                       char *output, size_t output_size) {
    char encoded[256] = {};
    output[0] = '\0';
    if (httpd_query_key_value(body.c_str(), key, encoded, sizeof(encoded)) != ESP_OK) {
        return false;
    }
    size_t written = 0;
    for (size_t read = 0; encoded[read] != '\0'; ++read) {
        unsigned char decoded = static_cast<unsigned char>(encoded[read]);
        if (encoded[read] == '+') {
            decoded = ' ';
        } else if (encoded[read] == '%' && encoded[read + 1] && encoded[read + 2]) {
            int high = form_hex_digit(encoded[read + 1]);
            int low = form_hex_digit(encoded[read + 2]);
            if (high < 0 || low < 0) return false;
            decoded = static_cast<unsigned char>((high << 4) | low);
            read += 2;
        }
        if (written + 1 >= output_size) return false;
        output[written++] = static_cast<char>(decoded);
    }
    output[written] = '\0';
    return true;
}

static esp_err_t network_get_handler(httpd_req_t *request) {
    NetworkSettings network = settings_network();
    char ssid[33] = {}, password[65] = {};
    settings_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password));
    std::string safe_ssid = ssid;
    for (char &character : safe_ssid) {
        if (character == '"' || character == '\\') character = '_';
    }
    std::string json = std::string("{\"mode\":\"") + (network.dhcp ? "dhcp" : "static") +
        "\",\"ssid\":\"" + safe_ssid + "\",\"ip\":\"" + network.ip + "\",\"gateway\":\"" + network.gateway +
        "\",\"netmask\":\"" + network.netmask + "\",\"dns\":\"" + network.dns + "\"}";
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, json.data(), json.size());
}

static esp_err_t wifi_post_handler(httpd_req_t *request) {
    std::string body(request->content_len + 1, '\0');
    int received = httpd_req_recv(request, body.data(), request->content_len);
    if (received <= 0) return ESP_FAIL;
    char ssid[33] = {}, password[65] = {};
    char mode[8] = "dhcp", ip[16] = {}, gateway[16] = {}, netmask[16] = {}, dns[16] = {};
    if (!form_value(body, "ssid", ssid, sizeof(ssid)) ||
        !form_value(body, "password", password, sizeof(password)) ||
        !form_value(body, "mode", mode, sizeof(mode)) ||
        !form_value(body, "ip", ip, sizeof(ip)) ||
        !form_value(body, "gateway", gateway, sizeof(gateway)) ||
        !form_value(body, "netmask", netmask, sizeof(netmask)) ||
        !form_value(body, "dns", dns, sizeof(dns))) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "La configuración contiene un valor no válido o demasiado largo.");
    }
    char current_ssid[33] = {}, current_password[65] = {};
    settings_wifi_credentials(current_ssid, sizeof(current_ssid),
                              current_password, sizeof(current_password));
    if (ssid[0] == '\0') strlcpy(ssid, current_ssid, sizeof(ssid));
    if (password[0] == '\0') strlcpy(password, current_password, sizeof(password));
    NetworkSettings network = settings_network();
    network.dhcp = strcmp(mode, "static") != 0;
    if (!network.dhcp) {
        if (!valid_ipv4(ip) || !valid_ipv4(gateway) || !valid_ipv4(netmask) || !valid_ipv4(dns)) {
            httpd_resp_set_status(request, "400 Bad Request");
            return httpd_resp_sendstr(request, "Revisa la IP, puerta de enlace, máscara y DNS.");
        }
        strlcpy(network.ip, ip, sizeof(network.ip));
        strlcpy(network.gateway, gateway, sizeof(network.gateway));
        strlcpy(network.netmask, netmask, sizeof(network.netmask));
        strlcpy(network.dns, dns, sizeof(network.dns));
    }
    if (!settings_set_wifi_credentials(ssid, password) || !settings_set_network(network)) {
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, "No se pudo guardar la red.");
    }
    httpd_resp_sendstr(request, "Configuración guardada. Reiniciando… El punto de acceso seguirá disponible para consultar la nueva IP.");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t wifi_scan_handler(httpd_req_t *request) {
    WifiScanState state = wifi_manager_scan_state();
    if (state == WifiScanState::idle || state == WifiScanState::failed) {
        state = wifi_manager_start_scan();
    }
    if (state == WifiScanState::running) {
        httpd_resp_set_status(request, "202 Accepted");
        httpd_resp_set_type(request, "application/json");
        return httpd_resp_sendstr(request, "[]");
    }
    if (state != WifiScanState::ready) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        httpd_resp_set_type(request, "application/json");
        return httpd_resp_sendstr(request, "[]");
    }

    WifiScanResult records[12] = {};
    size_t count = wifi_manager_scan_results(records, 12);
    std::string json = "[";
    for (size_t i = 0; i < count; ++i) {
        if (i) json += ",";
        std::string ssid(records[i].ssid);
        for (char &character : ssid) {
            if (character == '"' || character == '\\') character = '_';
        }
        json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + std::to_string(records[i].rssi) + "}";
    }
    json += "]";
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, json.data(), json.size());
}

static void delayed_restart_task(void *) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "Reinicio diferido después de OTA");
    esp_restart();
}

static esp_err_t ota_handler(httpd_req_t *request) {
    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    esp_ota_handle_t handle;
    if (!partition || esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &handle) != ESP_OK) return ESP_FAIL;
    char buffer[2048];
    int remaining = request->content_len;
    while (remaining > 0) {
        int chunk = remaining > static_cast<int>(sizeof(buffer)) ? sizeof(buffer) : remaining;
        int received = httpd_req_recv(request, buffer, chunk);
        if (received <= 0 || esp_ota_write(handle, buffer, received) != ESP_OK) {
            esp_ota_abort(handle);
            return ESP_FAIL;
        }
        remaining -= received;
    }
    if (esp_ota_end(handle) != ESP_OK || esp_ota_set_boot_partition(partition) != ESP_OK) return ESP_FAIL;
    esp_err_t response =
        httpd_resp_sendstr(request, "Firmware instalado. Reiniciando…");
    if (response != ESP_OK) return response;
    if (xTaskCreate(delayed_restart_task, "ota_restart", 2048, nullptr, 8, nullptr) !=
        pdPASS) {
        ESP_LOGE(TAG, "No se pudo programar el reinicio posterior a la OTA");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void web_server_start() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 16;
    httpd_handle_t server = nullptr;
    esp_err_t result = httpd_start(&server, &config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo iniciar el servidor web: %s", esp_err_to_name(result));
        return;
    }
    const httpd_uri_t routes[] = {
        {.uri="/", .method=HTTP_GET, .handler=index_handler, .user_ctx=nullptr},
        {.uri="/api/status", .method=HTTP_GET, .handler=status_handler, .user_ctx=nullptr},
        {.uri="/api/outages", .method=HTTP_GET, .handler=outage_history_handler, .user_ctx=nullptr},
        {.uri="/api/outages/clear", .method=HTTP_POST, .handler=clear_outage_history_handler, .user_ctx=nullptr},
        {.uri="/api/communication-events", .method=HTTP_GET, .handler=communication_history_handler, .user_ctx=nullptr},
        {.uri="/api/communication-events/clear", .method=HTTP_POST, .handler=clear_communication_history_handler, .user_ctx=nullptr},
        {.uri="/api/led", .method=HTTP_GET, .handler=led_get_handler, .user_ctx=nullptr},
        {.uri="/api/led", .method=HTTP_POST, .handler=led_post_handler, .user_ctx=nullptr},
        {.uri="/api/led/test", .method=HTTP_POST, .handler=led_test_handler, .user_ctx=nullptr},
        {.uri="/api/network", .method=HTTP_GET, .handler=network_get_handler, .user_ctx=nullptr},
        {.uri="/api/wifi", .method=HTTP_POST, .handler=wifi_post_handler, .user_ctx=nullptr},
        {.uri="/api/wifi/scan", .method=HTTP_GET, .handler=wifi_scan_handler, .user_ctx=nullptr},
        {.uri="/api/ota", .method=HTTP_POST, .handler=ota_handler, .user_ctx=nullptr},
    };
    for (const auto &route : routes) {
        result = httpd_register_uri_handler(server, &route);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "No se pudo registrar %s (%d): %s",
                     route.uri, route.method, esp_err_to_name(result));
        }
    }
    ESP_LOGI(TAG, "Servidor web iniciado con %u rutas",
             static_cast<unsigned>(sizeof(routes) / sizeof(routes[0])));
}
