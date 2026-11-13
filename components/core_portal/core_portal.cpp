#include "core_portal.hpp"

#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

namespace core::portal {

namespace {
constexpr const char* TAG = "WEB_PORTAL";
constexpr size_t MAX_WS_CLIENTS = 6;

#pragma pack(push, 1)
struct DnsHeader {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
};
struct DnsAnswer {
    uint16_t ptr_offset;
    uint16_t type;
    uint16_t clas;
    uint32_t ttl;
    uint16_t data_len;
    uint32_t ip_addr;
};
#pragma pack(pop)
} // namespace

void WebPortalService::start(const uint8_t* html_start, size_t html_len) {
    html_ptr_ = html_start;
    html_len_ = html_len;

    initControllableGpios();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.core_id = 0; // Core 0 (PRO_CPU)
    config.stack_size = 6144;

    ESP_ERROR_CHECK(httpd_start(&server_, &config));

    const httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = &WebPortalService::handleRootGet,
        .user_ctx = this,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr
    };
    httpd_register_uri_handler(server_, &root_uri);

    const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = &WebPortalService::handleWs,
        .user_ctx = this,
        .is_websocket = true,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr
    };
    httpd_register_uri_handler(server_, &ws_uri);

    httpd_register_err_handler(server_, HTTPD_404_NOT_FOUND, &WebPortalService::handleCaptiveRedirect);

    dns_task_.start("portal_dns", 4096, 3, 0, [this] { dnsCaptiveLoop(); });
    ws_stream_task_.start("portal_ws_tx", 4096, 4, 0, [this] { wsTelemetryStreamLoop(); });

    ESP_LOGI(TAG, "Captive Portal + Servidor WebSocket activo en http://192.168.4.1 (Core 0)");
}

void WebPortalService::initControllableGpios() {
    uint64_t mask = 0;
    for (const auto& p : digital_pins_) {
        mask |= (1ULL << p.gpio);
    }
    gpio_config_t io_cfg{};
    io_cfg.intr_type = GPIO_INTR_DISABLE;
    io_cfg.mode = GPIO_MODE_OUTPUT;
    io_cfg.pin_bit_mask = mask;
    io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_cfg);

    for (const auto& p : digital_pins_) {
        gpio_set_level(p.gpio, 0);
    }
}

bool WebPortalService::setDigitalPin(int gpio_num, bool state) {
    rtos::LockGuard lock(gpio_mutex_);
    for (auto& p : digital_pins_) {
        if (static_cast<int>(p.gpio) == gpio_num) {
            p.state = state;
            gpio_set_level(p.gpio, state ? 1 : 0);
            ESP_LOGI(TAG, "Web GPIO %d -> %s", gpio_num, state ? "HIGH (ON)" : "LOW (OFF)");
            return true;
        }
    }
    return false;
}

esp_err_t WebPortalService::handleRootGet(httpd_req_t* req) {
    auto* self = static_cast<WebPortalService*>(req->user_ctx);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    return httpd_resp_send(req, reinterpret_cast<const char*>(self->html_ptr_), self->html_len_);
}

esp_err_t WebPortalService::handleCaptiveRedirect(httpd_req_t* req, httpd_err_code_t /*err*/) {
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, "Redirecting to ESP32 Nextion Portal...", HTTPD_RESP_USE_STRLEN);
}

esp_err_t WebPortalService::handleWs(httpd_req_t* req) {
    auto* self = static_cast<WebPortalService*>(req->user_ctx);
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Cliente Web conectado al WebSocket (fd=%d)", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t pkt{};
    pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
    if (ret != ESP_OK || pkt.len == 0 || pkt.len > 512) return ret;

    char buf[513]{};
    pkt.payload = reinterpret_cast<uint8_t*>(buf);
    ret = httpd_ws_recv_frame(req, &pkt, pkt.len);
    if (ret != ESP_OK) return ret;
    buf[pkt.len] = '\0';

    self->handleIncomingWsJson(buf);
    return ESP_OK;
}

