#include "usb_monitor.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#include "guardian_state.h"

static const char *TAG = "usb_qx";
static constexpr uint16_t SUPPORTED_VID = 0x0665;
static constexpr uint16_t SUPPORTED_PID = 0x5161;
static constexpr uint8_t QX_INTERFACE = 0;
static constexpr uint8_t QX_INPUT_ENDPOINT = 0x81;
static constexpr const char *QX_PROBES[] = {"Q1\r", "QGS\r", "QS\r", "F\r", "I\r"};
static constexpr int64_t POLL_INTERVAL_MS = 1000;
static constexpr int64_t QUERY_TIMEOUT_MS = 2500;
static constexpr int64_t STALE_AFTER_MS = 4000;
static constexpr int64_t RECOVERY_TIMEOUT_MS = 1800;
static constexpr int64_t CLIENT_WATCHDOG_MS = 8000;
static constexpr uint32_t MAX_CONSECUTIVE_OVERFLOWS = 3;
static constexpr uint32_t MAX_CONSECUTIVE_FAILURES = 6;
static constexpr uint32_t MAX_RECOVERY_RESTARTS = 3;

static char qx_status[128] = "esperando dispositivo USB";
static char last_qx_reply[128] = "";
static portMUX_TYPE diagnostics_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE heartbeat_mux = portMUX_INITIALIZER_UNLOCKED;
static int64_t client_heartbeat_ms;
static bool client_started;
static bool maintenance_mode;
static constexpr uint32_t RECOVERY_RTC_MAGIC = 0x45504752;

struct RecoveryRtcState {
    uint32_t magic;
    uint32_t restart_streak;
    uint32_t restart_total;
};

// RTC_NOINIT conserva los contadores durante esp_restart(). RTC_DATA_ATTR se
// reinicializaba con la imagen y anulaba, en la práctica, el límite antibucle.
RTC_NOINIT_ATTR static RecoveryRtcState recovery_rtc;

struct UsbClient {
    usb_host_client_handle_t client;
    usb_device_handle_t device;
    usb_transfer_t *control;
    usb_transfer_t *input;
    uint8_t address;
    bool open_requested;
    bool close_requested;
    bool interface_claimed;
    bool control_in_flight;
    bool input_in_flight;
    bool control_event;
    bool input_event;
    bool query_active;
    bool recovery_pending;
    usb_transfer_status_t control_status;
    usb_transfer_status_t input_status;
    int input_size;
    char reply[128];
    size_t reply_size;
    int64_t next_query_ms;
    int64_t query_deadline_ms;
    int64_t recovery_deadline_ms;
    int64_t close_requested_ms;
    uint32_t overflow_streak;
    size_t probe_index;
};

static int64_t monotonic_ms() {
    return esp_timer_get_time() / 1000;
}

static void update_client_heartbeat(bool started = true) {
    portENTER_CRITICAL(&heartbeat_mux);
    client_heartbeat_ms = monotonic_ms();
    client_started = started;
    portEXIT_CRITICAL(&heartbeat_mux);
}

void usb_monitor_set_maintenance(bool enabled) {
    portENTER_CRITICAL(&heartbeat_mux);
    maintenance_mode = enabled;
    client_heartbeat_ms = monotonic_ms();
    portEXIT_CRITICAL(&heartbeat_mux);
}

static void set_status(const char *format, ...) {
    char text[sizeof(qx_status)] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    portENTER_CRITICAL(&diagnostics_mux);
    strlcpy(qx_status, text, sizeof(qx_status));
    portEXIT_CRITICAL(&diagnostics_mux);
}

static void set_reply(const char *reply) {
    portENTER_CRITICAL(&diagnostics_mux);
    strlcpy(last_qx_reply, reply ? reply : "", sizeof(last_qx_reply));
    portEXIT_CRITICAL(&diagnostics_mux);
}

void usb_monitor_copy_status(char *output, size_t output_size) {
    if (!output || output_size == 0) return;
    portENTER_CRITICAL(&diagnostics_mux);
    strlcpy(output, qx_status, output_size);
    portEXIT_CRITICAL(&diagnostics_mux);
}

