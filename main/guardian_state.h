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
    CommunicationLost,
    Recovering,
    Disconnected
};

enum class CommunicationEventType {
    Lost,
    Restored,
    Recovery,
    AutomaticRestart,
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
    int64_t monitoring_started_ms;
    int64_t last_error_ms;
    uint32_t consecutive_failures;
    uint32_t recovery_count;
    uint32_t automatic_restarts;
    int last_usb_status;
};

struct GuardianOutage {
    int64_t started_epoch;
    int64_t ended_epoch;
    PowerCondition condition;
};

struct GuardianCommunicationEvent {
    int64_t epoch;
    CommunicationEventType type;
    int detail;
};

void guardian_state_init();
GuardianSnapshot guardian_state_get();
void guardian_state_update(const GuardianSnapshot &snapshot);
size_t guardian_outage_history(GuardianOutage *outages, size_t capacity);
void guardian_record_communication_event(CommunicationEventType type, int detail = 0);
size_t guardian_communication_history(GuardianCommunicationEvent *events, size_t capacity);
const char *guardian_nut_status(PowerCondition condition);
const char *guardian_condition_name(PowerCondition condition);
const char *guardian_communication_event_name(CommunicationEventType type);
