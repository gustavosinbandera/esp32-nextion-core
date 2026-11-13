#include "core_config.hpp"
#include "nvs_flash.h"
#include "nvs.h"

namespace core::config {

esp_err_t ConfigStore::initNvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

std::string ConfigStore::getString(const char* key, const char* def) const {
    rtos::LockGuard lock(mutex_);
    nvs_handle_t h{};
    if (nvs_open(ns_, NVS_READONLY, &h) != ESP_OK) return def ? def : "";
    size_t len = 0;
    std::string out = def ? def : "";
    if (nvs_get_str(h, key, nullptr, &len) == ESP_OK && len > 0) {
        out.resize(len);
        if (nvs_get_str(h, key, out.data(), &len) == ESP_OK && !out.empty() && out.back() == '\0') {
            out.pop_back();
        }
    }
    nvs_close(h);
    return out;
}

int32_t ConfigStore::getInt(const char* key, int32_t def) const {
    rtos::LockGuard lock(mutex_);
    nvs_handle_t h{};
    if (nvs_open(ns_, NVS_READONLY, &h) != ESP_OK) return def;
    int32_t v = def;
    nvs_get_i32(h, key, &v);
    nvs_close(h);
    return v;
}

bool ConfigStore::getBool(const char* key, bool def) const {
    return getInt(key, def ? 1 : 0) != 0;
}

esp_err_t ConfigStore::setString(const char* key, const char* val) {
    rtos::LockGuard lock(mutex_);
    nvs_handle_t h{};
    esp_err_t err = nvs_open(ns_, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_str(h, key, val ? val : "");
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t ConfigStore::setInt(const char* key, int32_t val) {
    rtos::LockGuard lock(mutex_);
    nvs_handle_t h{};
    esp_err_t err = nvs_open(ns_, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_i32(h, key, val);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t ConfigStore::setBool(const char* key, bool val) {
    return setInt(key, val ? 1 : 0);
}

void ConfigStore::loadPeripheralsConfig(BoardPeripheralsConfig& cfg) const {
    cfg.nextion.rx_gpio = static_cast<gpio_num_t>(getInt("screen.rx", cfg.nextion.rx_gpio));
    cfg.nextion.tx_gpio = static_cast<gpio_num_t>(getInt("screen.tx", cfg.nextion.tx_gpio));
    cfg.encoder.cw_gpio = static_cast<gpio_num_t>(getInt("enc.cw", cfg.encoder.cw_gpio));
    cfg.encoder.ccw_gpio = static_cast<gpio_num_t>(getInt("enc.ccw", cfg.encoder.ccw_gpio));
    cfg.encoder.sw_gpio = static_cast<gpio_num_t>(getInt("enc.sw", cfg.encoder.sw_gpio));
    cfg.pwm.gpio = static_cast<gpio_num_t>(getInt("pwm.gpio", cfg.pwm.gpio));
    cfg.pwm.freq_hz = static_cast<uint32_t>(getInt("pwm.freq", cfg.pwm.freq_hz));
}

} // namespace core::config
