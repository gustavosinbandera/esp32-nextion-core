#include "pwm_driver.hpp"

#include <algorithm>
#include "esp_log.h"
#include "driver/ledc.h"

namespace core::peripherals {

namespace {
constexpr const char* TAG = "PWM_DRV";
}

bool PwmDriver::init(const config::PwmConfig& cfg) {
    cfg_ = cfg;
    if (!cfg_.enabled) return false;

    ledc_timer_config_t timer_cfg{};
    timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_cfg.duty_resolution = LEDC_TIMER_10_BIT;
    timer_cfg.timer_num = LEDC_TIMER_0;
    timer_cfg.freq_hz = cfg_.freq_hz;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg{};
    ch_cfg.gpio_num = cfg_.gpio;
    ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_cfg.channel = LEDC_CHANNEL_0;
    ch_cfg.intr_type = LEDC_INTR_DISABLE;
    ch_cfg.timer_sel = LEDC_TIMER_0;
    ch_cfg.duty = 0;
    ch_cfg.hpoint = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    ESP_LOGI(TAG, "PWM LEDC inicializado en GPIO %d (%lu Hz)",
             static_cast<int>(cfg_.gpio), static_cast<unsigned long>(cfg_.freq_hz));
    return true;
}

void PwmDriver::setDutyPercent(uint8_t duty_0_to_100) {
    duty_percent_ = std::min<uint8_t>(duty_0_to_100, 100);
    if (!cfg_.enabled) return;
    uint32_t raw_duty = (static_cast<uint32_t>(duty_percent_) * 1023U) / 100U;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, raw_duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void PwmDriver::setFrequency(uint32_t freq_hz) {
    if (freq_hz == 0 || !cfg_.enabled) return;
    cfg_.freq_hz = freq_hz;
    ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq_hz);
}

} // namespace core::peripherals
