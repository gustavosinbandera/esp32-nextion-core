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
#include "core_portal.hpp"
#include "app_screens.hpp"

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

namespace {
constexpr const char* TAG = "MAIN_CORE";

core::config::ConfigStore            g_config("nextion_fw");
core::config::BoardPeripheralsConfig g_periph_cfg{};
core::peripherals::NextionDriver     g_display{};
core::peripherals::EncoderDriver     g_encoder{};
core::peripherals::PwmDriver         g_pwm{};
core::peripherals::AdcDriver         g_adc{};
core::peripherals::WifiService       g_wifi{};

std::shared_ptr<app::screens::WifiScanScreen> g_wifi_scan_screen;
std::unique_ptr<core::ui::UiNavigator>        g_navigator;
std::unique_ptr<core::portal::WebPortalService> g_portal;
} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== Iniciando ESP32 Nextion Core Firmware (ESP-IDF + FreeRTOS + WebSocket Portal) ===");

    // 1. Inicializar NVS y cargar configuración de periféricos
    ESP_ERROR_CHECK(core::config::ConfigStore::initNvs());
    g_config.loadPeripheralsConfig(g_periph_cfg);

    // 2. Inicializar periférico Nextion TFT (UART1: RX=16, TX=17, 9600 -> 38400 bps)
    g_display.init(g_periph_cfg.nextion);
    core::rtos::Task::delayMs(200);
    g_display.clearScreen();
    g_display.drawTitle("Cargando...");

    // 3. Inicializar periféricos PWM (GPIO 5, 300 Hz) y ADC1 (GPIO 32, 33, 34, 35, 36, 39)
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

    // 8. Iniciar Captive Portal + Servidor HTTP/WebSocket sincronizado en Core 0 (http://192.168.4.1)
    const size_t html_len = (index_html_end - index_html_start) - 1;
    g_portal = std::make_unique<core::portal::WebPortalService>(*g_navigator);
    g_portal->start(index_html_start, html_len);

    // 9. Inicializar Encoder Rotatorio EC11 (CW=23, CCW=19, SW=18) conectado a la cola del UiNavigator
    g_encoder.init(g_periph_cfg.encoder, [](core::peripherals::EncoderEvent ev) {
        g_navigator->postEncoderEvent(ev);
    });

    ESP_LOGI(TAG, "Core Firmware + Web Portal listos en Wi-Fi '%s' -> http://192.168.4.1",
             g_periph_cfg.wifi.ap_ssid.c_str());
}
