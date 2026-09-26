#pragma once

#include <cstdint>

class DutyTransition final {
  public:
    void reset(std::uint32_t duty, std::uint32_t now) noexcept;
    [[nodiscard]] bool retarget(std::uint32_t target_duty, std::uint32_t duration,
                                std::uint32_t now) noexcept;
    [[nodiscard]] bool update(std::uint32_t now) noexcept;

    [[nodiscard]] std::uint32_t currentDuty() const noexcept;
    [[nodiscard]] std::uint32_t targetDuty() const noexcept;
    [[nodiscard]] bool isActive() const noexcept;

  private:
    [[nodiscard]] std::uint32_t valueAt(std::uint32_t now) const noexcept;

    std::uint32_t start_duty_{0};
    std::uint32_t target_duty_{0};
    std::uint32_t current_duty_{0};
    std::uint32_t started_at_{0};
    std::uint32_t duration_{0};
    bool active_{false};
};