void WebPortalService::handleIncomingWsJson(const char* raw_json) {
    cJSON* root = cJSON_Parse(raw_json);
    if (!root) return;

    const cJSON* cmd_it = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cJSON_IsString(cmd_it) || !cmd_it->valuestring) {
        cJSON_Delete(root);
        return;
    }
    const char* cmd = cmd_it->valuestring;
    auto& ctx = nav_.context();
    auto& sh  = nav_.sharedState();

    // 1. Cambiar pantalla / gráfica desde la Web (sincronizado con la Nextion si sync=true)
    if (std::strcmp(cmd, "set_screen") == 0) {
        const cJSON* scr = cJSON_GetObjectItemCaseSensitive(root, "screen");
        const cJSON* sync = cJSON_GetObjectItemCaseSensitive(root, "sync");
        if (cJSON_IsBool(sync)) {
            sh.sync_screen_from_web = cJSON_IsTrue(sync);
        }
        if (cJSON_IsString(scr) && scr->valuestring) {
            if (sh.sync_screen_from_web) {
                nav_.navigateTo(scr->valuestring);
            } else {
                std::snprintf(sh.active_screen, sizeof(sh.active_screen), "%s", scr->valuestring);
            }
        }
    }
    // 2. Seleccionar canal ADC (0..5) -> actualiza AdcDriver y refresca Nextion si está en ADC Scope
    else if (std::strcmp(cmd, "set_adc_ch") == 0) {
        const cJSON* idx = cJSON_GetObjectItemCaseSensitive(root, "index");
        if (cJSON_IsNumber(idx)) {
            ctx.adc.selectChannelIndex(idx->valueint);
            sh.selected_adc_idx = ctx.adc.selectedChannelIndex();
            if (sh.sync_screen_from_web) {
                nav_.requestRefresh();
            }
        }
    }
    // 3. Ajustar parámetros de Sine Wave (Freq / Amp) o ADC Gain desde la Web
    else if (std::strcmp(cmd, "set_graph_params") == 0) {
        const cJSON* f = cJSON_GetObjectItemCaseSensitive(root, "freq");
        const cJSON* a = cJSON_GetObjectItemCaseSensitive(root, "amp");
        const cJSON* g = cJSON_GetObjectItemCaseSensitive(root, "gain");
        if (cJSON_IsNumber(f)) sh.sine_freq_hz = std::clamp(static_cast<float>(f->valuedouble), 0.25f, 6.0f);
        if (cJSON_IsNumber(a)) sh.sine_amp_px  = std::clamp(a->valueint, 12, 88);
        if (cJSON_IsNumber(g)) sh.adc_gain_x   = std::clamp(g->valueint, 1, 4);
        if (sh.sync_screen_from_web) {
            nav_.requestRefresh();
        }
    }
    // 4. Controlar GPIO digital desde la Web
    else if (std::strcmp(cmd, "set_gpio") == 0) {
        const cJSON* pin = cJSON_GetObjectItemCaseSensitive(root, "gpio");
        const cJSON* st  = cJSON_GetObjectItemCaseSensitive(root, "state");
        if (cJSON_IsNumber(pin) && cJSON_IsBool(st)) {
            setDigitalPin(pin->valueint, cJSON_IsTrue(st));
        }
    }
    // 5. Controlar PWM (GPIO 5) desde la Web
    else if (std::strcmp(cmd, "set_pwm") == 0) {
        const cJSON* duty = cJSON_GetObjectItemCaseSensitive(root, "duty");
        const cJSON* freq = cJSON_GetObjectItemCaseSensitive(root, "freq");
        if (cJSON_IsNumber(duty)) ctx.pwm.setDutyPercent(static_cast<uint8_t>(duty->valueint));
        if (cJSON_IsNumber(freq)) ctx.pwm.setFrequency(static_cast<uint32_t>(freq->valueint));
        if (sh.sync_screen_from_web && std::strcmp(sh.active_screen, "pwm-control") == 0) {
            nav_.requestRefresh();
        }
    }
    // 6. Enviar evento de Encoder virtual desde la Web (Right, Left, Click, LongPress)
    else if (std::strcmp(cmd, "encoder") == 0) {
        const cJSON* act = cJSON_GetObjectItemCaseSensitive(root, "action");
        if (cJSON_IsString(act) && act->valuestring) {
            using peripherals::EncoderEvent;
            if (std::strcmp(act->valuestring, "right") == 0) nav_.postEncoderEvent(EncoderEvent::RotateRight);
            else if (std::strcmp(act->valuestring, "left") == 0) nav_.postEncoderEvent(EncoderEvent::RotateLeft);
            else if (std::strcmp(act->valuestring, "click") == 0) nav_.postEncoderEvent(EncoderEvent::Click);
            else if (std::strcmp(act->valuestring, "back") == 0) nav_.postEncoderEvent(EncoderEvent::LongPress);
        }
    }

    cJSON_Delete(root);
}

