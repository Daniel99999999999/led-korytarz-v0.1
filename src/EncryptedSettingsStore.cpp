#include "FeatureConfig.hpp"

#if LED_CONTROLLER_AT24C08_ENABLED

#include "BoardConfig.hpp"
#include "EncryptedSettingsStore.hpp"
#include "SecureSettingsCodec.hpp"
#include "mbedtls/platform_util.h"

#include <array>

EncryptedSettingsStore::EncryptedSettingsStore(At24c08& eeprom) noexcept : eeprom_(eeprom) {}

EncryptedSettingsStore::~EncryptedSettingsStore() noexcept {
    clearSensitiveState();
    mbedtls_platform_zeroize(pending_slot_.data(), pending_slot_.size());
}

esp_err_t EncryptedSettingsStore::load(const SecureSettingsRecord::Key& key,
                                       const std::uint32_t key_generation,
                                       SecureSettingsRecord::Payload& plaintext,
                                       std::size_t& plaintext_size, bool& found) noexcept {
    plaintext.fill(0);
    plaintext_size = 0;
    found = false;
    loaded_ = false;
    has_active_slot_ = false;
    active_slot_ = 0;
    active_sequence_ = 0;
    if (!eeprom_.isInitialized() || (save_state_ != SaveState::idle)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!keyIsUsable(key) || (key_generation == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    std::array<SecureSettingsRecord::Slot, SecureSettingsRecord::kSlotCount> slots{};
    std::array<SecureSettingsRecord::Metadata, SecureSettingsRecord::kSlotCount> metadata{};
    std::array<bool, SecureSettingsRecord::kSlotCount> matching_records{};
    bool committed_record_present = false;

    for (std::size_t index = 0; index < slots.size(); ++index) {
        const std::size_t numeric_address = index * SecureSettingsRecord::kSlotSize;
        const esp_err_t read_result = eeprom_.read(static_cast<std::uint16_t>(numeric_address),
                                                   slots[index].data(), slots[index].size());
        if (read_result != ESP_OK) {
            return read_result;
        }

        if (SecureSettingsRecord::inspect(slots[index], true, metadata[index])) {
            committed_record_present = true;
            matching_records[index] = metadata[index].key_generation == key_generation;
        }
    }

    if (!committed_record_present) {
        loaded_ = true;
        has_active_slot_ = false;
        active_slot_ = 0;
        active_sequence_ = 0;
        return ESP_OK;
    }
    if (!matching_records[0] && !matching_records[1]) {
        loaded_ = true;
        return ESP_ERR_INVALID_VERSION;
    }

    std::array<std::size_t, SecureSettingsRecord::kSlotCount> order{{0, 1}};
    if (matching_records[1] &&
        (!matching_records[0] ||
         SecureSettingsRecord::sequenceIsNewer(metadata[1].sequence, metadata[0].sequence))) {
        order = {{1, 0}};
    }

    bool authentication_failed = false;
    for (const std::size_t index : order) {
        if (!matching_records[index]) {
            continue;
        }

        SecureSettingsRecord::Metadata authenticated_metadata{};
        const esp_err_t open_result = SecureSettingsCodec::openCommitted(
            key, slots[index], plaintext, authenticated_metadata);
        if (open_result == ESP_OK) {
            loaded_ = true;
            has_active_slot_ = true;
            active_slot_ = index;
            active_sequence_ = authenticated_metadata.sequence;
            plaintext_size = authenticated_metadata.payload_size;
            found = true;
            return ESP_OK;
        }
        if (open_result == ESP_ERR_INVALID_CRC) {
            authentication_failed = true;
            continue;
        }
        loaded_ = true;
        return open_result;
    }

    loaded_ = true;
    return authentication_failed ? ESP_ERR_INVALID_CRC : ESP_ERR_INVALID_VERSION;
}

esp_err_t EncryptedSettingsStore::beginSave(const SecureSettingsRecord::Key& key,
                                            const std::uint32_t key_generation,
                                            const SecureSettingsRecord::Nonce& nonce,
                                            const std::uint8_t* const plaintext,
                                            const std::size_t plaintext_size) noexcept {
    if (!eeprom_.isInitialized() || !loaded_ || (save_state_ != SaveState::idle) ||
        eeprom_.isWritePending()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!keyIsUsable(key) || !nonceIsUsable(nonce) || (key_generation == 0U) ||
        (plaintext == nullptr) || (plaintext_size == 0U) ||
        (plaintext_size > SecureSettingsRecord::kMaxPayloadSize)) {
        return ESP_ERR_INVALID_ARG;
    }

    target_slot_ = has_active_slot_ ? (1U - active_slot_) : 0U;
    save_sequence_ = has_active_slot_ ? (active_sequence_ + 1U) : 1U;
    save_key_generation_ = key_generation;
    save_payload_size_ = static_cast<std::uint16_t>(plaintext_size);
    save_key_ = key;

    const esp_err_t result =
        SecureSettingsCodec::sealStaged(save_key_, save_key_generation_, save_sequence_, nonce,
                                        plaintext, plaintext_size, pending_slot_);
    if (result != ESP_OK) {
        clearSensitiveState();
        return result;
    }

    write_offset_ = 0;
    save_result_ = ESP_ERR_INVALID_STATE;
    save_state_ = SaveState::invalidating;
    return ESP_OK;
}

esp_err_t EncryptedSettingsStore::serviceSave() noexcept {
    if (!isSaveInProgress()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (eeprom_.isWritePending()) {
        bool complete = false;
        const esp_err_t poll_result = eeprom_.pollWriteComplete(complete);
        if (poll_result != ESP_OK) {
            return failSave(poll_result);
        }
        if (!complete) {
            return ESP_OK;
        }
    }

    switch (save_state_) {
    case SaveState::invalidating: {
        const std::uint8_t invalid_marker = SecureSettingsRecord::kInvalidMarker;
        const esp_err_t result = eeprom_.beginPageWrite(targetBaseAddress(), &invalid_marker, 1);
        if (result != ESP_OK) {
            return failSave(result);
        }
        save_state_ = SaveState::writing_pages;
        return ESP_OK;
    }

    case SaveState::writing_pages:
        if (write_offset_ < pending_slot_.size()) {
            const std::size_t remaining = pending_slot_.size() - write_offset_;
            const std::size_t write_size = (remaining < board_config::kSettingsEepromPageSize)
                                               ? remaining
                                               : board_config::kSettingsEepromPageSize;
            const std::uint16_t address = static_cast<std::uint16_t>(
                static_cast<std::size_t>(targetBaseAddress()) + write_offset_);
            const esp_err_t result =
                eeprom_.beginPageWrite(address, pending_slot_.data() + write_offset_, write_size);
            if (result != ESP_OK) {
                return failSave(result);
            }
            write_offset_ += write_size;
            return ESP_OK;
        }
        save_state_ = SaveState::committing;
        [[fallthrough]];

    case SaveState::committing: {
        const std::uint8_t committed_marker = SecureSettingsRecord::kCommittedMarker;
        const esp_err_t result = eeprom_.beginPageWrite(targetBaseAddress(), &committed_marker, 1);
        if (result != ESP_OK) {
            return failSave(result);
        }
        save_state_ = SaveState::verifying;
        return ESP_OK;
    }

    case SaveState::verifying: {
        SecureSettingsRecord::Slot readback{};
        const esp_err_t read_result =
            eeprom_.read(targetBaseAddress(), readback.data(), readback.size());
        if (read_result != ESP_OK) {
            return failSave(read_result);
        }

        SecureSettingsRecord::Payload verification_plaintext{};
        SecureSettingsRecord::Metadata verified_metadata{};
        const esp_err_t open_result = SecureSettingsCodec::openCommitted(
            save_key_, readback, verification_plaintext, verified_metadata);
        mbedtls_platform_zeroize(verification_plaintext.data(), verification_plaintext.size());
        if (open_result != ESP_OK) {
            return failSave(open_result);
        }
        if ((verified_metadata.sequence != save_sequence_) ||
            (verified_metadata.key_generation != save_key_generation_) ||
            (verified_metadata.payload_size != save_payload_size_)) {
            return failSave(ESP_ERR_INVALID_RESPONSE);
        }

        has_active_slot_ = true;
        active_slot_ = target_slot_;
        active_sequence_ = save_sequence_;
        save_result_ = ESP_OK;
        save_state_ = SaveState::complete;
        clearSensitiveState();
        return ESP_OK;
    }

    case SaveState::idle:
    case SaveState::complete:
    case SaveState::fault:
        return ESP_ERR_INVALID_STATE;
    }

    return ESP_ERR_INVALID_STATE;
}

esp_err_t EncryptedSettingsStore::resetSave() noexcept {
    if (isSaveInProgress() || eeprom_.isWritePending()) {
        return ESP_ERR_INVALID_STATE;
    }

    clearSensitiveState();
    mbedtls_platform_zeroize(pending_slot_.data(), pending_slot_.size());
    save_state_ = SaveState::idle;
    save_result_ = ESP_ERR_INVALID_STATE;
    target_slot_ = 0;
    write_offset_ = 0;
    return ESP_OK;
}

void EncryptedSettingsStore::resetState() noexcept {
    clearSensitiveState();
    mbedtls_platform_zeroize(pending_slot_.data(), pending_slot_.size());
    save_state_ = SaveState::idle;
    save_result_ = ESP_ERR_INVALID_STATE;
    target_slot_ = 0;
    write_offset_ = 0;
    active_slot_ = 0;
    active_sequence_ = 0;
    loaded_ = false;
    has_active_slot_ = false;
}

bool EncryptedSettingsStore::isSaveInProgress() const noexcept {
    return (save_state_ == SaveState::invalidating) || (save_state_ == SaveState::writing_pages) ||
           (save_state_ == SaveState::committing) || (save_state_ == SaveState::verifying);
}

bool EncryptedSettingsStore::isSaveFinished() const noexcept {
    return (save_state_ == SaveState::complete) || (save_state_ == SaveState::fault);
}

esp_err_t EncryptedSettingsStore::saveResult() const noexcept {
    return isSaveFinished() ? save_result_ : ESP_ERR_INVALID_STATE;
}

bool EncryptedSettingsStore::hasActiveRecord() const noexcept {
    return loaded_ && has_active_slot_;
}

std::uint32_t EncryptedSettingsStore::activeSequence() const noexcept {
    return hasActiveRecord() ? active_sequence_ : 0U;
}

bool EncryptedSettingsStore::keyIsUsable(const SecureSettingsRecord::Key& key) noexcept {
    std::uint8_t accumulator = 0;
    for (const std::uint8_t value : key) {
        accumulator = static_cast<std::uint8_t>(accumulator | value);
    }
    return accumulator != 0U;
}

bool EncryptedSettingsStore::nonceIsUsable(const SecureSettingsRecord::Nonce& nonce) noexcept {
    std::uint8_t accumulator = 0;
    for (const std::uint8_t value : nonce) {
        accumulator = static_cast<std::uint8_t>(accumulator | value);
    }
    return accumulator != 0U;
}

std::uint16_t EncryptedSettingsStore::targetBaseAddress() const noexcept {
    return static_cast<std::uint16_t>(target_slot_ * SecureSettingsRecord::kSlotSize);
}

esp_err_t EncryptedSettingsStore::failSave(const esp_err_t error) noexcept {
    save_result_ = error;
    save_state_ = SaveState::fault;
    clearSensitiveState();
    return error;
}

void EncryptedSettingsStore::clearSensitiveState() noexcept {
    mbedtls_platform_zeroize(save_key_.data(), save_key_.size());
    save_key_generation_ = 0;
    save_sequence_ = 0;
    save_payload_size_ = 0;
}

#endif
