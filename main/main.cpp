#include <memory>
#include "esp_log.h"

#include "core_rtos.hpp"
#include "core_config.hpp"
#include <memory>
#include <cstdio>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

#include "core_rtos.hpp"
#include "core_config.hpp"
#include "nextion_driver.hpp"
#include "encoder_driver.hpp"
#include "pwm_driver.hpp"
#include "adc_driver.hpp"
#include "wifi_service.hpp"
#include "core_ui.hpp"
#include "app_screens.hpp"

namespace {
constexpr const char* TAG = "MAIN_CORE";

// Escáner automático no destructivo de pines conectados en la PCB U020M 1.0.1
void probeBoardHardware() {
    ESP_LOGI(TAG, "--- ESCANEO DE HARDWARE PCB U020M 1.0.1 ---");

    // 1. Probar qué pines tienen circuito externo (comparando lectura con pull-up vs pull-down)
    constexpr int kTestGpios[] = {2, 4, 5, 12, 13, 14, 15, 21, 22, 25, 26, 27, 32, 33, 34, 35, 36, 39};
    for (int pin : kTestGpios) {
        gpio_num_t g = static_cast<gpio_num_t>(pin);
        if (pin < 34) {
            gpio_set_direction(g, GPIO_MODE_INPUT);
            gpio_set_pull_mode(g, GPIO_PULLUP_ONLY);
            core::rtos::Task::delayMs(2);
            int up = gpio_get_level(g);

            gpio_set_pull_mode(g, GPIO_PULLDOWN_ONLY);
            core::rtos::Task::delayMs(2);
            int down = gpio_get_level(g);

            gpio_set_pull_mode(g, GPIO_FLOATING);
            core::rtos::Task::delayMs(2);
            int flt = gpio_get_level(g);

            if (up == down) {
                ESP_LOGI(TAG, "GPIO %2d: CONECTADO A CIRCUITO EXTERNO (fijo en %d, float=%d)", pin, up, flt);
            } else {
                ESP_LOGI(TAG, "GPIO %2d: Libre / Alta impedancia (up=%d, down=%d, float=%d)", pin, up, down, flt);
            }
        } else {
            int flt = gpio_get_level(g);
            ESP_LOGI(TAG, "GPIO %2d (Input-Only): nivel digital=%d", pin, flt);
        }
    }

    // 2. Escanear bus I2C en GPIO 21 (SDA) / GPIO 22 (SCL)
    i2c_config_t conf{};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = GPIO_NUM_21;
    conf.scl_io_num = GPIO_NUM_22;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 100000;
    if (i2c_param_config(I2C_NUM_0, &conf) == ESP_OK &&
        i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0) == ESP_OK) {
        ESP_LOGI(TAG, "Escaneando bus I2C0 (SDA=21, SCL=22)...");
        int found = 0;
        for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
            i2c_master_stop(cmd);
            esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(15));
            i2c_cmd_link_delete(cmd);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "  -> Dispositivo I2C encontrado en 0x%02X", addr);
                ++found;
            }
        }
        if (found == 0) {
            ESP_LOGI(TAG, "  (Ningún dispositivo I2C respondió en 21/22)");
        }
        i2c_driver_delete(I2C_NUM_0);
    }
}

core::config::ConfigStore           g_config("nextion_fw");
core::config::BoardPeripheralsConfig g_periph_cfg{};
core::peripherals::NextionDriver    g_display{};
core::peripherals::EncoderDriver    g_encoder{};
core::peripherals::PwmDriver        g_pwm{};
core::peripherals::AdcDriver        g_adc{};
core::peripherals::WifiService      g_wifi{};

std::shared_ptr<app::screens::WifiScanScreen> g_wifi_scan_screen;
std::unique_ptr<core::ui::UiNavigator>        g_navigator;
} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== Iniciando ESP32 Nextion Core Firmware (ESP-IDF + FreeRTOS) ===");

    // 1. Inicializar NVS y cargar configuración de periféricos (respeta pines por defecto o overrides en NVS)
    ESP_ERROR_CHECK(core::config::ConfigStore::initNvs());
    probeBoardHardware();
    g_config.loadPeripheralsConfig(g_periph_cfg);

    // 2. Inicializar periférico Nextion TFT (UART1: RX=16, TX=17, 9600 -> 38400 bps)
    g_display.init(g_periph_cfg.nextion);
    core::rtos::Task::delayMs(200);
    g_display.clearScreen();
    g_display.drawTitle("Cargando...");

    // 3. Inicializar periféricos PWM (GPIO 5, 300 Hz) y ADC1 (GPIO 34)
    g_pwm.init(g_periph_cfg.pwm);
    g_adc.init(g_periph_cfg.adc);
    for (int i = 0; i < 6; ++i) {
        ESP_LOGI(TAG, "ADC1[%d] %s -> Raw=%d, Volts=%.2fV",
                 i, core::peripherals::kAdc1Channels[i].label,
                 g_adc.readRawByIndex(i), g_adc.readVoltsByIndex(i));
    }

    // 4. Crear el navegador y motor de UI genérico
    g_navigator = std::make_unique<core::ui::UiNavigator>(g_display, g_wifi, g_pwm, g_adc, g_config);
    g_wifi_scan_screen = std::make_shared<app::screens::WifiScanScreen>();

    // 5. Inicializar Wi-Fi AP+STA y enlazar resultados de escaneo asíncrono a WifiScanScreen
    g_wifi.init(g_periph_cfg.wifi, [](const std::vector<core::peripherals::WifiNetworkInfo>& nets) {
        if (g_wifi_scan_screen) {
            g_wifi_scan_screen->updateNetworks(nets);
        }
    });

    // 6. Registrar todas las pantallas en el Router del motor UI
    g_navigator->registerScreen("settings",    [] { return std::make_shared<app::screens::SettingsScreen>(); });
    g_navigator->registerScreen("graphs",      [] { return std::make_shared<app::screens::GraphsMenuScreen>(); });
    g_navigator->registerScreen("sine-graph",  [] { return std::make_shared<app::screens::SineWaveScreen>(); });
    g_navigator->registerScreen("adc-graph",   [] { return std::make_shared<app::screens::AdcGraphScreen>(); });
    g_navigator->registerScreen("adc-all",     [] { return std::make_shared<app::screens::AdcAllPinsScreen>(); });
    g_navigator->registerScreen("wifi",        [] { return std::make_shared<app::screens::WifiScreen>(); });
    g_navigator->registerScreen("wifi-scan",   [] { return g_wifi_scan_screen; });
    g_navigator->registerScreen("bluetooth",   [] { return std::make_shared<app::screens::BluetoothScreen>(); });
    g_navigator->registerScreen("pwm-control", [] { return std::make_shared<app::screens::PwmControlScreen>(); });
    g_navigator->registerScreen("wifi-status", [] { return std::make_shared<app::screens::WifiStatusScreen>(); });
    g_navigator->registerScreen("sys-info",    [] { return std::make_shared<app::screens::WifiStatusScreen>(); });

    // 7. Arrancar la tarea de UI en FreeRTOS en el menú raíz ("settings")
    g_navigator->start("settings");

    // 8. Inicializar Encoder Rotatorio EC11 (CW=23, CCW=19, SW=18) conectado a la cola del UiNavigator
    g_encoder.init(g_periph_cfg.encoder, [](core::peripherals::EncoderEvent ev) {
        g_navigator->postEncoderEvent(ev);
    });

    ESP_LOGI(TAG, "Core Firmware listo y ejecutándose.");
}
