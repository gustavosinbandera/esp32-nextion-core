#include "core_rtos.hpp"

namespace core::rtos {

bool Task::start(const char* name, uint32_t stack_bytes, UBaseType_t priority,
                 BaseType_t core_id, Fn fn) {
    fn_ = std::move(fn);
    return (xTaskCreatePinnedToCore(&Task::entry, name, stack_bytes, this,
                                    priority, &handle_, core_id) == pdPASS);
}

void Task::entry(void* arg) {
    auto* self = static_cast<Task*>(arg);
    if (self && self->fn_) {
        self->fn_();
    }
    vTaskDelete(nullptr);
}

} // namespace core::rtos
