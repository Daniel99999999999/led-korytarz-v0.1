#pragma once

#include "FeatureConfig.hpp"

#if LED_CONTROLLER_AT24C08_ENABLED

#include "SecureSettingsRecord.hpp"
#include "esp_err.h"

#include <cstddef>
#include <cstdint>

class SecureSettingsCodec final {
  public:
    static esp_err_t sealStaged(const SecureSettingsRecord::Key& key, std::uint32_t key_generation,
                                std::uint32_t sequence, const SecureSettingsRecord::Nonce& nonce,
                                const std::uint8_t* plaintext, std::size_t plaintext_size,
                                SecureSettingsRecord::Slot& slot) noexcept;

    static esp_err_t openCommitted(const SecureSettingsRecord::Key& key,
                                   const SecureSettingsRecord::Slot& slot,
                                   SecureSettingsRecord::Payload& plaintext,
                                   SecureSettingsRecord::Metadata& metadata) noexcept;
};

#endif
