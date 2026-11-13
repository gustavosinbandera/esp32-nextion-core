#include "adc_driver.hpp"

#include <algorithm>
#include "esp_log.h"

namespace core::peripherals {

namespace {
constexpr const char* TAG = "ADC_DRV";

adc_channel_t gpioToAdc1Channel(gpio_num_t gpio) {
    switch (gpio) {
        case GPIO_NUM_36: return ADC_CHANNEL_0;
        case GPIO_NUM_39: return ADC_CHANNEL_3;
        case GPIO_NUM_32: return ADC_CHANNEL_4;
        case GPIO_NUM_33: return ADC_CHANNEL_5;
        case GPIO_NUM_34: return ADC_CHANNEL_6;
        case GPIO_NUM_35: return ADC_CHANNEL_7;
        default:          return ADC_CHANNEL_6;
    }
}
} // namespace

bool AdcDriver::init(const config::AdcConfig& cfg) {
    cfg_ = cfg;
    if (!cfg_.enabled) return false;

    channel_ = gpioToAdc1Channel(cfg_.gpio);

    adc_oneshot_unit_init_cfg_t init_cfg{};
    init_cfg.unit_id = ADC_UNIT_1; // ADC1 opera sin conflictos con Wi-Fi
    if (adc_oneshot_new_unit(&init_cfg, &handle_) != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando ADC_UNIT_1");
        return false;
    }

    adc_oneshot_chan_cfg_t chan_cfg{};
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT; // 12 bits (0..4095)
    chan_cfg.atten    = ADC_ATTEN_DB_12;      // Rango completo ~0V a 3.1V
    ESP_ERROR_CHECK(adc_oneshot_config_channel(handle_, channel_, &chan_cfg));

    ESP_LOGI(TAG, "ADC1 inicializado en GPIO %d (Canal %d, 12-bit, 12dB)",
             static_cast<int>(cfg_.gpio), static_cast<int>(channel_));
    return true;
}

int AdcDriver::readRaw() const {
    if (!handle_) return 2048;
    int raw = 2048;
    adc_oneshot_read(handle_, channel_, &raw);
    return raw;
}

float AdcDriver::readVolts() const {
    int raw = readRaw();
    float v_pin = (static_cast<float>(raw) / 4095.0f) * 3.10f;
    return v_pin * cfg_.divider_ratio;
}

float AdcDriver::readNormalizedWave() const {
    int raw = readRaw();
    float norm = (static_cast<float>(raw) - 2048.0f) / 2048.0f;
    return std::clamp(norm, -1.0f, 1.0f);
}

} // namespace core::peripherals
