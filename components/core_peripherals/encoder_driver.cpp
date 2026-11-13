#include "encoder_driver.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

namespace core::peripherals {

namespace {
constexpr const char* TAG = "ENCODER_PCNT";
constexpr int kHighLimit = 2;
constexpr int kLowLimit  = -2;
} // namespace

bool IRAM_ATTR EncoderDriver::onPcntWatch(pcnt_unit_handle_t unit,
                                          const pcnt_watch_event_data_t* edata,
                                          void* user_ctx) {
    auto* self = static_cast<EncoderDriver*>(user_ctx);
    BaseType_t hp_woken = pdFALSE;
    int watch_val = edata->watch_point_value;
    // Reiniciar contador inmediatamente en hardware para el siguiente paso
    pcnt_unit_clear_count(unit);
    self->evt_queue_.sendFromIsr(watch_val, &hp_woken);
    return (hp_woken == pdTRUE);
}

bool EncoderDriver::init(const config::EncoderConfig& cfg, EventCallback cb) {
    cfg_ = cfg;
    cb_  = std::move(cb);
    if (!cfg_.enabled) return false;

    // 1. Configurar GPIOs con Pull-Up activo para las fases A/B (19, 23) y botón SW (18)
    gpio_config_t io_cfg{};
    io_cfg.intr_type = GPIO_INTR_DISABLE;
    io_cfg.mode = GPIO_MODE_INPUT;
    io_cfg.pin_bit_mask = (1ULL << cfg_.cw_gpio) |
                          (1ULL << cfg_.ccw_gpio) |
                          (1ULL << cfg_.sw_gpio);
    io_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io_cfg));

    // 2. Unidad PCNT por hardware (igual que rotary_encoder_pcnt_ec11.c pero con la API de ESP-IDF v5.5)
    pcnt_unit_config_t unit_cfg{};
    unit_cfg.high_limit = kHighLimit;
    unit_cfg.low_limit  = kLowLimit;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &pcnt_unit_));

    // Filtro de glitches en silicio (10 us = 10000 ns)
    pcnt_glitch_filter_config_t filter_cfg{};
    filter_cfg.max_glitch_ns = 10000;
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit_, &filter_cfg));

    // Canal 0: Fase A = ccw_gpio (19), Control B = cw_gpio (23)
    pcnt_chan_config_t chan_cfg{};
    chan_cfg.edge_gpio_num  = cfg_.ccw_gpio;
    chan_cfg.level_gpio_num = cfg_.cw_gpio;
    pcnt_channel_handle_t pcnt_chan = nullptr;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit_, &chan_cfg, &pcnt_chan));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(
        pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(
        pcnt_chan, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // Watchpoints en +2 y -2 igual que en tu implementación original
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit_, kHighLimit));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit_, kLowLimit));

    pcnt_event_callbacks_t cbs{};
    cbs.on_reach = &EncoderDriver::onPcntWatch;
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit_, &cbs, this));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit_));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit_));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit_));

    worker_task_.start("enc_pcnt_task", 4096, 6, 1, [this] { workerLoop(); });
    ESP_LOGI(TAG, "Encoder PCNT hardware iniciado (CW=%d, CCW=%d, SW=%d)",
             static_cast<int>(cfg_.cw_gpio), static_cast<int>(cfg_.ccw_gpio), static_cast<int>(cfg_.sw_gpio));
    return true;
}

void EncoderDriver::injectEvent(EncoderEvent ev) {
    if (cb_) cb_(ev);
}

void EncoderDriver::workerLoop() {
    int watch_val = 0;
    bool sw_pressed = false;
    bool long_fired = false;
    int64_t sw_press_ms = 0;
    int64_t last_rot_ms = 0;

    while (true) {
        bool got_rot = evt_queue_.receive(watch_val, pdMS_TO_TICKS(20));
        const int64_t now_ms = esp_timer_get_time() / 1000LL;

        if (got_rot) {
            // Antirrebote de 60 ms y vaciado de rebotes residuales en la cola
            if (now_ms - last_rot_ms >= 60) {
                last_rot_ms = now_ms;
                evt_queue_.clear();
                pcnt_unit_clear_count(pcnt_unit_);

                if (watch_val > 0) {
                    ESP_LOGI(TAG, "Giro -> RIGHT");
                    if (cb_) cb_(EncoderEvent::RotateRight);
                } else if (watch_val < 0) {
                    ESP_LOGI(TAG, "Giro -> LEFT");
                    if (cb_) cb_(EncoderEvent::RotateLeft);
                }
            }
        }

        // Lectura del botón SW (GPIO 18, Active-Low)
        bool is_low = (gpio_get_level(cfg_.sw_gpio) == 0);
        if (is_low && !sw_pressed) {
            sw_pressed = true;
            long_fired = false;
            sw_press_ms = now_ms;
        } else if (is_low && sw_pressed && !long_fired) {
            if (now_ms - sw_press_ms >= cfg_.long_press_ms) {
                long_fired = true;
                ESP_LOGI(TAG, "Botón -> LONG PRESS");
                if (cb_) cb_(EncoderEvent::LongPress);
            }
        } else if (!is_low && sw_pressed) {
            sw_pressed = false;
            if (!long_fired && (now_ms - sw_press_ms >= 30)) {
                ESP_LOGI(TAG, "Botón -> CLICK");
                if (cb_) cb_(EncoderEvent::Click);
            }
        }
    }
}

} // namespace core::peripherals
