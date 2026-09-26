#pragma once

#include "FeatureConfig.hpp"

#if LED_CONTROLLER_AT24C08_ENABLED

#include "SecureSettingsRecord.hpp"
#include "esp_err.h"

#include <cstdint>

class DeviceKeyProvider final {
  public:
    DeviceKeyProvider() noexcept = default;
    ~DeviceKeyProvider() noexcept;

    DeviceKeyProvider(const DeviceKeyProvider&) = delete;
    DeviceKeyProvider& operator=(const DeviceKeyProvider&) = delete;

    esp_err_t initialize() noexcept;

    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] const SecureSettingsRecord::Key& key() const noexcept;
    [[nodiscard]] std::uint32_t generation() const noexcept;
    [[nodiscard]] bool wasCreated() const noexcept;
    [[nodiscard]] bool isProtectedByFlashEncryption() const noexcept;

  private:
    [[nodiscard]] static bool keyIsUsable(const SecureSettingsRecord::Key& key) noexcept;

    SecureSettingsRecord::Key key_{};
    std::uint32_t generation_{0};
    bool ready_{false};
    bool created_{false};
    bool flash_encryption_enabled_{false};
};

#endif
