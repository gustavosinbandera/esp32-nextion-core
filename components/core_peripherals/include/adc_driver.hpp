#pragma once

#include <cstdint>
#include "core_config.hpp"
#include "esp_adc/adc_oneshot.h"

namespace core::peripherals {

class AdcDriver {
public:
    AdcDriver() = default;

    bool init(const config::AdcConfig& cfg);

    // Devuelve lectura cruda (0..4095)
    int readRaw() const;

    // Devuelve voltaje en el pin (0.0V .. 3.1V) multiplicado por divider_ratio
    float readVolts() const;

    // Devuelve muestra normalizada (-1.0f a +1.0f) centrada en 1.55V para graficar ondas AC/DC en el osciloscopio
    float readNormalizedWave() const;

private:
    config::AdcConfig cfg_{};
    adc_oneshot_unit_handle_t handle_{nullptr};
    adc_channel_t channel_{ADC_CHANNEL_6}; // GPIO 34 por defecto
};

} // namespace core::peripherals
