#pragma once

#include "esp_err.h"

#include <array>
#include <cstddef>
#include <cstdint>

class DeviceTlsIdentity final {
  public:
    esp_err_t initialize() noexcept;
    const std::uint8_t* certificate() const noexcept {
        return certificate_.data();
    }
    std::size_t certificateLength() const noexcept {
        return certificate_length_;
    }
    const std::uint8_t* privateKey() const noexcept {
        return private_key_.data();
    }
    std::size_t privateKeyLength() const noexcept {
        return private_key_length_;
    }

  private:
    esp_err_t generate() noexcept;
    esp_err_t printFingerprint() const noexcept;

    std::array<std::uint8_t, 2048> certificate_{};
    std::array<std::uint8_t, 1024> private_key_{};
    std::size_t certificate_length_{0};
    std::size_t private_key_length_{0};
};
