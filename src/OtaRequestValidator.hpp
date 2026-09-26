#pragma once

#include <cstddef>
#include <cstdint>

namespace ota_request {
inline constexpr std::size_t kMaxUrlLength = 511U;
inline constexpr char kHttpsPrefix[] = "https://";

enum class ValidationResult : std::uint8_t {
    ok,
    empty,
    too_long,
    insecure_scheme,
    missing_host,
    credentials_not_allowed,
    invalid_character,
};

[[nodiscard]] inline ValidationResult validate(const char* const value,
                                               const std::size_t length) noexcept {
    if ((value == nullptr) || (length == 0U)) {
        return ValidationResult::empty;
    }
    if (length > kMaxUrlLength) {
        return ValidationResult::too_long;
    }

    constexpr std::size_t kPrefixLength = sizeof(kHttpsPrefix) - 1U;
    if (length <= kPrefixLength) {
        return ValidationResult::missing_host;
    }
    for (std::size_t index = 0; index < kPrefixLength; ++index) {
        if (value[index] != kHttpsPrefix[index]) {
            return ValidationResult::insecure_scheme;
        }
    }

    std::size_t authority_end = length;
    for (std::size_t index = kPrefixLength; index < length; ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if ((character < 0x21U) || (character > 0x7EU) || (character == '\\') ||
            (character == '#')) {
            return ValidationResult::invalid_character;
        }
        if (((value[index] == '/') || (value[index] == '?')) && (authority_end == length)) {
            authority_end = index;
        }
    }
    if (authority_end == kPrefixLength) {
        return ValidationResult::missing_host;
    }
    for (std::size_t index = kPrefixLength; index < authority_end; ++index) {
        if (value[index] == '@') {
            return ValidationResult::credentials_not_allowed;
        }
    }
    return ValidationResult::ok;
}

[[nodiscard]] inline const char* resultName(const ValidationResult result) noexcept {
    switch (result) {
    case ValidationResult::ok:
        return "ok";
    case ValidationResult::empty:
        return "pusty adres";
    case ValidationResult::too_long:
        return "adres za dlugi";
    case ValidationResult::insecure_scheme:
        return "wymagany https://";
    case ValidationResult::missing_host:
        return "brak hosta";
    case ValidationResult::credentials_not_allowed:
        return "dane logowania w URL sa zabronione";
    case ValidationResult::invalid_character:
        return "niedozwolony znak";
    }
    return "nieznany blad";
}
} // namespace ota_request