void usb_monitor_copy_reply(char *output, size_t output_size) {
    if (!output || output_size == 0) return;
    portENTER_CRITICAL(&diagnostics_mux);
    strlcpy(output, last_qx_reply, output_size);
    portEXIT_CRITICAL(&diagnostics_mux);
}

static void client_event(const usb_host_client_event_msg_t *message, void *argument) {
    auto *context = static_cast<UsbClient *>(argument);
    if (message->event == USB_HOST_CLIENT_EVENT_NEW_DEV && context->device == nullptr) {
        context->address = message->new_dev.address;
        context->open_requested = true;
    } else if (message->event == USB_HOST_CLIENT_EVENT_DEV_GONE &&
               message->dev_gone.dev_hdl == context->device) {
        context->close_requested = true;
        context->close_requested_ms = monotonic_ms();
    }
}

static void control_finished(usb_transfer_t *transfer) {
    auto *context = static_cast<UsbClient *>(transfer->context);
    context->control_status = transfer->status;
    context->control_in_flight = false;
    context->control_event = true;
}

static void input_finished(usb_transfer_t *transfer) {
    auto *context = static_cast<UsbClient *>(transfer->context);
    context->input_status = transfer->status;
    context->input_size = transfer->actual_num_bytes;
    context->input_in_flight = false;
    context->input_event = true;
}

static void daemon_task(void *) {
    while (true) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
    }
}

static void publish_disconnected(bool record_event) {
    GuardianSnapshot snapshot = guardian_state_get();
    if (record_event && (snapshot.usb_vid != 0 || snapshot.data_valid)) {
        guardian_record_communication_event(CommunicationEventType::Disconnected);
    }
    snapshot.condition = PowerCondition::Disconnected;
    snapshot.data_valid = false;
    snapshot.usb_vid = 0;
    snapshot.usb_pid = 0;
    snapshot.consecutive_failures = 0;
    snapshot.last_usb_status = USB_TRANSFER_STATUS_NO_DEVICE;
    snapshot.automatic_restarts = recovery_rtc.restart_total;
    guardian_state_update(snapshot);
    set_status("esperando dispositivo USB");
    set_reply("");
}

static int estimate_battery(float voltage) {
    float percentage = (voltage - 10.5f) * 100.0f / 3.1f;
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;
    return static_cast<int>(percentage + 0.5f);
}

static int estimate_runtime(int battery_percent, int load_percent) {
    int effective_load = load_percent < 10 ? 10 : load_percent;
    float full_runtime_minutes = 20.0f * 10.0f / effective_load;
    if (full_runtime_minutes > 20.0f) full_runtime_minutes = 20.0f;
    return static_cast<int>(full_runtime_minutes * 60.0f * battery_percent / 100.0f);
}

static bool parse_qx_reply(const char *reply) {
    float input = 0, input_fault = 0, output = 0, frequency = 0, battery = 0;
    int load = 0;
    char temperature[8] = {};
    char flags[9] = {};
    int fields = sscanf(reply, "(%f %f %f %d %f %f %7s %8s",
                        &input, &input_fault, &output, &load, &frequency,
                        &battery, temperature, flags);
    if (fields < 8 || strlen(flags) < 8 || input < 0 || input > 300 ||
        output < 0 || output > 300 || load < 0 || load > 200 ||
        frequency < 0 || frequency > 100 || battery < 0 || battery > 100) {
        ESP_LOGW(TAG, "Respuesta Qx no reconocida: %s", reply);
        return false;
    }

    GuardianSnapshot snapshot = guardian_state_get();
    const bool communication_was_lost = !snapshot.data_valid && snapshot.last_update_ms > 0;
    const int64_t now = monotonic_ms();
    snapshot.data_valid = true;
    snapshot.input_voltage = input;
    snapshot.output_voltage = output;
    snapshot.load_percent = load;
    snapshot.frequency = frequency;
    snapshot.battery_voltage = battery;
    snapshot.battery_percent = estimate_battery(battery);
    snapshot.runtime_seconds = estimate_runtime(snapshot.battery_percent, load);
    snapshot.last_update_ms = now;
    if (snapshot.monitoring_started_ms == 0) snapshot.monitoring_started_ms = now;
    snapshot.consecutive_failures = 0;
    snapshot.last_usb_status = USB_TRANSFER_STATUS_COMPLETED;
    snapshot.automatic_restarts = recovery_rtc.restart_total;
    if (flags[3] == '1') {
        snapshot.condition = PowerCondition::Fault;
    } else if (flags[1] == '1') {
        snapshot.condition = PowerCondition::LowBattery;
    } else if (flags[0] == '1') {
        snapshot.condition = PowerCondition::OnBattery;
    } else {
        snapshot.condition = PowerCondition::Online;
    }
    guardian_state_update(snapshot);
    if (communication_was_lost) {
        guardian_record_communication_event(CommunicationEventType::Restored);
    }
    recovery_rtc.restart_streak = 0;
    set_status("comunicación Qx estable");
    return true;
}

