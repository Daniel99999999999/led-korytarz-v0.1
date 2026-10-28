#pragma once
#include <cstdint>
namespace stack_model {
enum class Level : unsigned { unknown, ok, warning, critical };
constexpr Level level(std::uint32_t free_bytes) noexcept {
    return free_bytes < 256U ? Level::critical : free_bytes < 512U ? Level::warning : Level::ok;
}
constexpr const char* name(Level value) noexcept {
    return value == Level::critical  ? "critical"
           : value == Level::warning ? "warning"
           : value == Level::ok      ? "ok"
                                     : "unknown";
}
struct Record {
    std::uint32_t budget{0}, minimum{0}, sampled_at{0}, minimum_at{0};
    bool valid{false}, finished{false};
    void observe(std::uint32_t bytes, std::uint32_t free_bytes, std::uint32_t seconds,
                 bool completed = false) noexcept {
        if (free_bytes > bytes || bytes == 0)
            return;
        if (!valid || free_bytes < minimum) {
            minimum = free_bytes;
            minimum_at = seconds;
        }
        budget = bytes;
        sampled_at = seconds;
        valid = true;
        finished = completed;
    }
    Level severity() const noexcept {
        return valid ? level(minimum) : Level::unknown;
    }
};
} // namespace stack_model
