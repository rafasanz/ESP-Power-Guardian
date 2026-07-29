#include "guardian_state.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t state_mutex;
static GuardianSnapshot state;
static GuardianOutage outage_history[10];
static size_t outage_count;

static bool is_outage(PowerCondition condition) {
    return condition == PowerCondition::OnBattery ||
           condition == PowerCondition::LowBattery;
}

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
    outage_count = 0;
}

GuardianSnapshot guardian_state_get() {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    GuardianSnapshot copy = state;
    xSemaphoreGive(state_mutex);
    return copy;
}

void guardian_state_update(const GuardianSnapshot &snapshot) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    const bool was_outage = is_outage(state.condition);
    const bool is_now_outage = is_outage(snapshot.condition);
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (!was_outage && is_now_outage) {
        if (outage_count == 10) {
            for (size_t i = 1; i < outage_count; ++i) outage_history[i - 1] = outage_history[i];
            --outage_count;
        }
        outage_history[outage_count++] = {
            .started_ms = now_ms,
            .ended_ms = 0,
            .condition = snapshot.condition,
        };
    } else if (was_outage && is_now_outage && outage_count > 0) {
        outage_history[outage_count - 1].condition = snapshot.condition;
    } else if (was_outage && !is_now_outage && outage_count > 0 &&
               outage_history[outage_count - 1].ended_ms == 0) {
        outage_history[outage_count - 1].ended_ms = now_ms;
    }
    state = snapshot;
    xSemaphoreGive(state_mutex);
}

size_t guardian_outage_history(GuardianOutage *outages, size_t capacity) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    size_t count = outage_count < capacity ? outage_count : capacity;
    for (size_t i = 0; i < count; ++i) outages[i] = outage_history[i];
    xSemaphoreGive(state_mutex);
    return count;
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
