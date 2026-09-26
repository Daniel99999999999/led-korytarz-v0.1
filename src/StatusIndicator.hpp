#pragma once

#include <cstdint>

class StatusIndicator final {
  public:
    explicit StatusIndicator(std::uint32_t toggle_interval) noexcept;

    void reset(bool fault_latched, std::uint32_t now) noexcept;
    [[nodiscard]] bool update(std::uint32_t now) noexcept;
    void latchFault() noexcept;
    void clearFault(std::uint32_t now) noexcept;

    [[nodiscard]] bool isIlluminated() const noexcept;
    [[nodiscard]] bool isFaultLatched() const noexcept;

  private:
    std::uint32_t toggle_interval_;
    std::uint32_t last_toggle_{0};
    bool initialized_{false};
    bool fault_latched_{false};
    bool illuminated_{false};
};
