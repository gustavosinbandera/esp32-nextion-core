#pragma once

#include <array>
#include <cstdint>
#include "core_config.hpp"
#include "esp_adc/adc_oneshot.h"

namespace core::peripherals {

struct AdcPinDesc {
    uint8_t index;          // 0..5
    gpio_num_t gpio;        // 32, 33, 34, 35, 36, 39
    adc_channel_t channel;  // ADC1_CH4, CH5, CH6, CH7, CH0, CH3
    const char* label;      // Nombre descriptivo
};

// Ordenados según comportamiento real en placa U020M 1.0.1:
// - GPIO 32, 33, 34: Lectura analógica activa
// - GPIO 35:         En alto directo (3.3V pull-up)
// - GPIO 36, 39:     Al final (0V / sin conexión en PCB)
inline constexpr std::array<AdcPinDesc, 6> kAdc1Channels = {{
    {0, GPIO_NUM_32, ADC_CHANNEL_4, "GPIO32 (ADC1_CH4)"},
    {1, GPIO_NUM_33, ADC_CHANNEL_5, "GPIO33 (ADC1_CH5)"},
    {2, GPIO_NUM_34, ADC_CHANNEL_6, "GPIO34 (ADC1_CH6)"},
    {3, GPIO_NUM_35, ADC_CHANNEL_7, "GPIO35 (HIGH/CH7)"},
    {4, GPIO_NUM_36, ADC_CHANNEL_0, "GPIO36 (NC/VP)"},
    {5, GPIO_NUM_39, ADC_CHANNEL_3, "GPIO39 (NC/VN)"},
}};

class AdcDriver {
public:
    AdcDriver() = default;

    bool init(const config::AdcConfig& cfg);

    void selectChannelIndex(int idx_0_to_5);
    int selectedChannelIndex() const { return selected_idx_; }
    const AdcPinDesc& currentChannelDesc() const { return kAdc1Channels[selected_idx_]; }

    int readRaw() const { return readRawByIndex(selected_idx_); }
    float readVolts() const { return readVoltsByIndex(selected_idx_); }

    int readRawByIndex(int idx_0_to_5) const;
    float readVoltsByIndex(int idx_0_to_5) const;

private:
    config::AdcConfig cfg_{};
    adc_oneshot_unit_handle_t handle_{nullptr};
    int selected_idx_{0}; // Por defecto índice 0 = GPIO 32 (ADC1_CH4)
};

} // namespace core::peripherals
