#include "adc_driver.hpp"

#include <algorithm>
#include "esp_log.h"

namespace core::peripherals {

namespace {
constexpr const char* TAG = "ADC_DRV";
} // namespace

bool AdcDriver::init(const config::AdcConfig& cfg) {
    cfg_ = cfg;
    if (!cfg_.enabled) return false;

    adc_oneshot_unit_init_cfg_t init_cfg{};
    init_cfg.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&init_cfg, &handle_) != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando ADC_UNIT_1");
        return false;
    }

    adc_oneshot_chan_cfg_t chan_cfg{};
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT; // 12 bits (0..4095)
    chan_cfg.atten    = ADC_ATTEN_DB_12;      // Rango ~0V a 3.1V

    for (const auto& ch : kAdc1Channels) {
        ESP_ERROR_CHECK(adc_oneshot_config_channel(handle_, ch.channel, &chan_cfg));
        if (ch.gpio == cfg_.gpio) {
            selected_idx_ = ch.index;
        }
    }

    ESP_LOGI(TAG, "6 canales ADC1 inicializados: GPIO 36, 39, 32, 33, 34, 35 (12-bit, 12dB)");
    return true;
}

void AdcDriver::selectChannelIndex(int idx_0_to_5) {
    selected_idx_ = std::clamp(idx_0_to_5, 0, static_cast<int>(kAdc1Channels.size()) - 1);
}

int AdcDriver::readRawByIndex(int idx_0_to_5) const {
    if (!handle_) return 0;
    int idx = std::clamp(idx_0_to_5, 0, static_cast<int>(kAdc1Channels.size()) - 1);
    int raw = 0;
    adc_oneshot_read(handle_, kAdc1Channels[idx].channel, &raw);
    return raw;
}

float AdcDriver::readVoltsByIndex(int idx_0_to_5) const {
    int raw = readRawByIndex(idx_0_to_5);
    float v_pin = (static_cast<float>(raw) / 4095.0f) * 3.10f;
    return v_pin * cfg_.divider_ratio;
}

} // namespace core::peripherals
