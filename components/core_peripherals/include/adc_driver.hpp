#pragma once

#include <array>
#include <cstdint>
#include "core_config.hpp"
#include "esp_adc/adc_oneshot.h"

namespace core::peripherals {

struct AdcPinDesc {
    uint8_t index;          // 0..5
    gpio_num_t gpio;        // 36, 39, 32, 33, 34, 35
    adc_channel_t channel;  // ADC1_CH0, CH3, CH4, CH5, CH6, CH7
    const char* label;      // Nombre descriptivo
};

// Los 6 canales de ADC1 disponibles en ESP32 (libres y compatibles con Wi-Fi activo)
inline constexpr std::array<AdcPinDesc, 6> kAdc1Channels = {{
    {0, GPIO_NUM_36, ADC_CHANNEL_0, "GPIO36 (VP/CH0)"},
    {1, GPIO_NUM_39, ADC_CHANNEL_3, "GPIO39 (VN/CH3)"},
    {2, GPIO_NUM_32, ADC_CHANNEL_4, "GPIO32 (ADC1_CH4)"},
    {3, GPIO_NUM_33, ADC_CHANNEL_5, "GPIO33 (ADC1_CH5)"},
    {4, GPIO_NUM_34, ADC_CHANNEL_6, "GPIO34 (ADC1_CH6)"},
    {5, GPIO_NUM_35, ADC_CHANNEL_7, "GPIO35 (ADC1_CH7)"},
}};

class AdcDriver {
public:
    AdcDriver() = default;

    // Inicializa los 6 canales de ADC1 (GPIO 36, 39, 32, 33, 34, 35)
    bool init(const config::AdcConfig& cfg);

    // Canal activo por índice (0..5)
    void selectChannelIndex(int idx_0_to_5);
    int selectedChannelIndex() const { return selected_idx_; }
    const AdcPinDesc& currentChannelDesc() const { return kAdc1Channels[selected_idx_]; }

    // Lectura del canal actualmente seleccionado
    int readRaw() const { return readRawByIndex(selected_idx_); }
    float readVolts() const { return readVoltsByIndex(selected_idx_); }

    // Lectura de cualquiera de los 6 canales de ADC1 por índice (0..5)
    int readRawByIndex(int idx_0_to_5) const;
    float readVoltsByIndex(int idx_0_to_5) const;

private:
    config::AdcConfig cfg_{};
    adc_oneshot_unit_handle_t handle_{nullptr};
    int selected_idx_{4}; // Por defecto índice 4 = GPIO 34 (ADC1_CH6)
};

} // namespace core::peripherals
