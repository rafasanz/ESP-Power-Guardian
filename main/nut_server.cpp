#include "nut_server.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "guardian_state.h"
#include "settings.h"

static const char *TAG = "nut_server";
static constexpr uint32_t MAX_NUT_CLIENTS = 4;
static portMUX_TYPE clients_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t active_clients;

static bool reserve_client() {
    bool reserved = false;
    portENTER_CRITICAL(&clients_mux);
    if (active_clients < MAX_NUT_CLIENTS) {
        active_clients++;
        reserved = true;
    }
    portEXIT_CRITICAL(&clients_mux);
    return reserved;
}

static void release_client() {
    portENTER_CRITICAL(&clients_mux);
    if (active_clients > 0) active_clients--;
    portEXIT_CRITICAL(&clients_mux);
}

struct NutVariable {
    const char *name;
    char value[48];
};

static void set_variable(NutVariable &variable, const char *name,
                         const char *value) {
    variable.name = name;
    strlcpy(variable.value, value, sizeof(variable.value));
}

static size_t variables(NutVariable *vars, size_t capacity) {
    if (capacity < 16) return 0;
    GuardianSnapshot s = guardian_state_get();
    char number[32];
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_text[18] = {};
    snprintf(mac_text, sizeof(mac_text), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    size_t count = 0;
    set_variable(vars[count++], "device.mfr", "ESP Power Guardian");
    set_variable(vars[count++], "device.model", "ESP32-S3 UPS Gateway");
    set_variable(vars[count++], "device.type", "ups");
    set_variable(vars[count++], "device.serial", mac_text);
    set_variable(vars[count++], "device.macaddr", mac_text);
    set_variable(vars[count++], "driver.name", "esp-power-guardian");
    set_variable(vars[count++], "driver.version", EPG_VERSION);
    set_variable(vars[count++], "ups.firmware", EPG_VERSION);
    set_variable(vars[count++], "ups.status", guardian_nut_status(s.condition));
    if (!s.data_valid) return count;
    snprintf(number, sizeof(number), "%.1f", s.input_voltage);
    set_variable(vars[count++], "input.voltage", number);
    snprintf(number, sizeof(number), "%.1f", s.output_voltage);
    set_variable(vars[count++], "output.voltage", number);
    snprintf(number, sizeof(number), "%.1f", s.battery_voltage);
    set_variable(vars[count++], "battery.voltage", number);
    snprintf(number, sizeof(number), "%d", s.battery_percent);
    set_variable(vars[count++], "battery.charge", number);
    snprintf(number, sizeof(number), "%d", s.runtime_seconds);
    set_variable(vars[count++], "battery.runtime", number);
    snprintf(number, sizeof(number), "%d", s.load_percent);
    set_variable(vars[count++], "ups.load", number);
    snprintf(number, sizeof(number), "%.1f", s.frequency);
    set_variable(vars[count++], "input.frequency", number);
    return count;
}

static bool send_line(int socket, const std::string &line) {
    std::string output = line + "\n";
    size_t sent = 0;
    while (sent < output.size()) {
        int result = send(socket, output.data() + sent, output.size() - sent, 0);
        if (result <= 0) return false;
        sent += static_cast<size_t>(result);
    }
    return true;
}

static void send_variable(int socket, const NutVariable &variable) {
    char line[160] = {};
    snprintf(line, sizeof(line), "VAR %s %s \"%s\"", settings_nut_name(),
             variable.name, variable.value);
    send_line(socket, line);
}

static void process(int socket, std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line == "VER") {
        send_line(socket, "Network UPS Tools upsd " EPG_VERSION);
        return;
    }
    if (line == "NETVER") {
        send_line(socket, "1.3");
        return;
    }
    if (line == "LIST UPS") {
        send_line(socket, "BEGIN LIST UPS");
        send_line(socket, std::string("UPS ") + settings_nut_name() + " \"" +
                  settings_device_name() + "\"");
        send_line(socket, "END LIST UPS");
        return;
    }
    if (line == std::string("GET UPSDESC ") + settings_nut_name()) {
        send_line(socket, std::string("UPSDESC ") + settings_nut_name() + " \"" +
                  settings_device_name() + "\"");
        return;
    }
    if (line == std::string("LIST RW ") + settings_nut_name()) {
        send_line(socket, std::string("BEGIN LIST RW ") + settings_nut_name());
        send_line(socket, std::string("END LIST RW ") + settings_nut_name());
        return;
    }
    if (line == std::string("LIST CMD ") + settings_nut_name()) {
        send_line(socket, std::string("BEGIN LIST CMD ") + settings_nut_name());
        send_line(socket, std::string("END LIST CMD ") + settings_nut_name());
        return;
    }
    if (line == std::string("LIST VAR ") + settings_nut_name()) {
        send_line(socket, std::string("BEGIN LIST VAR ") + settings_nut_name());
        NutVariable vars[16] = {};
        size_t count = variables(vars, 16);
        for (size_t i = 0; i < count; ++i) send_variable(socket, vars[i]);
        send_line(socket, std::string("END LIST VAR ") + settings_nut_name());
        return;
    }
    const std::string prefix = std::string("GET VAR ") + settings_nut_name() + " ";
    if (line.rfind(prefix, 0) == 0) {
        std::string name = line.substr(prefix.size());
        NutVariable vars[16] = {};
        size_t count = variables(vars, 16);
        for (size_t i = 0; i < count; ++i) {
            if (name == vars[i].name) {
                send_variable(socket, vars[i]);
                return;
            }
        }
        send_line(socket, "ERR VAR-NOT-SUPPORTED");
        return;
    }
    if (line.rfind("USERNAME ", 0) == 0 || line.rfind("PASSWORD ", 0) == 0) {
        send_line(socket, "OK");
        return;
    }
    if (line == "LOGOUT") {
        send_line(socket, "OK Goodbye");
        shutdown(socket, SHUT_RDWR);
        return;
    }
    send_line(socket, "ERR UNKNOWN-COMMAND");
}

static void client_task(void *arg) {
    int socket = static_cast<int>(reinterpret_cast<intptr_t>(arg));
    char buffer[512];
    std::string pending;
    while (true) {
        int received = recv(socket, buffer, sizeof(buffer), 0);
        if (received <= 0) break;
        pending.append(buffer, received);
        size_t newline;
        while ((newline = pending.find('\n')) != std::string::npos) {
            process(socket, pending.substr(0, newline + 1));
            pending.erase(0, newline + 1);
        }
    }
    shutdown(socket, SHUT_RDWR);
    close(socket);
    release_client();
    vTaskDelete(nullptr);
}

static void server_task(void *) {
    int server = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(3493);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    listen(server, 4);
    ESP_LOGI(TAG, "Servidor NUT escuchando en el puerto 3493");
    while (true) {
        int client = accept(server, nullptr, nullptr);
        if (client >= 0) {
            if (!reserve_client()) {
                ESP_LOGW(TAG, "Límite de clientes NUT alcanzado");
                shutdown(client, SHUT_RDWR);
                close(client);
                continue;
            }
            if (xTaskCreate(client_task, "nut_client", 7168,
                            reinterpret_cast<void *>(static_cast<intptr_t>(client)),
                            4, nullptr) != pdPASS) {
                ESP_LOGE(TAG, "No hay memoria para atender un cliente NUT");
                close(client);
                release_client();
            }
        }
    }
}

void nut_server_start() {
    xTaskCreate(server_task, "nut_server", 6144, nullptr, 4, nullptr);
}