static bool allocate_transfers(UsbClient &context) {
    if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + 8, 0, &context.control) != ESP_OK) {
        set_status("no se pudo reservar la transferencia de control");
        return false;
    }
    if (usb_host_transfer_alloc(8, 0, &context.input) != ESP_OK) {
        usb_host_transfer_free(context.control);
        context.control = nullptr;
        set_status("no se pudo reservar la transferencia de entrada");
        return false;
    }
    context.control->device_handle = context.device;
    context.control->callback = control_finished;
    context.control->context = &context;
    context.input->device_handle = context.device;
    context.input->bEndpointAddress = QX_INPUT_ENDPOINT;
    context.input->callback = input_finished;
    context.input->context = &context;
    context.input->num_bytes = 8;
    return true;
}

static bool submit_input(UsbClient &context) {
    context.input->num_bytes = 8;
    esp_err_t result = usb_host_transfer_submit(context.input);
    if (result == ESP_OK) context.input_in_flight = true;
    return result == ESP_OK;
}

static bool submit_command(UsbClient &context) {
    auto *setup = reinterpret_cast<usb_setup_packet_t *>(context.control->data_buffer);
    setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT |
                           USB_BM_REQUEST_TYPE_TYPE_CLASS |
                           USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
    setup->bRequest = 0x09;
    setup->wValue = 0x0200;
    setup->wIndex = QX_INTERFACE;
    setup->wLength = 8;
    uint8_t *payload = context.control->data_buffer + sizeof(usb_setup_packet_t);
    memset(payload, 0, 8);
    const char *command = QX_PROBES[context.probe_index];
    memcpy(payload, command, strlen(command));
    context.control->num_bytes = sizeof(usb_setup_packet_t) + 8;
    esp_err_t result = usb_host_transfer_submit_control(context.client, context.control);
    if (result == ESP_OK) context.control_in_flight = true;
    return result == ESP_OK;
}

static bool begin_query(UsbClient &context, int64_t now_ms) {
    if (context.control_in_flight || context.input_in_flight || context.recovery_pending) return false;
    context.reply_size = 0;
    memset(context.reply, 0, sizeof(context.reply));
    context.query_active = true;
    context.query_deadline_ms = now_ms + QUERY_TIMEOUT_MS;
    context.next_query_ms = 0;
    // El subcontrolador Cypress de NUT escribe primero el SET_REPORT y solo
    // después arma EP81. Armar EP81 antes podía recoger el informe saliente
    // ("Q1\r") como si fuese una respuesta y dejar la lectura real bloqueada.
    if (!submit_command(context)) {
        context.query_active = false;
        return false;
    }
    return true;
}

