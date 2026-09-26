#include "FeatureConfig.hpp"

#if LED_CONTROLLER_AT24C08_ENABLED

#include "SecureSettingsCodec.hpp"
#include "mbedtls/platform_util.h"
#include "psa/crypto.h"

#include <algorithm>
#include <array>

namespace {
constexpr std::size_t kCombinedCiphertextSize =
    SecureSettingsRecord::kMaxPayloadSize + SecureSettingsRecord::kTagSize;

[[nodiscard]] esp_err_t mapPsaStatus(const psa_status_t status) noexcept {
    switch (status) {
    case PSA_SUCCESS:
        return ESP_OK;
    case PSA_ERROR_INVALID_ARGUMENT:
        return ESP_ERR_INVALID_ARG;
    case PSA_ERROR_BUFFER_TOO_SMALL:
        return ESP_ERR_INVALID_SIZE;
    case PSA_ERROR_NOT_SUPPORTED:
        return ESP_ERR_NOT_SUPPORTED;
    case PSA_ERROR_INSUFFICIENT_MEMORY:
        return ESP_ERR_NO_MEM;
    case PSA_ERROR_INVALID_SIGNATURE:
        return ESP_ERR_INVALID_CRC;
    default:
        return ESP_FAIL;
    }
}

esp_err_t importVolatileKey(const SecureSettingsRecord::Key& key,
                            mbedtls_svc_key_id_t& key_id) noexcept {
    const psa_status_t init_status = psa_crypto_init();
    if (init_status != PSA_SUCCESS) {
        return mapPsaStatus(init_status);
    }

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, static_cast<std::size_t>(SecureSettingsRecord::kKeySize * 8U));
    const psa_status_t import_status = psa_import_key(&attributes, key.data(), key.size(), &key_id);
    psa_reset_key_attributes(&attributes);
    return mapPsaStatus(import_status);
}

esp_err_t destroyVolatileKey(const mbedtls_svc_key_id_t key_id,
                             const esp_err_t operation_result) noexcept {
    const esp_err_t destroy_result = mapPsaStatus(psa_destroy_key(key_id));
    return (operation_result != ESP_OK) ? operation_result : destroy_result;
}
} // namespace

esp_err_t SecureSettingsCodec::sealStaged(const SecureSettingsRecord::Key& key,
                                          const std::uint32_t key_generation,
                                          const std::uint32_t sequence,
                                          const SecureSettingsRecord::Nonce& nonce,
                                          const std::uint8_t* const plaintext,
                                          const std::size_t plaintext_size,
                                          SecureSettingsRecord::Slot& slot) noexcept {
    if (((plaintext == nullptr) && (plaintext_size != 0U)) ||
        (plaintext_size > SecureSettingsRecord::kMaxPayloadSize)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!SecureSettingsRecord::prepareStaged(slot, sequence, key_generation, plaintext_size,
                                             nonce)) {
        return ESP_ERR_INVALID_SIZE;
    }

    mbedtls_svc_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    esp_err_t result = importVolatileKey(key, key_id);
    if (result != ESP_OK) {
        mbedtls_platform_zeroize(slot.data(), slot.size());
        return result;
    }

    std::array<std::uint8_t, kCombinedCiphertextSize> combined{};
    std::size_t combined_size = 0;
    const psa_status_t crypto_status = psa_aead_encrypt(
        key_id, PSA_ALG_GCM, nonce.data(), nonce.size(), SecureSettingsRecord::aadData(slot),
        SecureSettingsRecord::aadSize(), plaintext, plaintext_size, combined.data(),
        combined.size(), &combined_size);
    result = mapPsaStatus(crypto_status);
    result = destroyVolatileKey(key_id, result);

    const std::size_t expected_size = plaintext_size + SecureSettingsRecord::kTagSize;
    if ((result == ESP_OK) && (combined_size == expected_size)) {
        std::copy_n(combined.begin(), plaintext_size, SecureSettingsRecord::ciphertextData(slot));
        std::copy_n(combined.begin() + plaintext_size, SecureSettingsRecord::kTagSize,
                    SecureSettingsRecord::tagData(slot));
    } else {
        mbedtls_platform_zeroize(slot.data(), slot.size());
        if (result == ESP_OK) {
            result = ESP_ERR_INVALID_SIZE;
        }
    }

    mbedtls_platform_zeroize(combined.data(), combined.size());
    return result;
}

esp_err_t SecureSettingsCodec::openCommitted(const SecureSettingsRecord::Key& key,
                                             const SecureSettingsRecord::Slot& slot,
                                             SecureSettingsRecord::Payload& plaintext,
                                             SecureSettingsRecord::Metadata& metadata) noexcept {
    SecureSettingsRecord::Metadata candidate{};
    if (!SecureSettingsRecord::inspect(slot, true, candidate)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    mbedtls_svc_key_id_t key_id = MBEDTLS_SVC_KEY_ID_INIT;
    esp_err_t result = importVolatileKey(key, key_id);
    if (result != ESP_OK) {
        return result;
    }

    std::array<std::uint8_t, kCombinedCiphertextSize> combined{};
    std::copy_n(SecureSettingsRecord::ciphertextData(slot), candidate.payload_size,
                combined.begin());
    std::copy_n(SecureSettingsRecord::tagData(slot), SecureSettingsRecord::kTagSize,
                combined.begin() + candidate.payload_size);

    std::size_t plaintext_size = 0;
    const std::size_t combined_size = candidate.payload_size + SecureSettingsRecord::kTagSize;
    const psa_status_t crypto_status = psa_aead_decrypt(
        key_id, PSA_ALG_GCM, candidate.nonce.data(), candidate.nonce.size(),
        SecureSettingsRecord::aadData(slot), SecureSettingsRecord::aadSize(), combined.data(),
        combined_size, plaintext.data(), plaintext.size(), &plaintext_size);
    result = mapPsaStatus(crypto_status);
    result = destroyVolatileKey(key_id, result);
    mbedtls_platform_zeroize(combined.data(), combined.size());

    if ((result != ESP_OK) || (plaintext_size != candidate.payload_size)) {
        mbedtls_platform_zeroize(plaintext.data(), plaintext.size());
        return (result == ESP_OK) ? ESP_ERR_INVALID_SIZE : result;
    }

    metadata = candidate;
    return ESP_OK;
}

#endif
