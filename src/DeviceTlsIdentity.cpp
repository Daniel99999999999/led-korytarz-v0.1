#include "DeviceTlsIdentity.hpp"

#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "nvs.h"
#include "psa/crypto.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace {
constexpr char kNamespace[] = "webtls";
constexpr char kCertKey[] = "cert";
constexpr char kPrivateKey[] = "key";
constexpr char kDnsName[] = "sterownik-led.local";
constexpr char kTag[] = "web_tls";
} // namespace

esp_err_t DeviceTlsIdentity::initialize() noexcept {
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (result != ESP_OK)
        return result;
    certificate_length_ = certificate_.size();
    private_key_length_ = private_key_.size();
    const esp_err_t cert_result =
        nvs_get_blob(handle, kCertKey, certificate_.data(), &certificate_length_);
    const esp_err_t key_result =
        nvs_get_blob(handle, kPrivateKey, private_key_.data(), &private_key_length_);
    if (cert_result == ESP_ERR_NVS_NOT_FOUND && key_result == ESP_ERR_NVS_NOT_FOUND) {
        result = generate();
        if (result == ESP_OK)
            result = nvs_set_blob(handle, kCertKey, certificate_.data(), certificate_length_);
        if (result == ESP_OK)
            result = nvs_set_blob(handle, kPrivateKey, private_key_.data(), private_key_length_);
        if (result == ESP_OK)
            result = nvs_commit(handle);
    } else if (cert_result != ESP_OK || key_result != ESP_OK || certificate_length_ < 100U ||
               private_key_length_ < 100U || certificate_.at(certificate_length_ - 1U) != 0U ||
               private_key_.at(private_key_length_ - 1U) != 0U) {
        result = ESP_ERR_INVALID_STATE;
    }
    nvs_close(handle);
    if (result != ESP_OK)
        return result;
    return printFingerprint();
}

esp_err_t DeviceTlsIdentity::generate() noexcept {
    if (psa_crypto_init() != PSA_SUCCESS)
        return ESP_FAIL;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t key_id = 0;
    const psa_status_t key_result = psa_generate_key(&attributes, &key_id);
    psa_reset_key_attributes(&attributes);
    if (key_result != PSA_SUCCESS) {
        ESP_LOGE(kTag, "Generowanie klucza TLS nie powiodlo sie: %ld",
                 static_cast<long>(key_result));
        return ESP_FAIL;
    }

    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    mbedtls_x509write_cert cert;
    mbedtls_x509write_crt_init(&cert);
    int result = mbedtls_pk_wrap_psa(&key, key_id);
    if (result == 0) {
        std::array<std::uint8_t, 16> serial{};
        esp_fill_random(serial.data(), serial.size());
        serial[0] &= 0x7FU;
        serial[0] |= 0x01U;
        mbedtls_x509write_crt_set_version(&cert, MBEDTLS_X509_CRT_VERSION_3);
        mbedtls_x509write_crt_set_subject_key(&cert, &key);
        mbedtls_x509write_crt_set_issuer_key(&cert, &key);
        mbedtls_x509write_crt_set_md_alg(&cert, MBEDTLS_MD_SHA256);
        result = mbedtls_x509write_crt_set_serial_raw(&cert, serial.data(), serial.size());
        if (result == 0)
            result = mbedtls_x509write_crt_set_subject_name(
                &cert, "CN=sterownik-led.local,O=Daniel Parzniewski");
        if (result == 0)
            result = mbedtls_x509write_crt_set_issuer_name(
                &cert, "CN=sterownik-led.local,O=Daniel Parzniewski");
        if (result == 0)
            result = mbedtls_x509write_crt_set_validity(&cert, "20200101000000", "20450101000000");
        if (result == 0)
            result = mbedtls_x509write_crt_set_basic_constraints(&cert, 0, -1);
        if (result == 0)
            result = mbedtls_x509write_crt_set_key_usage(&cert, MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
        mbedtls_x509_san_list san{};
        san.node.type = MBEDTLS_X509_SAN_DNS_NAME;
        san.node.san.unstructured_name.p =
            reinterpret_cast<unsigned char*>(const_cast<char*>(kDnsName));
        san.node.san.unstructured_name.len = sizeof(kDnsName) - 1U;
        if (result == 0)
            result = mbedtls_x509write_crt_set_subject_alternative_name(&cert, &san);
        if (result == 0)
            result = mbedtls_x509write_crt_pem(&cert, certificate_.data(), certificate_.size());
        if (result == 0)
            result = mbedtls_pk_write_key_pem(&key, private_key_.data(), private_key_.size());
        if (result == 0) {
            certificate_length_ =
                std::strlen(reinterpret_cast<const char*>(certificate_.data())) + 1U;
            private_key_length_ =
                std::strlen(reinterpret_cast<const char*>(private_key_.data())) + 1U;
        }
    }
    mbedtls_x509write_crt_free(&cert);
    mbedtls_pk_free(&key);
    psa_destroy_key(key_id);
    if (result != 0)
        ESP_LOGE(kTag, "Generowanie certyfikatu nie powiodlo sie: %d", result);
    return result == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t DeviceTlsIdentity::printFingerprint() const noexcept {
    if (psa_crypto_init() != PSA_SUCCESS)
        return ESP_FAIL;
    mbedtls_x509_crt parsed;
    mbedtls_x509_crt_init(&parsed);
    const int result = mbedtls_x509_crt_parse(&parsed, certificate_.data(), certificate_length_);
    if (result != 0) {
        mbedtls_x509_crt_free(&parsed);
        return ESP_ERR_INVALID_CRC;
    }
    std::array<std::uint8_t, 32> digest{};
    std::size_t digest_size = 0;
    const psa_status_t hash_result = psa_hash_compute(PSA_ALG_SHA_256, parsed.raw.p, parsed.raw.len,
                                                      digest.data(), digest.size(), &digest_size);
    mbedtls_x509_crt_free(&parsed);
    if (hash_result != PSA_SUCCESS || digest_size != digest.size())
        return ESP_FAIL;
    std::printf("HTTPS: odcisk SHA-256 certyfikatu: ");
    for (std::size_t index = 0; index < digest.size(); ++index)
        std::printf("%s%02X", index == 0U ? "" : ":", digest[index]);
    std::printf("\n");
    return ESP_OK;
}
