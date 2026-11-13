#pragma once

#include <cstdint>
#include <functional>
#include "core_config.hpp"
#include "core_rtos.hpp"
#include "driver/pulse_cnt.h"

namespace core::peripherals {

enum class EncoderEvent : uint8_t {
    RotateRight = 1, // Giro horario
    RotateLeft  = 2, // Giro antihorario
    Click       = 3, // Pulsación corta del botón SW (GPIO 18)
    LongPress   = 4, // Pulsación larga (>800 ms) del botón SW
};

class EncoderDriver {
public:
    using EventCallback = std::function<void(EncoderEvent)>;

    EncoderDriver() : evt_queue_(16) {}

    bool init(const config::EncoderConfig& cfg, EventCallback cb);
    void injectEvent(EncoderEvent ev);

private:
    static bool IRAM_ATTR onPcntWatch(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t* edata, void* user_ctx);
    void workerLoop();

    config::EncoderConfig cfg_{};
    EventCallback cb_{};
    pcnt_unit_handle_t pcnt_unit_{nullptr};
    rtos::Queue<int> evt_queue_;
    rtos::Task worker_task_{};
};

} // namespace core::peripherals
