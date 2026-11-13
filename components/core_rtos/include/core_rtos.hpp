#pragma once

#include <cstdint>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

namespace core::rtos {

class Mutex {
public:
    Mutex() : handle_(xSemaphoreCreateRecursiveMutex()) {}
    ~Mutex() {
        if (handle_) vSemaphoreDelete(handle_);
    }
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock() const {
        if (handle_) xSemaphoreTakeRecursive(handle_, portMAX_DELAY);
    }
    void unlock() const {
        if (handle_) xSemaphoreGiveRecursive(handle_);
    }

private:
    SemaphoreHandle_t handle_{nullptr};
};

class LockGuard {
public:
    explicit LockGuard(const Mutex& m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    const Mutex& m_;
};

template <typename T>
class Queue {
public:
    explicit Queue(UBaseType_t capacity) : handle_(xQueueCreate(capacity, sizeof(T))) {}
    ~Queue() {
        if (handle_) vQueueDelete(handle_);
    }
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    bool send(const T& item, TickType_t wait_ticks = 0) const {
        return handle_ && (xQueueSend(handle_, &item, wait_ticks) == pdTRUE);
    }
    bool sendFromIsr(const T& item, BaseType_t* hp_task_awoken) const {
        return handle_ && (xQueueSendFromISR(handle_, &item, hp_task_awoken) == pdTRUE);
    }
    bool receive(T& out, TickType_t wait_ticks = portMAX_DELAY) const {
        return handle_ && (xQueueReceive(handle_, &out, wait_ticks) == pdTRUE);
    }
    void clear() const {
        if (handle_) xQueueReset(handle_);
    }

private:
    QueueHandle_t handle_{nullptr};
};

class Task {
public:
    using Fn = std::function<void()>;

    Task() = default;
    bool start(const char* name, uint32_t stack_bytes, UBaseType_t priority,
               BaseType_t core_id, Fn fn);

    static void delayMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
    static int currentCore() { return xPortGetCoreID(); }

private:
    static void entry(void* arg);
    TaskHandle_t handle_{nullptr};
    Fn fn_{};
};

} // namespace core::rtos
