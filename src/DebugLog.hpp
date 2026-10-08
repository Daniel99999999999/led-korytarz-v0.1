#pragma once
#include "DebugBuffer.hpp"
#include "WebLogBuffer.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <atomic>
#include <cstdarg>

class DebugLog final {
  public:
    struct Status {
        bool active{false};
        std::uint32_t dropped{0};
    };
    static DebugLog& instance() noexcept;
    void install() noexcept;
    bool setEnabled(bool enabled) noexcept;
    bool status(Status& output) noexcept;
    bool pop(DebugBuffer::Message& output) noexcept;
    bool nextWebEntry(std::uint32_t after, WebLogBuffer::Entry& output, bool& gap) noexcept;
    void setUartEnabled(bool enabled) noexcept;
    bool uartEnabled() const noexcept;
    void noteTransportDrop() noexcept;
    void excludePublisherTask(TaskHandle_t task) noexcept;
    static std::uint32_t seconds() noexcept;
    static const char* resetReason() noexcept;

  private:
    static int output(const char* format, va_list args);
    int capture(const char* format, va_list args) noexcept;
    DebugBuffer buffer_{};
    WebLogBuffer web_buffer_{};
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
    std::atomic<bool> capture_enabled_{false};
    std::atomic<bool> uart_enabled_{true};
    std::atomic<std::uint32_t> adapter_dropped_{0};
    std::atomic<TaskHandle_t> publisher_{nullptr};
    std::atomic<vprintf_like_t> previous_{nullptr};
    bool installed_{false};
};
