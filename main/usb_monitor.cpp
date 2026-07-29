#include "usb_monitor.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#include "guardian_state.h"

static const char *TAG = "usb_monitor";

struct UsbClient {
    usb_host_client_handle_t client;
    usb_device_handle_t device;
    uint8_t address;
    bool open_requested;
    bool close_requested;
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
}

static void client_task(void *) {
    UsbClient context = {};
    usb_host_client_config_t config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event,
            .callback_arg = &context,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&config, &context.client));
    update_disconnected();

    while (true) {
        usb_host_client_handle_events(context.client, pdMS_TO_TICKS(100));
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
                }
            }
        }
        if (context.close_requested) {
            context.close_requested = false;
            if (context.device) {
                usb_host_device_close(context.client, context.device);
                context.device = nullptr;
            }
            context.address = 0;
            update_disconnected();
            ESP_LOGW(TAG, "Dispositivo USB desconectado");
        }
    }
}

void usb_monitor_start() {
    usb_host_config_t config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb = nullptr,
    };
    ESP_ERROR_CHECK(usb_host_install(&config));
    xTaskCreate(daemon_task, "usb_daemon", 4096, nullptr, 5, nullptr);
    xTaskCreate(client_task, "usb_monitor", 4096, nullptr, 4, nullptr);
}

