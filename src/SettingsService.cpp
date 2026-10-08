#include "FeatureConfig.hpp"

#if LED_CONTROLLER_AT24C08_ENABLED

#include "MqttService.hpp"
#include "SettingsService.hpp"
#include "WebPassword.hpp"
#include "WifiService.hpp"
#if LED_CONTROLLER_DS18B20_ENABLED
#include "Ds18b20Service.hpp"
#endif
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/platform_util.h"

#include <type_traits>

namespace {
constexpr char kTag[] = "settings";

class MutexGuard final {
  public:
    explicit MutexGuard(const SemaphoreHandle_t mutex) noexcept : mutex_(mutex) {
        locked_ = (mutex_ != nullptr) && (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE);
    }

    ~MutexGuard() {
        if (locked_) {
            static_cast<void>(xSemaphoreGive(mutex_));
        }
    }

    [[nodiscard]] bool locked() const noexcept {
        return locked_;
    }

  private:
    SemaphoreHandle_t mutex_{nullptr};
    bool locked_{false};
};

[[nodiscard]] bool deadlineReached(const TickType_t now, const TickType_t deadline) noexcept {
    using SignedTick = std::make_signed_t<TickType_t>;
    return static_cast<SignedTick>(now - deadline) >= 0;
}

void secureZero(void* const data, const std::size_t size) noexcept {
    if ((data != nullptr) && (size > 0U)) {
        mbedtls_platform_zeroize(data, size);
    }
}
} // namespace

SettingsService::SettingsService(WifiService& wifi_service, MqttService& mqtt_service) noexcept
    : wifi_service_(wifi_service), mqtt_service_(mqtt_service), store_(eeprom_) {}

