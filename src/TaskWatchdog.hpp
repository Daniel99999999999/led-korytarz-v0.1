#pragma once

#include "esp_err.h"

#include <cstdint>

class TaskWatchdog final {
  public:
    [[nodiscard]] static esp_err_t initialize(std::uint32_t timeout_ms) noexcept;
    [[nodiscard]] static esp_err_t subscribeCurrentTask() noexcept;
    [[nodiscard]] static esp_err_t feed() noexcept;
};
