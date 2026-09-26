#include "FeatureConfig.hpp"

#if LED_CONTROLLER_AT24C08_ENABLED

#include "DeviceKeyProvider.hpp"
#include "esp_efuse.h"
#include "esp_random.h"
#include "mbedtls/platform_util.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <cstddef>

namespace {
constexpr char kNamespace[] = "led_settings";
constexpr char kKeyEntry[] = "aes256_key";
constexpr char kGenerationEntry[] = "key_generation";
constexpr std::uint32_t kInitialGeneration = 1;
constexpr std::size_t kRandomAttempts = 3;
} // namespace

DeviceKeyProvider::~DeviceKeyProvider() noexcept {
    mbedtls_platform_zeroize(key_.data(), key_.size());
    generation_ = 0;
    ready_ = false;
}

esp_err_t DeviceKeyProvider::initialize() noexcept {
    if (ready_) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) {
        return result;
    }

    nvs_handle_t handle = 0;
    result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return result;
    }

    std::size_t stored_size = key_.size();
    result = nvs_get_blob(handle, kKeyEntry, key_.data(), &stored_size);
    if (result == ESP_OK) {
        if ((stored_size != key_.size()) || !keyIsUsable(key_)) {
            result = ESP_ERR_INVALID_SIZE;
        } else {
            result = nvs_get_u32(handle, kGenerationEntry, &generation_);
            if ((result == ESP_OK) && (generation_ == 0U)) {
                result = ESP_ERR_INVALID_VERSION;
            }
        }
    } else if (result == ESP_ERR_NVS_NOT_FOUND) {
        result = ESP_FAIL;
        for (std::size_t attempt = 0; attempt < kRandomAttempts; ++attempt) {
            esp_fill_random(key_.data(), key_.size());
            if (keyIsUsable(key_)) {
                result = ESP_OK;
                break;
            }
        }
        if (result == ESP_OK) {
            generation_ = kInitialGeneration;
            result = nvs_set_blob(handle, kKeyEntry, key_.data(), key_.size());
        }
        if (result == ESP_OK) {
            result = nvs_set_u32(handle, kGenerationEntry, generation_);
        }
        if (result == ESP_OK) {
            result = nvs_commit(handle);
        }
        created_ = result == ESP_OK;
    }

    nvs_close(handle);
    if (result != ESP_OK) {
        mbedtls_platform_zeroize(key_.data(), key_.size());
        generation_ = 0;
        created_ = false;
        return result;
    }

    flash_encryption_enabled_ = esp_efuse_is_flash_encryption_enabled();
    ready_ = true;
    return ESP_OK;
}

bool DeviceKeyProvider::isReady() const noexcept {
    return ready_;
}

const SecureSettingsRecord::Key& DeviceKeyProvider::key() const noexcept {
    return key_;
}

std::uint32_t DeviceKeyProvider::generation() const noexcept {
    return generation_;
}

bool DeviceKeyProvider::wasCreated() const noexcept {
    return created_;
}

bool DeviceKeyProvider::isProtectedByFlashEncryption() const noexcept {
    return flash_encryption_enabled_;
}

bool DeviceKeyProvider::keyIsUsable(const SecureSettingsRecord::Key& key) noexcept {
    std::uint8_t accumulator = 0;
    for (const std::uint8_t value : key) {
        accumulator = static_cast<std::uint8_t>(accumulator | value);
    }
    return accumulator != 0U;
}

#endif