void WebPortalService::wsTelemetryStreamLoop() {
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(80)); // ~12.5 Hz stream por WebSocket

        if (!server_) continue;
        size_t fds = MAX_WS_CLIENTS;
        int client_fds[MAX_WS_CLIENTS]{};
        if (httpd_get_client_list(server_, &fds, client_fds) != ESP_OK || fds == 0) continue;

        bool any_ws = false;
        for (size_t i = 0; i < fds; ++i) {
            if (httpd_ws_get_fd_info(server_, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
                any_ws = true;
                break;
            }
        }
        if (!any_ws) continue;

        auto& ctx = nav_.context();
        auto& sh  = nav_.sharedState();
        sh.selected_adc_idx = ctx.adc.selectedChannelIndex();
        const auto& adc_desc = ctx.adc.currentChannelDesc();
        const int adc_raw    = ctx.adc.readRaw();
        const float adc_v    = ctx.adc.readVolts();

        // Avanzar fase senoidal de respaldo por si la Nextion está en otra pantalla pero la web mira el seno
        web_sine_phase_ += (2.0f * 3.14159265f * sh.sine_freq_hz) / 18.0f;
        if (web_sine_phase_ > 628.3185f) web_sine_phase_ -= 628.3185f;
        float sine_val = std::sin(web_sine_phase_) * (static_cast<float>(sh.sine_amp_px) / 88.0f);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "state");
        cJSON_AddStringToObject(root, "screen", sh.active_screen);
        cJSON_AddBoolToObject(root, "sync", sh.sync_screen_from_web);
        cJSON_AddNumberToObject(root, "adc_idx", sh.selected_adc_idx);
        cJSON_AddStringToObject(root, "adc_label", adc_desc.label);
        cJSON_AddNumberToObject(root, "adc_gpio", static_cast<int>(adc_desc.gpio));
        cJSON_AddNumberToObject(root, "adc_raw", adc_raw);
        cJSON_AddNumberToObject(root, "adc_v", adc_v);
        cJSON_AddNumberToObject(root, "adc_gain", sh.adc_gain_x);
        cJSON_AddNumberToObject(root, "sine_freq", sh.sine_freq_hz);
        cJSON_AddNumberToObject(root, "sine_amp", sh.sine_amp_px);
        cJSON_AddNumberToObject(root, "sine_sample", sine_val);
        cJSON_AddNumberToObject(root, "pwm_duty", ctx.pwm.dutyPercent());
        cJSON_AddNumberToObject(root, "pwm_freq", ctx.pwm.frequency());

        // Array de los 6 canales ADC1
        cJSON* adc_arr = cJSON_AddArrayToObject(root, "adc_all");
        for (int i = 0; i < 6; ++i) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "gpio", static_cast<int>(peripherals::kAdc1Channels[i].gpio));
            cJSON_AddNumberToObject(item, "raw", ctx.adc.readRawByIndex(i));
            cJSON_AddNumberToObject(item, "v", ctx.adc.readVoltsByIndex(i));
            cJSON_AddItemToArray(adc_arr, item);
        }

        // Array de GPIOs digitales controlables
        cJSON* gpio_arr = cJSON_AddArrayToObject(root, "gpios");
        {
            rtos::LockGuard lock(gpio_mutex_);
            for (const auto& p : digital_pins_) {
                cJSON* g = cJSON_CreateObject();
                cJSON_AddNumberToObject(g, "gpio", static_cast<int>(p.gpio));
                cJSON_AddStringToObject(g, "name", p.name);
                cJSON_AddBoolToObject(g, "state", p.state);
                cJSON_AddItemToArray(gpio_arr, g);
            }
        }

        char* out = cJSON_PrintUnformatted(root);
        if (out) {
            broadcastJson(out);
            cJSON_free(out);
        }
        cJSON_Delete(root);
    }
}

void WebPortalService::broadcastJson(const char* json_str) {
    size_t fds = MAX_WS_CLIENTS;
    int client_fds[MAX_WS_CLIENTS]{};
    if (httpd_get_client_list(server_, &fds, client_fds) != ESP_OK) return;

    httpd_ws_frame_t ws_pkt{};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(json_str));
    ws_pkt.len = std::strlen(json_str);

    for (size_t i = 0; i < fds; ++i) {
        if (httpd_ws_get_fd_info(server_, client_fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(server_, client_fds[i], &ws_pkt);
        }
    }
}

void WebPortalService::dnsCaptiveLoop() {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) return;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(53);
    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(sock);
        return;
    }

    uint8_t buf[512];
    sockaddr_in src{};
    socklen_t slen = sizeof(src);

    while (true) {
        int len = recvfrom(sock, buf, sizeof(buf) - sizeof(DnsAnswer), 0,
                           reinterpret_cast<sockaddr*>(&src), &slen);
        if (len < static_cast<int>(sizeof(DnsHeader))) continue;

        auto* hdr = reinterpret_cast<DnsHeader*>(buf);
        if (ntohs(hdr->qd_count) == 0) continue;

        hdr->flags = htons(0x8180);
        hdr->an_count = htons(1);
        hdr->ns_count = 0;
        hdr->ar_count = 0;

        DnsAnswer ans{htons(0xC00C), htons(1), htons(1), htonl(60), htons(4), inet_addr("192.168.4.1")};
        std::memcpy(buf + len, &ans, sizeof(ans));
        sendto(sock, buf, len + sizeof(ans), 0, reinterpret_cast<sockaddr*>(&src), slen);
    }
}

} // namespace core::portal
