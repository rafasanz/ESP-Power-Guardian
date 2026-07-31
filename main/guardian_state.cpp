#include "guardian_state.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include <ctime>

static SemaphoreHandle_t state_mutex;
static GuardianSnapshot state;
static GuardianOutage outage_history[10];
static size_t outage_count;
static constexpr const char *OUTAGE_NAMESPACE = "guardian";

struct StoredOutages {
    uint32_t version;
    uint32_t count;
    GuardianOutage entries[10];
};

static bool is_outage(PowerCondition condition) {
    return condition == PowerCondition::OnBattery ||
           condition == PowerCondition::LowBattery;
}

static int64_t now_epoch() {
    const time_t now = time(nullptr);
    return now >= 1700000000 ? static_cast<int64_t>(now) : 0;
}

static void persist_outages() {
    StoredOutages stored = {};
    stored.version = 1;
    stored.count = static_cast<uint32_t>(outage_count);
    for (size_t i = 0; i < outage_count; ++i) stored.entries[i] = outage_history[i];
    nvs_handle_t nvs;
    if (nvs_open(OUTAGE_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return;
    if (nvs_set_blob(nvs, "outages", &stored, sizeof(stored)) == ESP_OK) nvs_commit(nvs);
    nvs_close(nvs);
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
    StoredOutages stored = {};
    size_t stored_size = sizeof(stored);
    nvs_handle_t nvs;
    if (nvs_open(OUTAGE_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        if (nvs_get_blob(nvs, "outages", &stored, &stored_size) == ESP_OK &&
            stored.version == 1 && stored.count <= 10) {
            outage_count = stored.count;
            for (size_t i = 0; i < outage_count; ++i) outage_history[i] = stored.entries[i];
        }
        nvs_close(nvs);
    }
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
    const int64_t now = now_epoch();
    bool changed = false;
    if (!was_outage && is_now_outage) {
        if (outage_count > 0 && outage_history[outage_count - 1].ended_epoch == 0) {
            outage_history[outage_count - 1].condition = snapshot.condition;
        } else {
            if (outage_count == 10) {
                for (size_t i = 1; i < outage_count; ++i) outage_history[i - 1] = outage_history[i];
                --outage_count;
            }
            outage_history[outage_count++] = {
                .started_epoch = now,
                .ended_epoch = 0,
                .condition = snapshot.condition,
            };
        }
        changed = true;
    } else if (was_outage && is_now_outage && outage_count > 0) {
        if (outage_history[outage_count - 1].condition != snapshot.condition) {
            outage_history[outage_count - 1].condition = snapshot.condition;
            changed = true;
        }
    } else if (!is_now_outage && outage_count > 0 &&
               outage_history[outage_count - 1].ended_epoch == 0) {
        outage_history[outage_count - 1].ended_epoch = now;
        changed = true;
    }
    state = snapshot;
    xSemaphoreGive(state_mutex);
    if (changed) persist_outages();
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
