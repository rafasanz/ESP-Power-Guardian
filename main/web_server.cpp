#include "web_server.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "guardian_state.h"
#include "settings.h"
#include "usb_monitor.h"
#include "wifi_manager.h"
#include "web_ui.h"

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

static esp_err_t index_handler(httpd_req_t *request) {
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, WEB_PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t status_handler(httpd_req_t *request) {
    GuardianSnapshot s = guardian_state_get();
    wifi_ap_record_t access_point = {};
    int rssi = esp_wifi_sta_get_ap_info(&access_point) == ESP_OK ? access_point.rssi : 0;
    int64_t uptime = esp_timer_get_time() / 1000000;
    int64_t age = s.last_update_ms > 0 ? (esp_timer_get_time() / 1000 - s.last_update_ms) / 1000 : -1;
    char json[960];
    snprintf(json, sizeof(json),
             "{\"firmware\":\"%s\",\"condition\":\"%s\",\"wifi\":\"%s\",\"ip\":\"%s\","
             "\"rssi\":%d,\"uptime_s\":%lld,\"nut_port\":3493,\"ups_status\":\"%s\","
             "\"usb_vid\":\"%04x\",\"usb_pid\":\"%04x\",\"qx_status\":\"%s\",\"qx_reply\":\"%s\","
             "\"data_valid\":%s,\"last_data_age_s\":%lld,\"input_voltage\":%.1f,"
             "\"output_voltage\":%.1f,\"frequency\":%.1f,\"battery_voltage\":%.1f,"
             "\"battery\":%d,\"runtime_s\":%d,\"load\":%d}",
             EPG_VERSION, guardian_condition_name(s.condition),
             wifi_manager_connected() ? "conectado" : "sin conexión", wifi_manager_ip(),
             rssi, static_cast<long long>(uptime), guardian_nut_status(s.condition),
             s.usb_vid, s.usb_pid, usb_monitor_status(), usb_monitor_reply(), s.data_valid ? "true" : "false",
             static_cast<long long>(age), s.input_voltage, s.output_voltage, s.frequency,
             s.battery_voltage, s.battery_percent, s.runtime_seconds, s.load_percent);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, json);
}

static esp_err_t led_get_handler(httpd_req_t *request) {
    LedPalette p = settings_led_palette();
    std::string json = "{\"starting\":\"" + hex_color(p.starting) + "\",\"online\":\"" +
        hex_color(p.online) + "\",\"on_battery\":\"" + hex_color(p.on_battery) +
        "\",\"low_battery\":\"" + hex_color(p.low_battery) + "\",\"fault\":\"" +
        hex_color(p.fault) + "\",\"disconnected\":\"" + hex_color(p.disconnected) +
        "\",\"brightness\":" + std::to_string(p.brightness) + "}";
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
    size_t brightness = body.find("\"brightness\":");
    if (brightness != std::string::npos) p.brightness = atoi(body.c_str() + brightness + 13);
    bool ok = settings_set_led_palette(p);
    return httpd_resp_sendstr(request, ok ? "Colores guardados." : "No se pudo guardar.");
}

static esp_err_t wifi_post_handler(httpd_req_t *request) {
    std::string body(request->content_len + 1, '\0');
    int received = httpd_req_recv(request, body.data(), request->content_len);
    if (received <= 0) return ESP_FAIL;
    char ssid[33] = {}, password[65] = {};
    httpd_query_key_value(body.c_str(), "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(body.c_str(), "password", password, sizeof(password));
    if (!settings_set_wifi_credentials(ssid, password)) {
        httpd_resp_set_status(request, "500 Internal Server Error");
        return httpd_resp_sendstr(request, "No se pudo guardar la red.");
    }
    httpd_resp_sendstr(request, "Configuración guardada. Reiniciando…");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t wifi_scan_handler(httpd_req_t *request) {
    wifi_scan_config_t scan = {};
    if (esp_wifi_scan_start(&scan, true) != ESP_OK) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "[]");
    }
    uint16_t count = 12;
    wifi_ap_record_t records[12] = {};
    esp_wifi_scan_get_ap_records(&count, records);
    std::string json = "[";
    for (uint16_t i = 0; i < count; ++i) {
        if (i) json += ",";
        std::string ssid(reinterpret_cast<const char *>(records[i].ssid));
        for (char &character : ssid) {
            if (character == '"' || character == '\\') character = '_';
        }
        json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + std::to_string(records[i].rssi) + "}";
    }
    json += "]";
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, json.data(), json.size());
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
    httpd_resp_sendstr(request, "Firmware instalado. Reiniciando…");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

void web_server_start() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    const httpd_uri_t routes[] = {
        {.uri="/", .method=HTTP_GET, .handler=index_handler},
        {.uri="/api/status", .method=HTTP_GET, .handler=status_handler},
        {.uri="/api/led", .method=HTTP_GET, .handler=led_get_handler},
        {.uri="/api/led", .method=HTTP_POST, .handler=led_post_handler},
        {.uri="/api/wifi", .method=HTTP_POST, .handler=wifi_post_handler},
        {.uri="/api/wifi/scan", .method=HTTP_GET, .handler=wifi_scan_handler},
        {.uri="/api/ota", .method=HTTP_POST, .handler=ota_handler},
    };
    for (const auto &route : routes) ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
}
