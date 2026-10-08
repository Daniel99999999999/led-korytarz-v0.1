#include "DebugLog.hpp"

#include "esp_system.h"
#include "esp_timer.h"

#include <cstdio>

DebugLog& DebugLog::instance() noexcept {
    static DebugLog instance;
    return instance;
}
void DebugLog::install() noexcept {
    if (installed_) {
        return;
    }
    previous_ = &vprintf;
    previous_ = esp_log_set_vprintf(output);
    installed_ = true;
}
std::uint32_t DebugLog::seconds() noexcept {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1'000'000);
}
bool DebugLog::setEnabled(const bool enabled) noexcept {
    if (busy_.test_and_set(std::memory_order_acquire)) {
        return false;
    }
    if (enabled && !buffer_.active(seconds())) {
        adapter_dropped_.store(0);
    }
    buffer_.setEnabled(enabled, seconds());
    capture_enabled_.store(enabled, std::memory_order_release);
    busy_.clear(std::memory_order_release);
    return true;
}
bool DebugLog::status(Status& value) noexcept {
    if (busy_.test_and_set(std::memory_order_acquire)) {
        return false;
    }
    value.active = buffer_.active(seconds());
    value.dropped = buffer_.dropped() + adapter_dropped_.load(std::memory_order_relaxed);
    capture_enabled_.store(value.active, std::memory_order_release);
    busy_.clear(std::memory_order_release);
    return true;
}
bool DebugLog::pop(DebugBuffer::Message& value) noexcept {
    if (busy_.test_and_set(std::memory_order_acquire)) {
        return false;
    }
    const bool available = buffer_.pop(value, seconds());
    busy_.clear(std::memory_order_release);
    return available;
}
bool DebugLog::nextWebEntry(const std::uint32_t after, WebLogBuffer::Entry& output,
                            bool& gap) noexcept {
    if (busy_.test_and_set(std::memory_order_acquire))
        return false;
    const bool available = web_buffer_.nextAfter(after, output, gap);
    busy_.clear(std::memory_order_release);
    return available;
}
void DebugLog::setUartEnabled(const bool enabled) noexcept {
    uart_enabled_.store(enabled, std::memory_order_release);
}
bool DebugLog::uartEnabled() const noexcept {
    return uart_enabled_.load(std::memory_order_acquire);
}
void DebugLog::noteTransportDrop() noexcept {
    adapter_dropped_.fetch_add(1, std::memory_order_relaxed);
}
void DebugLog::excludePublisherTask(TaskHandle_t task) noexcept {
    publisher_.store(task);
}
int DebugLog::output(const char* format, va_list args) {
    auto& logger = instance();
    const bool uart_enabled = logger.uartEnabled();
    int result = 0;
    if (uart_enabled) {
        va_list uart_args;
        va_copy(uart_args, args);
        result = logger.previous_.load(std::memory_order_acquire)(format, uart_args);
        va_end(uart_args);
    }
    // ESP_LOG is not an ISR API. Do not add formatting/queue work to such a call.
    if (!xPortInIsrContext()) {
        va_list copy;
        va_copy(copy, args);
        const int captured = logger.capture(format, copy);
        va_end(copy);
        if (!uart_enabled)
            result = captured;
    }
    return result;
}
int DebugLog::capture(const char* format, va_list args) noexcept {
    DebugBuffer::Message text{};
    const int written = std::vsnprintf(text.data(), text.size(), format, args);
    if (written <= 0) {
        return written;
    }
    // ASCII transport: remove control/ANSI bytes, preserve line breaks. Never emit broken UTF-8.
    std::size_t end = 0;
    bool escape = false;
    for (std::size_t i = 0; i + 1U < text.size() && text[i] != '\0'; ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 27U) {
            escape = true;
            continue;
        }
        if (escape) {
            if (c >= 64U && c <= 126U && c != '[') {
                escape = false;
            }
            continue;
        }
        if (c == '\n' || c == '\t' || c >= 32U) {
            text[end++] = c < 127U ? static_cast<char>(c) : '?';
        }
    }
    text[end] = '\0';
    if (busy_.test_and_set(std::memory_order_acquire)) {
        noteTransportDrop();
        return written;
    }
    web_buffer_.push(text.data());
    if (capture_enabled_.load(std::memory_order_acquire) &&
        xTaskGetCurrentTaskHandle() != publisher_.load(std::memory_order_relaxed)) {
        if (static_cast<std::size_t>(written) >= text.size()) {
            buffer_.noteDropped();
        }
        static_cast<void>(buffer_.push(text.data(), seconds()));
    }
    busy_.clear(std::memory_order_release);
    return written;
}
const char* DebugLog::resetReason() noexcept {
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
        return "power_on";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:
        return "task_watchdog";
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deep_sleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    default:
        return "other";
    }
}