static void start_endpoint_recovery(UsbClient &context, usb_transfer_status_t status,
                                    int64_t now_ms) {
    GuardianSnapshot snapshot = guardian_state_get();
    snapshot.recovery_count++;
    snapshot.last_usb_status = status;
    snapshot.last_error_ms = now_ms;
    if (!snapshot.data_valid || snapshot.consecutive_failures >= 3) {
        snapshot.condition = PowerCondition::Recovering;
        snapshot.data_valid = false;
    }
    guardian_state_update(snapshot);
    guardian_record_communication_event(CommunicationEventType::Recovery, status);

    context.query_active = false;
    context.next_query_ms = 0;
    context.recovery_pending = true;
    context.recovery_deadline_ms = now_ms + RECOVERY_TIMEOUT_MS;
    // halt/flush/clear pueden bloquear según ESP-IDF. Esperamos brevemente el
    // callback pendiente y, si no llega, hacemos un reinicio controlado. Así la
    // monitorización nunca queda congelada dentro de esas llamadas síncronas.
    ESP_LOGW(TAG, "Recuperación EP81 no bloqueante (estado=%d)", status);
}

static bool automatic_usb_restart(usb_transfer_status_t status) {
    GuardianSnapshot snapshot = guardian_state_get();
    snapshot.condition = PowerCondition::Recovering;
    snapshot.data_valid = false;
    snapshot.last_usb_status = status;
    snapshot.last_error_ms = monotonic_ms();
    if (recovery_rtc.restart_streak >= MAX_RECOVERY_RESTARTS) {
        snapshot.condition = PowerCondition::CommunicationLost;
        guardian_state_update(snapshot);
        set_status("recuperación automática agotada; requiere reinicio manual");
        ESP_LOGE(TAG, "Se alcanzó el límite de reinicios USB automáticos");
        return false;
    }
    recovery_rtc.restart_streak++;
    recovery_rtc.restart_total++;
    snapshot.automatic_restarts = recovery_rtc.restart_total;
    guardian_state_update(snapshot);
    guardian_record_communication_event(CommunicationEventType::AutomaticRestart, status);
    set_status("reiniciando el bus USB tras errores persistentes");
    ESP_LOGE(TAG, "Reinicio controlado %lu/%lu por error USB %d",
             static_cast<unsigned long>(recovery_rtc.restart_streak),
             static_cast<unsigned long>(MAX_RECOVERY_RESTARTS), status);
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_restart();
    return true;
}

