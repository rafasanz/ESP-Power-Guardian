#include "status_led.h"

#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "guardian_state.h"
#include "settings.h"

static const char *TAG = "status_led";
static rmt_channel_handle_t channel;
static rmt_encoder_handle_t encoder;

struct LedEncoder {
    rmt_encoder_t base;
    rmt_encoder_handle_t bytes;
    rmt_encoder_handle_t copy;
    int phase;
    rmt_symbol_word_t reset;
};

static size_t encode_led(rmt_encoder_t *base, rmt_channel_handle_t ch,
                         const void *data, size_t size,
                         rmt_encode_state_t *ret_state) {
    auto *led = reinterpret_cast<LedEncoder *>(base);
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t symbols = 0;
    if (led->phase == 0) {
        symbols += led->bytes->encode(led->bytes, ch, data, size, &state);
        if (state & RMT_ENCODING_COMPLETE) led->phase = 1;
        if (state & RMT_ENCODING_MEM_FULL) {
            *ret_state = RMT_ENCODING_MEM_FULL;
            return symbols;
        }
    }
    symbols += led->copy->encode(led->copy, ch, &led->reset, sizeof(led->reset), &state);
    if (state & RMT_ENCODING_COMPLETE) {
        led->phase = 0;
        *ret_state = RMT_ENCODING_COMPLETE;
    } else {
        *ret_state = RMT_ENCODING_MEM_FULL;
    }
    return symbols;
}

static esp_err_t reset_led_encoder(rmt_encoder_t *base) {
    auto *led = reinterpret_cast<LedEncoder *>(base);
    rmt_encoder_reset(led->bytes);
    rmt_encoder_reset(led->copy);
    led->phase = 0;
    return ESP_OK;
}

static esp_err_t delete_led_encoder(rmt_encoder_t *base) {
    auto *led = reinterpret_cast<LedEncoder *>(base);
    rmt_del_encoder(led->bytes);
    rmt_del_encoder(led->copy);
    delete led;
    return ESP_OK;
}

static esp_err_t make_encoder(rmt_encoder_handle_t *out) {
    auto *led = new LedEncoder{};
    led->base.encode = encode_led;
    led->base.reset = reset_led_encoder;
    led->base.del = delete_led_encoder;
    led->reset = {.duration0 = 1000, .level0 = 0, .duration1 = 1000, .level1 = 0};
    rmt_bytes_encoder_config_t bytes_cfg = {
        .bit0 = {.duration0 = 4, .level0 = 1, .duration1 = 9, .level1 = 0},
        .bit1 = {.duration0 = 8, .level0 = 1, .duration1 = 5, .level1 = 0},
        .flags = {.msb_first = 1},
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&bytes_cfg, &led->bytes));
    rmt_copy_encoder_config_t copy_config = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_config, &led->copy));
    *out = &led->base;
    return ESP_OK;
}

static uint32_t color_for(PowerCondition condition, const LedPalette &p) {
    switch (condition) {
        case PowerCondition::Starting: return p.starting;
        case PowerCondition::Online: return p.online;
        case PowerCondition::OnBattery: return p.on_battery;
        case PowerCondition::LowBattery: return p.low_battery;
        case PowerCondition::Fault: return p.fault;
        case PowerCondition::Disconnected: return p.disconnected;
    }
    return 0;
}

static void led_task(void *) {
    uint64_t previous_signature = UINT64_MAX;
    while (true) {
        LedPalette p = settings_led_palette();
        uint32_t rgb = color_for(guardian_state_get().condition, p);
        uint64_t signature = (static_cast<uint64_t>(rgb) << 8) | p.brightness;
        if (signature != previous_signature) {
            uint8_t scale = p.brightness;
            uint8_t grb[3] = {
                static_cast<uint8_t>((((rgb >> 8) & 0xff) * scale) / 255),
                static_cast<uint8_t>((((rgb >> 16) & 0xff) * scale) / 255),
                static_cast<uint8_t>(((rgb & 0xff) * scale) / 255),
            };
            rmt_transmit_config_t cfg = {.loop_count = 0};
            rmt_transmit(channel, encoder, grb, sizeof(grb), &cfg);
            rmt_tx_wait_all_done(channel, pdMS_TO_TICKS(100));
            previous_signature = signature;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void status_led_start() {
    rmt_tx_channel_config_t cfg = {
        .gpio_num = GPIO_NUM_48,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .intr_priority = 0,
        .flags = {.invert_out = 0, .with_dma = 0, .io_loop_back = 0, .io_od_mode = 0},
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&cfg, &channel));
    ESP_ERROR_CHECK(make_encoder(&encoder));
    ESP_ERROR_CHECK(rmt_enable(channel));
    xTaskCreate(led_task, "status_led", 3072, nullptr, 4, nullptr);
    ESP_LOGI(TAG, "WS2818 iniciado en GPIO48");
}