#if LED_CONTROLLER_DS18B20_ENABLED
esp_err_t
SettingsService::attachTemperatureService(Ds18b20Service* const temperature_service) noexcept {
    if ((temperature_service == nullptr) || (temperature_service_ != nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }
    temperature_service_ = temperature_service;
    return ESP_OK;
}
#endif

esp_err_t SettingsService::initialize() noexcept {
    if (initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    if (mutex_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t result = key_provider_.initialize();
    {
        MutexGuard lock(mutex_);
        if (!lock.locked()) {
            return ESP_FAIL;
        }
        status_.initialized = true;
        status_.key_ready = result == ESP_OK;
        status_.key_created = (result == ESP_OK) && key_provider_.wasCreated();
        status_.key_protected_by_flash_encryption =
            (result == ESP_OK) && key_provider_.isProtectedByFlashEncryption();
        status_.key_generation = (result == ESP_OK) ? key_provider_.generation() : 0U;
        status_.last_error = result;
    }
    initialized_ = true;

    if (result == ESP_OK) {
        ESP_LOGI(kTag, "Klucz AES-256 %s w NVS; Flash Encryption: %s",
                 key_provider_.wasCreated() ? "utworzony" : "odczytany",
                 key_provider_.isProtectedByFlashEncryption() ? "wlaczone" : "wylaczone");
    }
    return result;
}

esp_err_t SettingsService::attachBus(const i2c_master_bus_handle_t bus_handle,
                                     const TickType_t now) noexcept {
    if (!initialized_ || !key_provider_.isReady() || bus_attached_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (bus_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    store_.resetState();
    const esp_err_t result = eeprom_.initialize(bus_handle);
    if (result != ESP_OK) {
        markStorageError(result);
        return result;
    }

    bus_attached_ = true;
    eeprom_present_ = false;
    storage_loaded_ = false;
    load_requested_ = false;
    loaded_apply_pending_ = false;
    next_storage_action_at_ = now;
    absence_reported_ = false;
    {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.bus_attached = true;
            status_.eeprom_present = false;
            status_.record_found = false;
            status_.record_applied = false;
            status_.save_in_progress = false;
            status_.storage_ready = false;
        }
    }
    return ESP_OK;
}

esp_err_t SettingsService::detachBus() noexcept {
    if (!bus_attached_) {
        return ESP_OK;
    }

    const esp_err_t result = eeprom_.reset();
    store_.resetState();
    bus_attached_ = false;
    eeprom_present_ = false;
    storage_loaded_ = false;
    load_requested_ = false;
    loaded_apply_pending_ = false;
    bus_recovery_requested_.store(false, std::memory_order_release);
    secureZero(&loaded_settings_, sizeof(loaded_settings_));
    {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            secureZero(&web_credential_, sizeof(web_credential_));
            secureZero(&web_saving_credential_, sizeof(web_saving_credential_));
            secureZero(&web_active_credential_, sizeof(web_active_credential_));
            status_.bus_attached = false;
            status_.eeprom_present = false;
            status_.record_applied = false;
            status_.save_in_progress = false;
            status_.storage_ready = false;
            status_.web_password_set = false;
            if (result != ESP_OK) {
                status_.last_error = result;
            }
        }
    }
    return result;
}

void SettingsService::service(const TickType_t now) noexcept {
    if (!initialized_ || !key_provider_.isReady() || !bus_attached_) {
        return;
    }

    if ((request_bits_.fetch_and(~kRequestReload, std::memory_order_acq_rel) & kRequestReload) !=
        0U) {
        if (store_.isSaveInProgress()) {
            request_bits_.fetch_or(kRequestReload, std::memory_order_release);
        } else {
            load_requested_ = true;
            loaded_apply_pending_ = false;
            saved_revision_ = requested_revision_.load(std::memory_order_acquire);
            updateSavePendingStatus();
        }
    }

    if (store_.isSaveInProgress()) {
        serviceSave(now);
        return;
    }

    if (eeprom_present_ && load_requested_) {
        loadRecord(now);
    }

    if (!eeprom_present_ && deadlineReached(now, next_storage_action_at_)) {
        bool present = false;
        const esp_err_t result = eeprom_.probe(present);
        if (result != ESP_OK) {
            requestBusRecoveryIfTimedOut(result);
            markStorageError(result);
            next_storage_action_at_ = now + pdMS_TO_TICKS(kFaultRetryIntervalMs);
            return;
        }
        if (!present) {
            if (!absence_reported_) {
                ESP_LOGI(kTag, "AT24C08N nieobecny; ponowienie co %u s",
                         static_cast<unsigned>(kAbsentRetryIntervalMs / 1'000U));
                absence_reported_ = true;
            }
            next_storage_action_at_ = now + pdMS_TO_TICKS(kAbsentRetryIntervalMs);
            return;
        }

        eeprom_present_ = true;
        load_requested_ = true;
        absence_reported_ = false;
        {
            MutexGuard lock(mutex_);
            if (lock.locked()) {
                status_.eeprom_present = true;
                status_.last_error = ESP_OK;
            }
        }
        ESP_LOGI(kTag, "AT24C08N wykryty pod adresami 0x50-0x53");
        loadRecord(now);
    }

    if (loaded_apply_pending_ && deadlineReached(now, next_apply_attempt_at_)) {
        applyLoadedRecord(now);
    }
    if (eeprom_present_ && storage_loaded_ && saveIsDue(now)) {
        beginSave(now);
    }
}

void SettingsService::notifyConfigurationChanged() noexcept {
#if LED_CONTROLLER_DS18B20_ENABLED
    if (temperature_service_ != nullptr) {
        temperature_assignments_cache_ = temperature_service_->assignments();
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.temperature_assignments = static_cast<std::uint8_t>(
                (temperature_assignments_cache_.rom_ids[0] != 0U ? 1U : 0U) +
                (temperature_assignments_cache_.rom_ids[1] != 0U ? 1U : 0U));
        }
    }
#endif
    requested_revision_.fetch_add(1U, std::memory_order_acq_rel);
    scheduleSaveAt(xTaskGetTickCount() + pdMS_TO_TICKS(kAutomaticSaveDelayMs));
    updateSavePendingStatus();
}

void SettingsService::requestSave() noexcept {
    requested_revision_.fetch_add(1U, std::memory_order_acq_rel);
    scheduleSaveAt(xTaskGetTickCount());
    updateSavePendingStatus();
}

void SettingsService::requestReload() noexcept {
    request_bits_.fetch_or(kRequestReload, std::memory_order_release);
}

esp_err_t SettingsService::requestClear() noexcept {
    esp_err_t result = wifi_service_.clear();
    const esp_err_t mqtt_result = mqtt_service_.clear();
    if (result == ESP_OK) {
        result = mqtt_result;
    }
    ina_current_correction_ppm_.store(0, std::memory_order_release);
    temperature_assignments_cache_ = {};
#if LED_CONTROLLER_DS18B20_ENABLED
    if (temperature_service_ != nullptr) {
        const esp_err_t temperature_result = temperature_service_->clearAssignments();
        if (result == ESP_OK) {
            result = temperature_result;
        }
    }
#endif
    {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            web_credential_ = {};
            status_.ina_current_correction_ppm = 0;
            status_.temperature_assignments = 0;
            status_.record_applied = false;
        }
    }
    requestSave();
    return result;
}

esp_err_t SettingsService::setInaCurrentCorrectionPpm(const std::int32_t correction_ppm) noexcept {
    if ((correction_ppm < persistent_settings::kMinimumInaCurrentCorrectionPpm) ||
        (correction_ppm > persistent_settings::kMaximumInaCurrentCorrectionPpm)) {
        return ESP_ERR_INVALID_ARG;
    }
    ina_current_correction_ppm_.store(correction_ppm, std::memory_order_release);
    {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.ina_current_correction_ppm = correction_ppm;
        }
    }
    notifyConfigurationChanged();
    return ESP_OK;
}

esp_err_t SettingsService::setWebPassword(const char* password) noexcept {
    persistent_settings::WebCredential candidate{};
    esp_err_t result = web_password::create(password, candidate);
    if (result != ESP_OK)
        return result;
    persistent_settings::Data preview{};
    SecureSettingsRecord::Payload encoded{};
    std::size_t encoded_size = 0;
    result = wifi_service_.exportPersistentConfig(preview.wifi);
    if (result == ESP_OK)
        result = mqtt_service_.exportPersistentConfig(preview.mqtt);
    if (result == ESP_OK) {
        preview.ina_current_correction_ppm = inaCurrentCorrectionPpm();
#if LED_CONTROLLER_DS18B20_ENABLED
        if (temperature_service_ != nullptr)
            preview.temperature_assignments = temperature_service_->assignments();
#endif
        preview.web = candidate;
        const auto codec =
            persistent_settings::encode(preview, encoded.data(), encoded.size(), encoded_size);
        if (codec != persistent_settings::CodecResult::ok)
            result = codec == persistent_settings::CodecResult::buffer_too_small
                         ? ESP_ERR_INVALID_SIZE
                         : ESP_ERR_INVALID_ARG;
    }
    secureZero(&preview, sizeof(preview));
    secureZero(encoded.data(), encoded.size());
    if (result != ESP_OK) {
        secureZero(&candidate, sizeof(candidate));
        return result;
    }
    {
        MutexGuard lock(mutex_);
        if (!lock.locked() || !status_.storage_ready || !status_.eeprom_present ||
            !status_.key_ready) {
            secureZero(&candidate, sizeof(candidate));
            return ESP_ERR_INVALID_STATE;
        }
        web_credential_ = candidate;
    }
    secureZero(&candidate, sizeof(candidate));
    requestSave();
    return ESP_OK;
}

esp_err_t
SettingsService::readWebCredential(persistent_settings::WebCredential& output) const noexcept {
    MutexGuard lock(mutex_);
    if (!lock.locked() || !status_.storage_ready || !status_.eeprom_present || !status_.key_ready)
        return ESP_ERR_INVALID_STATE;
    output = web_active_credential_;
    return ESP_OK;
}

bool SettingsService::isInitialized() const noexcept {
    return initialized_ && key_provider_.isReady();
}

bool SettingsService::isBusAttached() const noexcept {
    return bus_attached_;
}

bool SettingsService::takeBusRecoveryRequest() noexcept {
    return bus_recovery_requested_.exchange(false, std::memory_order_acq_rel);
}

std::int32_t SettingsService::inaCurrentCorrectionPpm() const noexcept {
    return ina_current_correction_ppm_.load(std::memory_order_acquire);
}

SettingsService::Status SettingsService::status() const noexcept {
    MutexGuard lock(mutex_);
    return lock.locked() ? status_ : Status{};
}

void SettingsService::updateSavePendingStatus() noexcept {
    MutexGuard lock(mutex_);
    if (lock.locked()) {
        status_.save_pending =
            requested_revision_.load(std::memory_order_acquire) != saved_revision_;
    }
}

void SettingsService::markStorageError(const esp_err_t error) noexcept {
    MutexGuard lock(mutex_);
    if (lock.locked()) {
        status_.last_error = error;
    }
}

void SettingsService::requestBusRecoveryIfTimedOut(const esp_err_t error) noexcept {
    if (error == ESP_ERR_TIMEOUT) {
        bus_recovery_requested_.store(true, std::memory_order_release);
    }
}

void SettingsService::loadRecord(const TickType_t now) noexcept {
    SecureSettingsRecord::Payload plaintext{};
    std::size_t plaintext_size = 0;
    bool found = false;
    const esp_err_t result = store_.load(key_provider_.key(), key_provider_.generation(), plaintext,
                                         plaintext_size, found);
    load_requested_ = false;
    if (result != ESP_OK) {
        requestBusRecoveryIfTimedOut(result);
        secureZero(plaintext.data(), plaintext.size());
        storage_loaded_ = (result == ESP_ERR_INVALID_CRC) || (result == ESP_ERR_INVALID_VERSION);
        markStorageError(result);
        {
            MutexGuard lock(mutex_);
            if (lock.locked())
                status_.storage_ready = false;
        }
        if (!storage_loaded_) {
            eeprom_present_ = false;
            store_.resetState();
            next_storage_action_at_ = now + pdMS_TO_TICKS(kFaultRetryIntervalMs);
            MutexGuard lock(mutex_);
            if (lock.locked()) {
                status_.eeprom_present = false;
            }
        } else {
            ESP_LOGE(kTag, "Rekord AT24C08N odrzucony: %s", esp_err_to_name(result));
        }
        return;
    }

    storage_loaded_ = true;
    persistent_settings::Data decoded{};
    if (found) {
        const persistent_settings::CodecResult decode_result =
            persistent_settings::decode(plaintext.data(), plaintext_size, decoded);
        if (decode_result != persistent_settings::CodecResult::ok) {
            secureZero(plaintext.data(), plaintext.size());
            markStorageError(ESP_ERR_INVALID_RESPONSE);
            MutexGuard lock(mutex_);
            if (lock.locked())
                status_.storage_ready = false;
            ESP_LOGE(kTag, "Payload ustawien odrzucony: %s",
                     persistent_settings::resultName(decode_result));
            return;
        }
    }
    secureZero(plaintext.data(), plaintext.size());

    const bool unsaved_runtime_changes =
        requested_revision_.load(std::memory_order_acquire) != saved_revision_;
    if (!found && !unsaved_runtime_changes) {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            web_credential_ = {};
            web_active_credential_ = {};
            status_.web_password_set = false;
        }
    }

    if (found) {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            if (!unsaved_runtime_changes || !web_credential_.configured)
                web_credential_ = decoded.web;
            web_active_credential_ = decoded.web;
            status_.web_password_set = decoded.web.configured;
        }
    }

    if (found && !unsaved_runtime_changes) {
        secureZero(&loaded_settings_, sizeof(loaded_settings_));
        loaded_settings_ = decoded;
        ina_current_correction_ppm_.store(decoded.ina_current_correction_ppm,
                                          std::memory_order_release);
        temperature_assignments_cache_ = decoded.temperature_assignments;
#if LED_CONTROLLER_DS18B20_ENABLED
        if (temperature_service_ != nullptr) {
            const esp_err_t temperature_result =
                temperature_service_->restoreAssignments(temperature_assignments_cache_);
            if (temperature_result != ESP_OK) {
                secureZero(&decoded, sizeof(decoded));
                markStorageError(temperature_result);
                return;
            }
        }
#endif
        loaded_apply_pending_ = true;
        next_apply_attempt_at_ = now;
    }
    secureZero(&decoded, sizeof(decoded));

    {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.record_found = found;
            status_.storage_ready = true;
            status_.record_applied = false;
            status_.active_sequence = store_.activeSequence();
            status_.payload_size = plaintext_size;
            status_.ina_current_correction_ppm = inaCurrentCorrectionPpm();
            status_.temperature_assignments = static_cast<std::uint8_t>(
                (temperature_assignments_cache_.rom_ids[0] != 0U ? 1U : 0U) +
                (temperature_assignments_cache_.rom_ids[1] != 0U ? 1U : 0U));
            status_.last_error = ESP_OK;
        }
    }
    if (found) {
        ESP_LOGI(kTag, "Odczytano zaszyfrowany rekord nr %lu (%u B)%s",
                 static_cast<unsigned long>(store_.activeSequence()),
                 static_cast<unsigned>(plaintext_size),
                 unsaved_runtime_changes ? "; zachowano nowsza konfiguracje RAM" : "");
    } else {
        ESP_LOGI(kTag, "AT24C08N nie zawiera jeszcze poprawnego rekordu ustawien");
    }
}