static void client_watchdog_task(void *) {
    while (true) {
        int64_t heartbeat = 0;
        bool started = false;
        bool maintenance = false;
        portENTER_CRITICAL(&heartbeat_mux);
        heartbeat = client_heartbeat_ms;
        started = client_started;
        maintenance = maintenance_mode;
        portEXIT_CRITICAL(&heartbeat_mux);
        const int64_t now_ms = monotonic_ms();
        if (!maintenance && started && heartbeat > 0 &&
            now_ms - heartbeat >= CLIENT_WATCHDOG_MS) {
            set_status("tarea USB bloqueada; reinicio de seguridad");
            ESP_LOGE(TAG, "Watchdog USB: sin actividad durante %lld ms",
                     static_cast<long long>(now_ms - heartbeat));
            if (!automatic_usb_restart(USB_TRANSFER_STATUS_TIMED_OUT)) {
                vTaskDelete(nullptr);
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void handle_failure(UsbClient &context, usb_transfer_status_t status,
                           int64_t now_ms, const char *reason) {
    GuardianSnapshot snapshot = guardian_state_get();
    const bool was_valid = snapshot.data_valid;
    snapshot.consecutive_failures++;
    snapshot.last_usb_status = status;
    snapshot.last_error_ms = now_ms;
    snapshot.automatic_restarts = recovery_rtc.restart_total;
    if (status == USB_TRANSFER_STATUS_OVERFLOW) {
        context.overflow_streak++;
    } else {
        context.overflow_streak = 0;
    }
    if (snapshot.consecutive_failures >= 3 ||
        (snapshot.last_update_ms > 0 && now_ms - snapshot.last_update_ms >= STALE_AFTER_MS)) {
        snapshot.data_valid = false;
        snapshot.condition = PowerCondition::CommunicationLost;
    }
    guardian_state_update(snapshot);
    if (was_valid && !snapshot.data_valid) {
        guardian_record_communication_event(CommunicationEventType::Lost, status);
    }
    set_status("%s; fallo %lu", reason,
               static_cast<unsigned long>(snapshot.consecutive_failures));
    ESP_LOGW(TAG, "%s (estado=%d, consecutivos=%lu, overflow=%lu)", reason, status,
             static_cast<unsigned long>(snapshot.consecutive_failures),
             static_cast<unsigned long>(context.overflow_streak));

    if (context.overflow_streak >= MAX_CONSECUTIVE_OVERFLOWS ||
        snapshot.consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
        if (!automatic_usb_restart(status)) {
            context.overflow_streak = 0;
            start_endpoint_recovery(context, status, now_ms);
            context.recovery_deadline_ms = now_ms + 10000;
        }
        return;
    }
    start_endpoint_recovery(context, status, now_ms);
}

static bool append_input(UsbClient &context) {
    if (context.input_size <= 0) return false;
    size_t available = sizeof(context.reply) - context.reply_size - 1;
    size_t amount = static_cast<size_t>(context.input_size);
    if (amount > available) return false;
    memcpy(context.reply + context.reply_size, context.input->data_buffer, amount);
    context.reply_size += amount;
    context.reply[context.reply_size] = '\0';
    return true;
}

enum class ReplyResult {
    Incomplete,
    Valid,
    CypressEcho,
};

static ReplyResult process_reply(UsbClient &context) {
    char *frame = strchr(context.reply, '(');
    if (frame) {
        char *end = strchr(frame, '\r');
        if (end) {
            end[1] = '\0';
            set_reply(frame);
            return parse_qx_reply(frame) ? ReplyResult::Valid : ReplyResult::Incomplete;
        }
    }
    if (strchr(context.reply, '\r')) {
        // En 0665:5161 el primer informe de entrada puede ser el eco HID del
        // SET_REPORT. La respuesta serie aparece al cebar una nueva consulta;
        // dejar una lectura EP81 pendiente aquí termina siempre en timeout.
        set_reply(context.reply);
        context.reply_size = 0;
        memset(context.reply, 0, sizeof(context.reply));
        return ReplyResult::CypressEcho;
    }
    return ReplyResult::Incomplete;
}

static void mark_stale_if_needed(int64_t now_ms) {
    GuardianSnapshot snapshot = guardian_state_get();
    if (!snapshot.data_valid || snapshot.last_update_ms <= 0 ||
        now_ms - snapshot.last_update_ms < STALE_AFTER_MS) return;
    snapshot.data_valid = false;
    snapshot.condition = PowerCondition::CommunicationLost;
    snapshot.last_error_ms = now_ms;
    guardian_state_update(snapshot);
    guardian_record_communication_event(CommunicationEventType::Lost,
                                         USB_TRANSFER_STATUS_TIMED_OUT);
    set_status("datos Qx obsoletos; iniciando recuperación");
}

static void release_device(UsbClient &context) {
    if (context.control) {
        usb_host_transfer_free(context.control);
        context.control = nullptr;
    }
    if (context.input) {
        usb_host_transfer_free(context.input);
        context.input = nullptr;
    }
    if (context.interface_claimed) {
        usb_host_interface_release(context.client, context.device, QX_INTERFACE);
        context.interface_claimed = false;
    }
    if (context.device) {
        usb_host_device_close(context.client, context.device);
        context.device = nullptr;
    }
    context.address = 0;
    context.query_active = false;
    context.recovery_pending = false;
    context.close_requested = false;
    context.control_in_flight = false;
    context.input_in_flight = false;
    publish_disconnected(true);
}

static void open_device(UsbClient &context, int64_t now_ms) {
    context.open_requested = false;
    if (usb_host_device_open(context.client, context.address, &context.device) != ESP_OK) {
        set_status("no se pudo abrir el dispositivo USB");
        return;
    }
    const usb_device_desc_t *descriptor = nullptr;
    if (usb_host_get_device_descriptor(context.device, &descriptor) != ESP_OK) {
        set_status("no se pudo leer el descriptor USB");
        usb_host_device_close(context.client, context.device);
        context.device = nullptr;
        return;
    }
    GuardianSnapshot snapshot = guardian_state_get();
    snapshot.usb_vid = descriptor->idVendor;
    snapshot.usb_pid = descriptor->idProduct;
    snapshot.condition = PowerCondition::Starting;
    snapshot.data_valid = false;
    snapshot.automatic_restarts = recovery_rtc.restart_total;
    guardian_state_update(snapshot);
    ESP_LOGI(TAG, "USB detectado %04x:%04x", descriptor->idVendor, descriptor->idProduct);

    if (descriptor->idVendor != SUPPORTED_VID || descriptor->idProduct != SUPPORTED_PID) {
        snapshot.condition = PowerCondition::Fault;
        guardian_state_update(snapshot);
        set_status("dispositivo USB sin controlador compatible");
        return;
    }
    if (usb_host_interface_claim(context.client, context.device, QX_INTERFACE, 0) != ESP_OK) {
        snapshot.condition = PowerCondition::Fault;
        guardian_state_update(snapshot);
        set_status("no se pudo reclamar la interfaz HID/Qx");
        return;
    }
    context.interface_claimed = true;
    if (!allocate_transfers(context)) {
        snapshot.condition = PowerCondition::Fault;
        guardian_state_update(snapshot);
        return;
    }
    context.next_query_ms = now_ms + 250;
    context.overflow_streak = 0;
    set_status("interfaz Cypress preparada; iniciando Q1");
}

static void client_task(void *) {
    UsbClient context = {};
    usb_host_client_config_t config = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async = {
            .client_event_callback = client_event,
            .callback_arg = &context,
        },
    };
    esp_err_t client_result = usb_host_client_register(&config, &context.client);
    if (client_result != ESP_OK) {
        set_status("USB Host activo; cliente no disponible: %s", esp_err_to_name(client_result));
        ESP_LOGE(TAG, "No se pudo registrar el cliente USB: %s", esp_err_to_name(client_result));
        vTaskDelete(nullptr);
        return;
    }
    GuardianSnapshot startup = guardian_state_get();
    startup.automatic_restarts = recovery_rtc.restart_total;
    guardian_state_update(startup);
    publish_disconnected(false);
    update_client_heartbeat();

    while (true) {
        usb_host_client_handle_events(context.client, pdMS_TO_TICKS(50));
        const int64_t now_ms = monotonic_ms();
        update_client_heartbeat();

        if (context.open_requested && context.device == nullptr) open_device(context, now_ms);

        if (context.control_event) {
            context.control_event = false;
            if (!context.recovery_pending && context.control_status != USB_TRANSFER_STATUS_COMPLETED) {
                handle_failure(context, context.control_status, now_ms, "fallo SET_REPORT Q1");
            } else if (!context.recovery_pending && context.query_active &&
                       !context.input_in_flight && !submit_input(context)) {
                handle_failure(context, USB_TRANSFER_STATUS_ERROR, now_ms,
                               "no se pudo iniciar la lectura Q1");
            }
        }

        if (context.input_event) {
            context.input_event = false;
            if (context.recovery_pending && context.input_status == USB_TRANSFER_STATUS_CANCELED) {
                // Cancelación solicitada por la propia recuperación.
            } else if (context.input_status == USB_TRANSFER_STATUS_COMPLETED &&
                       append_input(context)) {
                const ReplyResult reply_result = process_reply(context);
                if (reply_result == ReplyResult::Valid) {
                    context.query_active = false;
                    context.overflow_streak = 0;
                    context.probe_index = 0;
                    context.next_query_ms = now_ms + POLL_INTERVAL_MS;
                } else if (reply_result == ReplyResult::CypressEcho) {
                    context.query_active = false;
                    context.probe_index = (context.probe_index + 1) %
                        (sizeof(QX_PROBES) / sizeof(QX_PROBES[0]));
                    context.next_query_ms = now_ms + 200;
                    set_status("eco Cypress recibido; probando consulta %s",
                               QX_PROBES[context.probe_index]);
                } else if (context.query_active && now_ms < context.query_deadline_ms) {
                    if (!submit_input(context)) {
                        handle_failure(context, USB_TRANSFER_STATUS_ERROR, now_ms,
                                       "no se pudo continuar la lectura Q1");
                    }
                } else {
                    handle_failure(context, USB_TRANSFER_STATUS_TIMED_OUT, now_ms,
                                   "respuesta Q1 incompleta");
                }
            } else if (!context.recovery_pending) {
                handle_failure(context, context.input_status, now_ms, "fallo lectura EP81");
            }
        }

        mark_stale_if_needed(now_ms);

        if (context.recovery_pending) {
            if (!context.control_in_flight && !context.input_in_flight) {
                context.recovery_pending = false;
                context.next_query_ms = now_ms + 500;
            } else if (now_ms >= context.recovery_deadline_ms) {
                if (!automatic_usb_restart(USB_TRANSFER_STATUS_TIMED_OUT)) {
                    context.recovery_deadline_ms = now_ms + 30000;
                }
            }
        } else if (context.query_active && now_ms >= context.query_deadline_ms) {
            handle_failure(context, USB_TRANSFER_STATUS_TIMED_OUT, now_ms,
                           "tiempo de respuesta Q1 agotado");
        } else if (context.interface_claimed && !context.query_active &&
                   !context.control_in_flight && !context.input_in_flight &&
                   context.next_query_ms > 0 && now_ms >= context.next_query_ms) {
            if (!begin_query(context, now_ms)) {
                handle_failure(context, USB_TRANSFER_STATUS_ERROR, now_ms,
                               "no se pudo iniciar la consulta Q1");
            }
        }

        if (context.close_requested) {
            if (!context.control_in_flight && !context.input_in_flight) {
                release_device(context);
                ESP_LOGW(TAG, "Dispositivo USB desconectado");
            } else if (now_ms - context.close_requested_ms >= RECOVERY_TIMEOUT_MS) {
                if (!automatic_usb_restart(USB_TRANSFER_STATUS_NO_DEVICE)) {
                    context.close_requested_ms = now_ms + 30000;
                }
            }
        }
    }
}

static void usb_start_task(void *) {
    usb_host_config_t config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb = nullptr,
    };
    while (true) {
        esp_err_t result = usb_host_install(&config);
        if (result == ESP_OK) {
            set_status("USB Host iniciado; esperando dispositivo");
            ESP_LOGI(TAG, "USB Host iniciado");
            if (xTaskCreate(daemon_task, "usb_daemon", 4096, nullptr, 5, nullptr) != pdPASS ||
                xTaskCreate(client_task, "usb_qx", 8192, nullptr, 6, nullptr) != pdPASS) {
                set_status("USB Host iniciado; no se pudieron crear las tareas");
                ESP_LOGE(TAG, "No se pudieron crear las tareas USB");
            }
            vTaskDelete(nullptr);
            return;
        }
        set_status("USB ocupado; reintentando: %s", esp_err_to_name(result));
        ESP_LOGW(TAG, "USB ocupado; reintentando: %s", esp_err_to_name(result));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void usb_monitor_start() {
    if (recovery_rtc.magic != RECOVERY_RTC_MAGIC) {
        recovery_rtc = {
            .magic = RECOVERY_RTC_MAGIC,
            .restart_streak = 0,
            .restart_total = 0,
        };
    }
    if (xTaskCreate(usb_start_task, "usb_start", 3072, nullptr, 3, nullptr) != pdPASS) {
        set_status("no se pudo iniciar la tarea USB Host");
        ESP_LOGE(TAG, "No se pudo iniciar la tarea USB Host");
    }
    if (xTaskCreate(client_watchdog_task, "usb_watchdog", 3072, nullptr, 5, nullptr) != pdPASS) {
        set_status("no se pudo iniciar el supervisor USB");
        ESP_LOGE(TAG, "No se pudo iniciar el supervisor USB");
    }
}
