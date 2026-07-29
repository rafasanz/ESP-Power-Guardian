#include "nut_server.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "guardian_state.h"

static const char *TAG = "nut_server";
static constexpr const char *UPS_NAME = "guardian";

static std::vector<std::pair<std::string, std::string>> variables() {
    GuardianSnapshot s = guardian_state_get();
    char number[32];
    std::vector<std::pair<std::string, std::string>> vars = {
        {"device.mfr", "ESP Power Guardian"},
        {"device.model", "USB UPS"},
        {"device.type", "ups"},
        {"driver.name", "esp-power-guardian"},
        {"driver.version", EPG_VERSION},
        {"ups.status", guardian_nut_status(s.condition)},
    };
    snprintf(number, sizeof(number), "%.1f", s.input_voltage);
    vars.emplace_back("input.voltage", number);
    snprintf(number, sizeof(number), "%.1f", s.output_voltage);
    vars.emplace_back("output.voltage", number);
    snprintf(number, sizeof(number), "%.1f", s.battery_voltage);
    vars.emplace_back("battery.voltage", number);
    snprintf(number, sizeof(number), "%d", s.battery_percent);
    vars.emplace_back("battery.charge", number);
    snprintf(number, sizeof(number), "%d", s.runtime_seconds);
    vars.emplace_back("battery.runtime", number);
    snprintf(number, sizeof(number), "%d", s.load_percent);
    vars.emplace_back("ups.load", number);
    snprintf(number, sizeof(number), "%.1f", s.frequency);
    vars.emplace_back("input.frequency", number);
    return vars;
}

static void send_line(int socket, const std::string &line) {
    std::string output = line + "\n";
    send(socket, output.data(), output.size(), 0);
}

static std::string quoted(const std::string &value) {
    return "\"" + value + "\"";
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
        send_line(socket, std::string("UPS ") + UPS_NAME + " \"ESP Power Guardian\"");
        send_line(socket, "END LIST UPS");
        return;
    }
    if (line == std::string("GET UPSDESC ") + UPS_NAME) {
        send_line(socket, std::string("UPSDESC ") + UPS_NAME + " \"ESP Power Guardian\"");
        return;
    }
    if (line == std::string("LIST RW ") + UPS_NAME) {
        send_line(socket, std::string("BEGIN LIST RW ") + UPS_NAME);
        send_line(socket, std::string("END LIST RW ") + UPS_NAME);
        return;
    }
    if (line == std::string("LIST CMD ") + UPS_NAME) {
        send_line(socket, std::string("BEGIN LIST CMD ") + UPS_NAME);
        send_line(socket, std::string("END LIST CMD ") + UPS_NAME);
        return;
    }
    if (line == std::string("LIST VAR ") + UPS_NAME) {
        send_line(socket, std::string("BEGIN LIST VAR ") + UPS_NAME);
        for (const auto &var : variables()) {
            send_line(socket, std::string("VAR ") + UPS_NAME + " " + var.first + " " + quoted(var.second));
        }
        send_line(socket, std::string("END LIST VAR ") + UPS_NAME);
        return;
    }
    const std::string prefix = std::string("GET VAR ") + UPS_NAME + " ";
    if (line.rfind(prefix, 0) == 0) {
        std::string name = line.substr(prefix.size());
        for (const auto &var : variables()) {
            if (var.first == name) {
                send_line(socket, std::string("VAR ") + UPS_NAME + " " + name + " " + quoted(var.second));
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
            xTaskCreate(client_task, "nut_client", 4096,
                        reinterpret_cast<void *>(static_cast<intptr_t>(client)), 4, nullptr);
        }
    }
}

void nut_server_start() {
    xTaskCreate(server_task, "nut_server", 6144, nullptr, 4, nullptr);
}
