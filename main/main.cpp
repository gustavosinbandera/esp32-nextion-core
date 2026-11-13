#include <memory>
#include "esp_log.h"

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
    g_config.loadPeripheralsConfig(g_periph_cfg);

    // 2. Inicializar periférico Nextion TFT (UART1: RX=16, TX=17, 9600 -> 38400 bps)
    g_display.init(g_periph_cfg.nextion);
    core::rtos::Task::delayMs(200);
    g_display.clearScreen();
    g_display.drawTitle("Cargando...");

    // 3. Inicializar periféricos PWM (GPIO 5, 300 Hz) y ADC1 (GPIO 34)
    g_pwm.init(g_periph_cfg.pwm);
    g_adc.init(g_periph_cfg.adc);

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
    g_navigator->registerScreen("scope",       [] { return std::make_shared<app::screens::WaveformScreen>(); });
    g_navigator->registerScreen("settings",    [] { return std::make_shared<app::screens::SettingsScreen>(); });
    g_navigator->registerScreen("wifi",        [] { return std::make_shared<app::screens::WifiScreen>(); });
    g_navigator->registerScreen("wifi-scan",   [] { return g_wifi_scan_screen; });
    g_navigator->registerScreen("bluetooth",   [] { return std::make_shared<app::screens::BluetoothScreen>(); });
    g_navigator->registerScreen("pwm-control", [] { return std::make_shared<app::screens::PwmControlScreen>(); });
    g_navigator->registerScreen("wifi-status", [] { return std::make_shared<app::screens::WifiStatusScreen>(); });
    g_navigator->registerScreen("sys-info",    [] { return std::make_shared<app::screens::WifiStatusScreen>(); });

    // 7. Arrancar la tarea de UI en FreeRTOS con "settings" en la base y abrir directo el osciloscopio ("scope")
    g_navigator->start("settings");
    g_navigator->push("scope");

    // 8. Inicializar Encoder Rotatorio EC11 (CW=23, CCW=19, SW=18) conectado a la cola del UiNavigator
    g_encoder.init(g_periph_cfg.encoder, [](core::peripherals::EncoderEvent ev) {
        g_navigator->postEncoderEvent(ev);
    });

    ESP_LOGI(TAG, "Core Firmware listo y ejecutándose.");
}
