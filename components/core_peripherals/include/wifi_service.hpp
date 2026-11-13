#pragma once

#include <string>
#include <vector>
#include <functional>
#include "core_config.hpp"
#include "core_rtos.hpp"

namespace core::peripherals {

struct WifiNetworkInfo {
    std::string ssid;
    int8_t rssi{0};
    uint8_t channel{0};
    bool is_open{false};
};

class WifiService {
public:
    using ScanDoneCallback = std::function<void(const std::vector<WifiNetworkInfo>&)>;

    WifiService() = default;

    bool init(const config::WifiConfig& cfg, ScanDoneCallback on_scan_done);

    // Inicia escaneo Wi-Fi asíncrono (sin bloquear la UI ni el encoder)
    bool startAsyncScan();

    // Conecta a una red Wi-Fi STA
    bool connectSta(const std::string& ssid, const std::string& password);

    bool isConnected() const { return sta_connected_; }
    std::string currentSsid() const;
    std::string currentIp() const;

private:
    static void onWifiOrIpEvent(void* arg, const char* base, int32_t id, void* data);
    void collectScanResults();

    config::WifiConfig cfg_{};
    ScanDoneCallback on_scan_done_{};
    mutable rtos::Mutex mutex_{};
    bool scanning_{false};
    bool sta_connected_{false};
    std::string sta_ssid_{};
    std::string sta_ip_{"0.0.0.0"};
};

} // namespace core::peripherals
