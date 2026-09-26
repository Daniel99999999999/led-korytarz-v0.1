#pragma once

#include "FeatureConfig.hpp"

#if LED_CONTROLLER_AT24C08_ENABLED

#include "At24c08.hpp"
#include "SecureSettingsRecord.hpp"
#include "esp_err.h"

#include <cstddef>
#include <cstdint>

class EncryptedSettingsStore final {
  public:
    explicit EncryptedSettingsStore(At24c08& eeprom) noexcept;
    ~EncryptedSettingsStore() noexcept;

    EncryptedSettingsStore(const EncryptedSettingsStore&) = delete;
    EncryptedSettingsStore& operator=(const EncryptedSettingsStore&) = delete;

    esp_err_t load(const SecureSettingsRecord::Key& key, std::uint32_t key_generation,
                   SecureSettingsRecord::Payload& plaintext, std::size_t& plaintext_size,
                   bool& found) noexcept;

    esp_err_t beginSave(const SecureSettingsRecord::Key& key, std::uint32_t key_generation,
                        const SecureSettingsRecord::Nonce& nonce, const std::uint8_t* plaintext,
                        std::size_t plaintext_size) noexcept;
    esp_err_t serviceSave() noexcept;
    esp_err_t resetSave() noexcept;
    void resetState() noexcept;

    [[nodiscard]] bool isSaveInProgress() const noexcept;
    [[nodiscard]] bool isSaveFinished() const noexcept;
    [[nodiscard]] esp_err_t saveResult() const noexcept;
    [[nodiscard]] bool hasActiveRecord() const noexcept;
    [[nodiscard]] std::uint32_t activeSequence() const noexcept;

  private:
    enum class SaveState : std::uint8_t {
        idle,
        invalidating,
        writing_pages,
        committing,
        verifying,
        complete,
        fault,
    };

    [[nodiscard]] static bool keyIsUsable(const SecureSettingsRecord::Key& key) noexcept;
    [[nodiscard]] static bool nonceIsUsable(const SecureSettingsRecord::Nonce& nonce) noexcept;
    [[nodiscard]] std::uint16_t targetBaseAddress() const noexcept;
    esp_err_t failSave(esp_err_t error) noexcept;
    void clearSensitiveState() noexcept;

    At24c08& eeprom_;
    SecureSettingsRecord::Slot pending_slot_{};
    SecureSettingsRecord::Key save_key_{};
    SaveState save_state_{SaveState::idle};
    esp_err_t save_result_{ESP_ERR_INVALID_STATE};
    std::uint32_t save_key_generation_{0};
    std::uint32_t save_sequence_{0};
    std::uint16_t save_payload_size_{0};
    std::size_t target_slot_{0};
    std::size_t write_offset_{0};
    std::size_t active_slot_{0};
    std::uint32_t active_sequence_{0};
    bool loaded_{false};
    bool has_active_slot_{false};
};

#endif