void SettingsService::applyLoadedRecord(const TickType_t now) noexcept {
    WifiService::Status wifi_status{};
    MqttService::Status mqtt_status{};
    if (!wifi_service_.tryGetStatus(wifi_status) || !mqtt_service_.tryGetStatus(mqtt_status)) {
        next_apply_attempt_at_ = now + pdMS_TO_TICKS(kApplyRetryIntervalMs);
        return;
    }
    if (!wifi_status.started || !mqtt_status.started) {
        next_apply_attempt_at_ = now + pdMS_TO_TICKS(kApplyRetryIntervalMs);
        return;
    }

    ESP_LOGI(kTag, "Rekord EEPROM: Wi-Fi configured=%s, MQTT configured=%s",
             loaded_settings_.wifi.configured ? "tak" : "nie",
             loaded_settings_.mqtt.configured ? "tak" : "nie");
    const char* stage = "Wi-Fi restore/clear";
    esp_err_t result = ESP_OK;
    if (loaded_settings_.wifi.configured) {
        result = wifi_service_.restorePersistentConfig(loaded_settings_.wifi);
        if (result == ESP_OK) {
            stage = "Wi-Fi apply";
            result = wifi_service_.apply();
        }
    } else {
        result = wifi_service_.clear();
    }

    if (result == ESP_OK) {
        stage = "MQTT restore/clear";
        if (loaded_settings_.mqtt.configured) {
            result = mqtt_service_.restorePersistentConfig(loaded_settings_.mqtt);
            if (result == ESP_OK) {
                stage = "MQTT apply";
                result = mqtt_service_.apply();
            }
        } else {
            result = mqtt_service_.clear();
        }
    }

    if (result != ESP_OK) {
        ESP_LOGE(kTag, "Odtwarzanie EEPROM: etap=%s, wynik=%s", stage, esp_err_to_name(result));
        markStorageError(result);
        next_apply_attempt_at_ = now + pdMS_TO_TICKS(kApplyRetryIntervalMs);
        return;
    }

    loaded_apply_pending_ = false;
    secureZero(&loaded_settings_, sizeof(loaded_settings_));
    {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.record_applied = true;
            status_.last_error = ESP_OK;
        }
    }
    ESP_LOGI(kTag, "Konfiguracja Wi-Fi/MQTT przywrocona z AT24C08N");
}

