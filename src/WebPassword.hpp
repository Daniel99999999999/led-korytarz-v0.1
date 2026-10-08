#pragma once

#include "PersistentSettings.hpp"
#include "esp_err.h"

#include <cstddef>

namespace web_password {
inline constexpr std::size_t kMinimumLength = 12;
inline constexpr std::size_t kMaximumLength = 64;

[[nodiscard]] bool validNewPassword(const char* password) noexcept;
esp_err_t create(const char* password, persistent_settings::WebCredential& output) noexcept;
esp_err_t verify(const char* password, const persistent_settings::WebCredential& stored,
                 bool& matches) noexcept;
} // namespace web_password
