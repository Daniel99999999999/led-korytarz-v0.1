#include "StackMonitor.hpp"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <atomic>
namespace stack_monitor {
namespace {
struct Slot {
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    stack_model::Record record{};
};
std::array<Slot, kCount>& slots() noexcept {
    static std::array<Slot, kCount> values{};
    return values;
}
} // namespace
const char* name(Task task) noexcept {
    constexpr const char* names[] = {"main",         "led_control", "lcd_service", "ir_remote",
                                     "mqtt_service", "mqtt_client", "ota_service", "local_ota",
                                     "http_ota",     "https",       "web_start",   "sys_event",
                                     "console"};
    const auto index = static_cast<unsigned>(task);
    return index < kCount ? names[index] : "unknown";
}
bool read(Task task, stack_model::Record& record) noexcept {
    const auto index = static_cast<unsigned>(task);
    if (index >= kCount)
        return false;
    auto& slot = slots()[index];
    if (slot.busy.test_and_set(std::memory_order_acquire))
        return false;
    record = slot.record;
    slot.busy.clear(std::memory_order_release);
    return true;
}
void sample(Task task, unsigned budget, bool force, bool finished) noexcept {
    if (xPortInIsrContext())
        return;
    const auto index = static_cast<unsigned>(task);
    if (index >= kCount)
        return;
    const auto seconds = static_cast<std::uint32_t>(esp_timer_get_time() / 1000000);
    stack_model::Record previous{};
    if (!read(task, previous))
        return;
    if (!force && previous.valid && seconds - previous.sampled_at < 10U)
        return;
    const auto free_bytes = static_cast<std::uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
    auto& slot = slots()[index];
    if (slot.busy.test_and_set(std::memory_order_acquire))
        return;
    slot.record.observe(budget, free_bytes, seconds, finished);
    slot.busy.clear(std::memory_order_release);
}
Summary summary() noexcept {
    Summary result{};
    for (unsigned i = 0; i < kCount; ++i) {
        stack_model::Record value{};
        if (!read(static_cast<Task>(i), value)) {
            result.complete = false;
            return result;
        }
        if (!value.valid)
            continue;
        ++result.measured;
        if (!result.record.valid || value.minimum < result.record.minimum) {
            result.task = static_cast<Task>(i);
            result.record = value;
        }
    }
    return result;
}
} // namespace stack_monitor
