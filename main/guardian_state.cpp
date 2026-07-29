#include "guardian_state.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t state_mutex;
static GuardianSnapshot state;

void guardian_state_init() {
    state_mutex = xSemaphoreCreateMutex();
    state = {
        .condition = PowerCondition::Starting,
        .data_valid = false,
        .usb_vid = 0,
        .usb_pid = 0,
        .input_voltage = 0,
        .output_voltage = 0,
        .battery_voltage = 0,
        .frequency = 0,
        .load_percent = 0,
        .battery_percent = 0,
        .runtime_seconds = 0,
        .last_update_ms = 0,
    };
}

GuardianSnapshot guardian_state_get() {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    GuardianSnapshot copy = state;
    xSemaphoreGive(state_mutex);
    return copy;
}

void guardian_state_update(const GuardianSnapshot &snapshot) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    state = snapshot;
    xSemaphoreGive(state_mutex);
}

const char *guardian_nut_status(PowerCondition condition) {
    switch (condition) {
        case PowerCondition::Online: return "OL";
        case PowerCondition::OnBattery: return "OB";
        case PowerCondition::LowBattery: return "OB LB";
        case PowerCondition::Fault: return "ALARM";
        default: return "OFF";
    }
}

const char *guardian_condition_name(PowerCondition condition) {
    switch (condition) {
        case PowerCondition::Starting: return "iniciando";
        case PowerCondition::Online: return "en línea";
        case PowerCondition::OnBattery: return "funcionando con batería";
        case PowerCondition::LowBattery: return "batería baja";
        case PowerCondition::Fault: return "alarma";
        case PowerCondition::Disconnected: return "SAI desconectado";
    }
    return "desconocido";
}

