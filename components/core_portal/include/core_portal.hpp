#pragma once

#include <array>
#include <cstdint>
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "core_rtos.hpp"
#include "core_ui.hpp"

namespace core::portal {

struct DigitalOutPin {
    uint8_t idx;
    gpio_num_t gpio;
    const char* name;
    bool state;
};

class WebPortalService {
public:
    explicit WebPortalService(ui::UiNavigator& nav) : nav_(nav) {}

    void start(const uint8_t* html_start, size_t html_len);

private:
    void initControllableGpios();
    bool setDigitalPin(int gpio_num, bool state);

    void dnsCaptiveLoop();
    void wsTelemetryStreamLoop();
    void broadcastJson(const char* json_str);
    void handleIncomingWsJson(const char* raw_json);

    static esp_err_t handleRootGet(httpd_req_t* req);
    static esp_err_t handleCaptiveRedirect(httpd_req_t* req, httpd_err_code_t err);
    static esp_err_t handleWs(httpd_req_t* req);

    ui::UiNavigator& nav_;
    httpd_handle_t server_{nullptr};
    const uint8_t* html_ptr_{nullptr};
    size_t html_len_{0};

    rtos::Task dns_task_{};
    rtos::Task ws_stream_task_{};
    rtos::Mutex gpio_mutex_{};

    std::array<DigitalOutPin, 5> digital_pins_{{
        {0, GPIO_NUM_2,  "GPIO 2 (Aux/LED)",   false},
        {1, GPIO_NUM_4,  "GPIO 4 (DS/Aux)",    false},
        {2, GPIO_NUM_13, "GPIO 13 (JTAG/IO)",  false},
        {3, GPIO_NUM_14, "GPIO 14 (JTAG/IO)",  false},
        {4, GPIO_NUM_25, "GPIO 25 (NFC/Toggle)", false},
    }};
    float web_sine_phase_{0.0f};
};

} // namespace core::portal
