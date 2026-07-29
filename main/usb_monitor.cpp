#include "usb_monitor.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#include "guardian_state.h"

static const char *TAG = "usb_qx";
static char qx_status[96] = "esperando dispositivo USB";
static char last_qx_reply[128] = "";
static constexpr uint16_t SUPPORTED_VID = 0x0665;
static constexpr uint16_t SUPPORTED_PID = 0x5161;
static constexpr uint8_t QX_INTERFACE = 0;
static constexpr uint8_t QX_INPUT_ENDPOINT = 0x81;
static constexpr const char *QX_PROBES[] = {"Q1\r", "QGS\r", "QS\r", "F\r", "I\r"};

struct UsbClient {
    usb_host_client_handle_t client;
    usb_device_handle_t device;
    usb_transfer_t *control;
    usb_transfer_t *input;
    uint8_t address;
    bool open_requested;
    bool close_requested;
    bool control_finished;
    bool input_finished;
    bool interface_claimed;
    usb_transfer_status_t control_status;
    usb_transfer_status_t input_status;
    int input_size;
    char reply[128];
    size_t reply_size;
    int64_t next_query_ms;
    size_t probe_index;
};

static void client_event(const usb_host_client_event_msg_t *message, void *argument) {
    auto *context = static_cast<UsbClient *>(argument);
    if (message->event == USB_HOST_CLIENT_EVENT_NEW_DEV && context->device == nullptr) {
        context->address = message->new_dev.address;
        context->open_requested = true;
    } else if (message->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        context->close_requested = true;
    }
}

static void control_finished(usb_transfer_t *transfer) {
    auto *context = static_cast<UsbClient *>(transfer->context);
    context->control_status = transfer->status;
    context->control_finished = true;
}

static void input_finished(usb_transfer_t *transfer) {
    auto *context = static_cast<UsbClient *>(transfer->context);
    context->input_status = transfer->status;
    context->input_size = transfer->actual_num_bytes;
    context->input_finished = true;
}

static void daemon_task(void *) {
    while (true) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
    }
}

