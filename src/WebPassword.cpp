#include "WebPassword.hpp"

#include "esp_random.h"
#include "mbedtls/platform_util.h"
#include "psa/crypto.h"

#include <array>
#include <cstring>

namespace web_password {
namespace {
constexpr std::uint32_t kPbkdf2Iterations = 20'000;

std::size_t boundedLength(const char* text) noexcept {
    std::size_t length = 0;
    while (length <= kMaximumLength && text[length] != '\0')
        ++length;
    return length;
}

esp_err_t derive(const char* password, const persistent_settings::WebCredential& credential,
                 std::array<std::uint8_t, 32>& output) noexcept {
    if (psa_crypto_init() != PSA_SUCCESS)
        return ESP_FAIL;
    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t result =
        psa_key_derivation_setup(&operation, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    if (result == PSA_SUCCESS)
        result = psa_key_derivation_input_integer(&operation, PSA_KEY_DERIVATION_INPUT_COST,
                                                  kPbkdf2Iterations);
    if (result == PSA_SUCCESS)
        result = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT,
                                                credential.salt.data(), credential.salt.size());
    if (result == PSA_SUCCESS)
        result = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_PASSWORD,
                                                reinterpret_cast<const std::uint8_t*>(password),
                                                std::strlen(password));
    if (result == PSA_SUCCESS)
        result = psa_key_derivation_output_bytes(&operation, output.data(), output.size());
    psa_key_derivation_abort(&operation);
    return result == PSA_SUCCESS ? ESP_OK : ESP_FAIL;
}
} // namespace

bool validNewPassword(const char* password) noexcept {
    if (!password)
        return false;
    const std::size_t length = boundedLength(password);
    if (length < kMinimumLength || length > kMaximumLength || std::strcmp(password, "admin") == 0)
        return false;
    for (std::size_t index = 0; index < length; ++index) {
        const auto character = static_cast<unsigned char>(password[index]);
        if (character < 0x21U || character > 0x7EU)
            return false;
    }
    return true;
}

esp_err_t create(const char* password, persistent_settings::WebCredential& output) noexcept {
    if (!validNewPassword(password))
        return ESP_ERR_INVALID_ARG;
    persistent_settings::WebCredential candidate{};
    esp_fill_random(candidate.salt.data(), candidate.salt.size());
    const esp_err_t result = derive(password, candidate, candidate.verifier);
    if (result == ESP_OK) {
        candidate.configured = true;
        output = candidate;
    }
    mbedtls_platform_zeroize(&candidate, sizeof(candidate));
    return result;
}

esp_err_t verify(const char* password, const persistent_settings::WebCredential& stored,
                 bool& matches) noexcept {
    matches = false;
    if (!password)
        return ESP_ERR_INVALID_ARG;
    if (!stored.configured) {
        matches = std::strcmp(password, "admin") == 0;
        return ESP_OK;
    }
    if (boundedLength(password) > kMaximumLength)
        return ESP_ERR_INVALID_ARG;
    std::array<std::uint8_t, 32> candidate{};
    const esp_err_t result = derive(password, stored, candidate);
    if (result == ESP_OK) {
        unsigned difference = 0;
        for (std::size_t index = 0; index < candidate.size(); ++index)
            difference |= static_cast<unsigned>(candidate[index] ^ stored.verifier[index]);
        matches = difference == 0U;
    }
    mbedtls_platform_zeroize(candidate.data(), candidate.size());
    return result;
}
} // namespace web_password
