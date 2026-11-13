#pragma once

#include <cstdint>
#include <string>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "core_rtos.hpp"

namespace core::config {

// =============================================================================
// Configuración de Periféricos de la Placa (Equivalente en C++ al mos.yml)
// Respeta exactamente los pines de tu placa conectada:
//   - Nextion Display: UART1, RX = GPIO 16, TX = GPIO 17, 9600 -> 38400 bps
//   - Rotary Encoder:  CW = GPIO 23, CCW = GPIO 19, SW = GPIO 18
//   - PWM Output:      GPIO 5, 300 Hz
// =============================================================================

struct NextionPortConfig {
    bool enabled{true};
    uart_port_t uart_num{UART_NUM_1};
    gpio_num_t rx_gpio{GPIO_NUM_16};
    gpio_num_t tx_gpio{GPIO_NUM_17};
    uint32_t init_baud{9600};
    uint32_t work_baud{38400};
    uint16_t width{320};
    uint16_t height{471};
};

struct EncoderConfig {
    bool enabled{true};
    gpio_num_t cw_gpio{GPIO_NUM_23};   // encoder.cw = 23
    gpio_num_t ccw_gpio{GPIO_NUM_19};  // encoder.ccw = 19
    gpio_num_t sw_gpio{GPIO_NUM_18};   // encoder.sw = 18
    uint32_t debounce_ms{50};
    uint32_t long_press_ms{800};
};

struct PwmConfig {
    bool enabled{true};
    gpio_num_t gpio{GPIO_NUM_5};       // pwm.gpio = 5
    uint32_t freq_hz{300};             // pwm.freq = 300
};

struct AdcConfig {
    bool enabled{true};
    gpio_num_t gpio{GPIO_NUM_34};      // ADC1_CH6 (compatible con Wi-Fi activo)
    float divider_ratio{1.0f};         // Factor divisor resistivo opcional
};

struct WifiConfig {
    std::string ap_ssid{"CoreFW_Nextion"};
    std::string ap_pass{""};
    uint8_t ap_channel{6};
};

struct BoardPeripheralsConfig {
    NextionPortConfig nextion{};
    EncoderConfig encoder{};
    PwmConfig pwm{};
    AdcConfig adc{};
    WifiConfig wifi{};
};

// Almacén NVS persistente para guardar y cargar parámetros en Flash
class ConfigStore {
public:
    explicit ConfigStore(const char* ns = "core_cfg") : ns_(ns) {}

    static esp_err_t initNvs();

    std::string getString(const char* key, const char* def = "") const;
    int32_t getInt(const char* key, int32_t def = 0) const;
    bool getBool(const char* key, bool def = false) const;

    esp_err_t setString(const char* key, const char* val);
    esp_err_t setInt(const char* key, int32_t val);
    esp_err_t setBool(const char* key, bool val);

    // Carga overrides desde NVS sobre la configuración por defecto de periféricos
    void loadPeripheralsConfig(BoardPeripheralsConfig& cfg) const;

private:
    const char* ns_;
    mutable rtos::Mutex mutex_{};
};

} // namespace core::config
