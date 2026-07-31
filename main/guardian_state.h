#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum class PowerCondition {
    Starting,
    Online,
    OnBattery,
    LowBattery,
    Fault,
    Disconnected
};

struct GuardianSnapshot {
    PowerCondition condition;
    bool data_valid;
    uint16_t usb_vid;
    uint16_t usb_pid;
    float input_voltage;
    float output_voltage;
    float battery_voltage;
    float frequency;
    int load_percent;
    int battery_percent;
    int runtime_seconds;
    int64_t last_update_ms;
};

struct GuardianOutage {
    int64_t started_epoch;
    int64_t ended_epoch;
    PowerCondition condition;
};

void guardian_state_init();
GuardianSnapshot guardian_state_get();
void guardian_state_update(const GuardianSnapshot &snapshot);
size_t guardian_outage_history(GuardianOutage *outages, size_t capacity);
const char *guardian_nut_status(PowerCondition condition);
const char *guardian_condition_name(PowerCondition condition);