void SettingsService::beginSave(const TickType_t now) noexcept {
    persistent_settings::Data settings{};
    esp_err_t result = wifi_service_.exportPersistentConfig(settings.wifi);
    if (result == ESP_OK) {
        result = mqtt_service_.exportPersistentConfig(settings.mqtt);
    }
    settings.ina_current_correction_ppm = inaCurrentCorrectionPpm();
#if LED_CONTROLLER_DS18B20_ENABLED
    if (temperature_service_ != nullptr) {
        temperature_assignments_cache_ = temperature_service_->assignments();
    }
#endif
    settings.temperature_assignments = temperature_assignments_cache_;
    bool credential_copied = false;
    {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            settings.web = web_credential_;
            web_saving_credential_ = web_credential_;
            credential_copied = true;
        }
    }
    if (!credential_copied && result == ESP_OK)
        result = ESP_FAIL;
    if (result != ESP_OK) {
        secureZero(&settings, sizeof(settings));
        markStorageError(result);
        scheduleSaveAt(now + pdMS_TO_TICKS(kFaultRetryIntervalMs));
        return;
    }

    SecureSettingsRecord::Payload plaintext{};
    std::size_t plaintext_size = 0;
    const persistent_settings::CodecResult encode_result =
        persistent_settings::encode(settings, plaintext.data(), plaintext.size(), plaintext_size);
    secureZero(&settings, sizeof(settings));
    const std::uint32_t revision = requested_revision_.load(std::memory_order_acquire);
    if (encode_result != persistent_settings::CodecResult::ok) {
        secureZero(plaintext.data(), plaintext.size());
        saved_revision_ = revision;
        markStorageError(encode_result == persistent_settings::CodecResult::buffer_too_small
                             ? ESP_ERR_INVALID_SIZE
                             : ESP_ERR_INVALID_ARG);
        updateSavePendingStatus();
        ESP_LOGE(kTag, "Nie zapisano ustawien: %s (limit %u B)",
                 persistent_settings::resultName(encode_result),
                 static_cast<unsigned>(plaintext.size()));
        return;
    }

    SecureSettingsRecord::Nonce nonce{};
    esp_fill_random(nonce.data(), nonce.size());
    result = store_.beginSave(key_provider_.key(), key_provider_.generation(), nonce,
                              plaintext.data(), plaintext_size);
    secureZero(nonce.data(), nonce.size());
    secureZero(plaintext.data(), plaintext.size());
    if (result != ESP_OK) {
        markStorageError(result);
        scheduleSaveAt(now + pdMS_TO_TICKS(kFaultRetryIntervalMs));
        return;
    }

    saving_revision_ = revision;
    {
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.payload_size = plaintext_size;
            status_.save_in_progress = true;
            status_.last_error = ESP_OK;
        }
    }
}