static void update_disconnected() {
    GuardianSnapshot snapshot = guardian_state_get();
    snapshot.condition = PowerCondition::Disconnected;
    snapshot.data_valid = false;
    snapshot.usb_vid = 0;
    snapshot.usb_pid = 0;
    guardian_state_update(snapshot);
    strlcpy(qx_status, "esperando dispositivo USB", sizeof(qx_status));
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

static void parse_qx_reply(const char *reply) {
    strlcpy(last_qx_reply, reply, sizeof(last_qx_reply));
    float input = 0, input_fault = 0, output = 0, frequency = 0, battery = 0;
    int load = 0;
    char temperature[8] = {};
    char flags[9] = {};
    int fields = sscanf(reply, "(%f %f %f %d %f %f %7s %8s",
                        &input, &input_fault, &output, &load, &frequency,
                        &battery, temperature, flags);
    if (fields < 8) {
        strlcpy(qx_status, "respuesta Qx no reconocida", sizeof(qx_status));
        ESP_LOGW(TAG, "Respuesta Qx no reconocida: %s", reply);
        return;
    }

    GuardianSnapshot snapshot = guardian_state_get();
    snapshot.data_valid = true;
    snapshot.input_voltage = input;
    snapshot.output_voltage = output;
    snapshot.load_percent = load;
    snapshot.frequency = frequency;
    snapshot.battery_voltage = battery;
    snapshot.battery_percent = estimate_battery(battery);
    snapshot.runtime_seconds = estimate_runtime(snapshot.battery_percent, load);
    snapshot.last_update_ms = esp_timer_get_time() / 1000;
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
    strlcpy(qx_status, "comunicación Qx activa", sizeof(qx_status));
}

static bool allocate_transfers(UsbClient &context) {
    if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + 8, 0, &context.control) != ESP_OK) {
        strlcpy(qx_status, "no se pudo reservar transferencia de control", sizeof(qx_status));
        return false;
    }
    if (usb_host_transfer_alloc(8, 0, &context.input) != ESP_OK) {
        usb_host_transfer_free(context.control);
        context.control = nullptr;
        strlcpy(qx_status, "no se pudo reservar transferencia de entrada", sizeof(qx_status));
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

static bool send_query(UsbClient &context) {
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
    context.control_finished = false;
    context.reply_size = 0;
    memset(context.reply, 0, sizeof(context.reply));
    return usb_host_transfer_submit_control(context.client, context.control) == ESP_OK;
}

static bool submit_input(UsbClient &context) {
    context.input_finished = false;
    context.input->num_bytes = 8;
    return usb_host_transfer_submit(context.input) == ESP_OK;
}

static bool begin_query(UsbClient &context) {
    context.reply_size = 0;
    memset(context.reply, 0, sizeof(context.reply));
    if (!submit_input(context)) return false;
    if (!send_query(context)) return false;
    return true;
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
    update_disconnected();
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
    ESP_ERROR_CHECK(usb_host_client_register(&config, &context.client));
    update_disconnected();

    while (true) {
        usb_host_client_handle_events(context.client, pdMS_TO_TICKS(50));
        int64_t now_ms = esp_timer_get_time() / 1000;

        if (context.open_requested) {
            context.open_requested = false;
            if (usb_host_device_open(context.client, context.address, &context.device) == ESP_OK) {
                const usb_device_desc_t *descriptor = nullptr;
                if (usb_host_get_device_descriptor(context.device, &descriptor) == ESP_OK) {
                    GuardianSnapshot snapshot = guardian_state_get();
                    snapshot.usb_vid = descriptor->idVendor;
                    snapshot.usb_pid = descriptor->idProduct;
                    snapshot.condition = PowerCondition::Starting;
                    snapshot.data_valid = false;
                    guardian_state_update(snapshot);
                    ESP_LOGI(TAG, "USB detectado %04x:%04x", descriptor->idVendor, descriptor->idProduct);
                    if (descriptor->idVendor == SUPPORTED_VID && descriptor->idProduct == SUPPORTED_PID &&
                        usb_host_interface_claim(context.client, context.device, QX_INTERFACE, 0) == ESP_OK) {
                        context.interface_claimed = true;
                        strlcpy(qx_status, "interfaz HID reclamada; iniciando Qx", sizeof(qx_status));
                        if (allocate_transfers(context)) {
                            context.next_query_ms = now_ms + 250;
                        }
                    } else {
                        strlcpy(qx_status, "dispositivo no compatible o interfaz HID ocupada", sizeof(qx_status));
                    }
                }
            }
        }

        if (context.control_finished) {
            context.control_finished = false;
            if (context.control_status != USB_TRANSFER_STATUS_COMPLETED) {
                snprintf(qx_status, sizeof(qx_status), "fallo SET_REPORT: %d", context.control_status);
                context.next_query_ms = now_ms + 1000;
            }
        }

        if (context.input_finished) {
            context.input_finished = false;
            if (context.input_status == USB_TRANSFER_STATUS_COMPLETED && context.input_size > 0) {
                char packet_debug[96] = {};
                size_t debug_offset = snprintf(packet_debug, sizeof(packet_debug), "len=%d hex=", context.input_size);
                for (int i = 0; i < context.input_size && debug_offset + 4 < sizeof(packet_debug); ++i) {
                    debug_offset += snprintf(packet_debug + debug_offset, sizeof(packet_debug) - debug_offset,
                                             "%02X", context.input->data_buffer[i]);
                }
                strlcpy(last_qx_reply, packet_debug, sizeof(last_qx_reply));
                size_t available = sizeof(context.reply) - context.reply_size - 1;
                size_t copy = static_cast<size_t>(context.input_size) < available ?
                              static_cast<size_t>(context.input_size) : available;
                memcpy(context.reply + context.reply_size, context.input->data_buffer, copy);
                context.reply_size += copy;
                context.reply[context.reply_size] = '\0';
                if (strchr(context.reply, '\r')) {
                    if (context.reply[0] == '(') {
                        parse_qx_reply(context.reply);
                        context.probe_index = 0;
                        context.next_query_ms = now_ms + 1000;
                    } else {
                        // Un eco indica que este dialecto no está implementado.
                        context.reply_size = 0;
                        memset(context.reply, 0, sizeof(context.reply));
                        context.probe_index =
                            (context.probe_index + 1) % (sizeof(QX_PROBES) / sizeof(QX_PROBES[0]));
                        context.next_query_ms = now_ms + 200;
                    }
                } else if (!submit_input(context)) {
                    context.next_query_ms = now_ms + 1000;
                }
            } else {
                snprintf(qx_status, sizeof(qx_status), "fallo lectura 0x81: %d", context.input_status);
                context.next_query_ms = now_ms + 1000;
            }
        }

        if (context.interface_claimed && !context.control_finished && !context.input_finished &&
            context.next_query_ms > 0 && now_ms >= context.next_query_ms) {
            context.next_query_ms = 0;
            if (!begin_query(context)) context.next_query_ms = now_ms + 1000;
        }

        if (context.close_requested) {
            context.close_requested = false;
            release_device(context);
            ESP_LOGW(TAG, "Dispositivo USB desconectado");
        }
    }
}

const char *usb_monitor_status() {
    return qx_status;
}

const char *usb_monitor_reply() {
    return last_qx_reply;
}

void usb_monitor_start() {
    usb_host_config_t config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb = nullptr,
    };
    ESP_ERROR_CHECK(usb_host_install(&config));
    xTaskCreate(daemon_task, "usb_daemon", 4096, nullptr, 5, nullptr);
    xTaskCreate(client_task, "usb_qx", 6144, nullptr, 4, nullptr);
}
