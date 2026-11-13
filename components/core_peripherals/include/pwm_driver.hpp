#pragma once

#include <cstdint>
#include "core_config.hpp"

namespace core::peripherals {

class PwmDriver {
public:
    bool init(const config::PwmConfig& cfg);
    void setDutyPercent(uint8_t duty_0_to_100);
    void setFrequency(uint32_t freq_hz);

    uint8_t dutyPercent() const { return duty_percent_; }
    uint32_t frequency() const { return cfg_.freq_hz; }

private:
    config::PwmConfig cfg_{};
    uint8_t duty_percent_{0};
};

} // namespace core::peripherals