void SettingsService::serviceSave(const TickType_t now) noexcept {
    const esp_err_t result = store_.serviceSave();
    if ((result != ESP_OK) || store_.isSaveFinished()) {
        finishSave((result == ESP_OK) ? store_.saveResult() : result, now);
    }
}

void SettingsService::finishSave(const esp_err_t result, const TickType_t now) noexcept {
    if (result == ESP_OK) {
        saved_revision_ = saving_revision_;
        {
            MutexGuard lock(mutex_);
            if (lock.locked()) {
                web_active_credential_ = web_saving_credential_;
                status_.web_password_set = web_active_credential_.configured;
                status_.active_sequence = store_.activeSequence();
                status_.record_found = true;
                status_.record_applied = true;
                status_.save_in_progress = false;
                status_.last_error = ESP_OK;
            }
        }
        ESP_LOGI(kTag, "Zapis ustawien zakonczony; rekord nr %lu",
                 static_cast<unsigned long>(store_.activeSequence()));
    } else {
        markStorageError(result);
        {
            MutexGuard lock(mutex_);
            if (lock.locked()) {
                status_.save_in_progress = false;
            }
        }
        scheduleSaveAt(now + pdMS_TO_TICKS(kFaultRetryIntervalMs));
        ESP_LOGW(kTag, "Zapis AT24C08N nie powiodl sie: %s", esp_err_to_name(result));
    }

    const esp_err_t reset_result = store_.resetSave();
    if (reset_result != ESP_OK) {
        store_.resetState();
    }
    if (result != ESP_OK) {
        eeprom_present_ = false;
        storage_loaded_ = false;
        load_requested_ = false;
        next_storage_action_at_ = now + pdMS_TO_TICKS(kFaultRetryIntervalMs);
        MutexGuard lock(mutex_);
        if (lock.locked()) {
            status_.eeprom_present = false;
            status_.storage_ready = false;
        }
    }
    updateSavePendingStatus();
}

void SettingsService::scheduleSaveAt(const TickType_t deadline) noexcept {
    save_due_at_.store(deadline, std::memory_order_release);
}

bool SettingsService::saveIsDue(const TickType_t now) const noexcept {
    if (requested_revision_.load(std::memory_order_acquire) == saved_revision_) {
        return false;
    }
    return deadlineReached(now, save_due_at_.load(std::memory_order_acquire));
}

#endif
