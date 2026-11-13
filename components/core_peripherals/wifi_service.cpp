#include "wifi_service.hpp"

#include <cstring>
#include <cstdio>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"

namespace core::peripherals {

namespace {
constexpr const char* TAG = "WIFI_SVC";
}

bool WifiService::init(const config::WifiConfig& cfg, ScanDoneCallback on_scan_done) {
    cfg_ = cfg;
    on_scan_done_ = std::move(on_scan_done);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiService::onWifiOrIpEvent, this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiService::onWifiOrIpEvent, this));

    wifi_config_t ap_cfg{};
    std::strncpy(reinterpret_cast<char*>(ap_cfg.ap.ssid), cfg_.ap_ssid.c_str(), sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = cfg_.ap_ssid.size();
    ap_cfg.ap.channel = cfg_.ap_channel;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP+STA iniciado (AP SSID: %s, canal %d)", cfg_.ap_ssid.c_str(), cfg_.ap_channel);
    return true;
}

bool WifiService::startAsyncScan() {
    {
        rtos::LockGuard lock(mutex_);
        if (scanning_) return false;
        scanning_ = true;
    }

    wifi_scan_config_t scan_cfg{};
    scan_cfg.show_hidden = false;
    // block = false -> escaneo 100% asíncrono, avisa por WIFI_EVENT_SCAN_DONE
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
    if (err != ESP_OK) {
        rtos::LockGuard lock(mutex_);
        scanning_ = false;
        return false;
    }
    ESP_LOGI(TAG, "Escaneo Wi-Fi asíncrono iniciado...");
    return true;
}

bool WifiService::connectSta(const std::string& ssid, const std::string& password) {
    {
        rtos::LockGuard lock(mutex_);
        sta_ssid_ = ssid;
    }
    wifi_config_t sta_cfg{};
    std::strncpy(reinterpret_cast<char*>(sta_cfg.sta.ssid), ssid.c_str(), sizeof(sta_cfg.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(sta_cfg.sta.password), password.c_str(), sizeof(sta_cfg.sta.password) - 1);

    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    return (esp_wifi_connect() == ESP_OK);
}

void WifiService::onWifiOrIpEvent(void* arg, const char* base, int32_t id, void* data) {
    auto* self = static_cast<WifiService*>(arg);

    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        self->collectScanResults();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* ev = static_cast<ip_event_got_ip_t*>(data);
        char ip_buf[16];
        std::snprintf(ip_buf, sizeof(ip_buf), IPSTR, IP2STR(&ev->ip_info.ip));
        rtos::LockGuard lock(self->mutex_);
        self->sta_ip_ = ip_buf;
        self->sta_connected_ = true;
        ESP_LOGI(TAG, "Wi-Fi STA conectado con IP: %s", ip_buf);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        rtos::LockGuard lock(self->mutex_);
        self->sta_connected_ = false;
        self->sta_ip_ = "0.0.0.0";
    }
}

void WifiService::collectScanResults() {
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 24) ap_count = 24;

    std::vector<wifi_ap_record_t> records(ap_count);
    std::vector<WifiNetworkInfo> results;

    if (ap_count > 0 && esp_wifi_scan_get_ap_records(&ap_count, records.data()) == ESP_OK) {
        results.reserve(ap_count);
        for (uint16_t i = 0; i < ap_count; ++i) {
            const char* ssid_str = reinterpret_cast<const char*>(records[i].ssid);
            if (!*ssid_str) continue;
            WifiNetworkInfo info;
            info.ssid = ssid_str;
            info.rssi = records[i].rssi;
            info.channel = records[i].primary;
            info.is_open = (records[i].authmode == WIFI_AUTH_OPEN);
            results.push_back(std::move(info));
        }
    }

    {
        rtos::LockGuard lock(mutex_);
        scanning_ = false;
    }

    ESP_LOGI(TAG, "Escaneo Wi-Fi completado: %zu redes encontradas", results.size());
    if (on_scan_done_) {
        on_scan_done_(results);
    }
}

std::string WifiService::currentSsid() const {
    rtos::LockGuard lock(mutex_);
    return sta_ssid_;
}

std::string WifiService::currentIp() const {
    rtos::LockGuard lock(mutex_);
    return sta_ip_;
}

} // namespace core::peripherals
