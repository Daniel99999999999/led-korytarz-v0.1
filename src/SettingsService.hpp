#pragma once

#include "FeatureConfig.hpp"

#if LED_CONTROLLER_AT24C08_ENABLED

#include "At24c08.hpp"
#include "DeviceKeyProvider.hpp"
#include "EncryptedSettingsStore.hpp"
#include "PersistentSettings.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

class MqttService;
class WifiService;
#if LED_CONTROLLER_DS18B20_ENABLED
class Ds18b20Service;
#endif

class SettingsService final {
  public:
    struct Status final {
        std::uint32_t active_sequence{0};
        std::uint32_t key_generation{0};
        std::size_t payload_size{0};
        std::int32_t ina_current_correction_ppm{0};
        std::uint8_t temperature_assignments{0};
        esp_err_t last_error{ESP_OK};
        bool initialized{false};
        bool key_ready{false};
        bool key_created{false};
        bool key_protected_by_flash_encryption{false};
        bool bus_attached{false};
        bool eeprom_present{false};
        bool record_found{false};
        bool record_applied{false};
        bool save_pending{false};
        bool save_in_progress{false};
    };

    SettingsService(WifiService& wifi_service, MqttService& mqtt_service) noexcept;

#if LED_CONTROLLER_DS18B20_ENABLED
    esp_err_t attachTemperatureService(Ds18b20Service* temperature_service) noexcept;
#endif

    esp_err_t initialize() noexcept;
    esp_err_t attachBus(i2c_master_bus_handle_t bus_handle, TickType_t now) noexcept;
    esp_err_t detachBus() noexcept;
    void service(TickType_t now) noexcept;

    void notifyConfigurationChanged() noexcept;
    void requestSave() noexcept;
    void requestReload() noexcept;
    esp_err_t requestClear() noexcept;
    esp_err_t setInaCurrentCorrectionPpm(std::int32_t correction_ppm) noexcept;

    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] bool isBusAttached() const noexcept;
    [[nodiscard]] bool takeBusRecoveryRequest() noexcept;
    [[nodiscard]] std::int32_t inaCurrentCorrectionPpm() const noexcept;
    [[nodiscard]] Status status() const noexcept;

  private:
    static constexpr std::uint32_t kAutomaticSaveDelayMs = 5'000;
    static constexpr std::uint32_t kAbsentRetryIntervalMs = 30'000;
    static constexpr std::uint32_t kFaultRetryIntervalMs = 2'000;
    static constexpr std::uint32_t kApplyRetryIntervalMs = 2'000;
    static constexpr std::uint32_t kRequestReload = 1U << 0U;

    void updateSavePendingStatus() noexcept;
    void markStorageError(esp_err_t error) noexcept;
    void requestBusRecoveryIfTimedOut(esp_err_t error) noexcept;
    void loadRecord(TickType_t now) noexcept;
    void applyLoadedRecord(TickType_t now) noexcept;
    void beginSave(TickType_t now) noexcept;
    void serviceSave(TickType_t now) noexcept;
    void finishSave(esp_err_t result, TickType_t now) noexcept;
    void scheduleSaveAt(TickType_t deadline) noexcept;
    [[nodiscard]] bool saveIsDue(TickType_t now) const noexcept;

    WifiService& wifi_service_;
    MqttService& mqtt_service_;
    DeviceKeyProvider key_provider_;
    At24c08 eeprom_;
    EncryptedSettingsStore store_;
    mutable StaticSemaphore_t mutex_storage_{};
    mutable SemaphoreHandle_t mutex_{nullptr};
    std::atomic<std::uint32_t> request_bits_{0};
    std::atomic<std::uint32_t> requested_revision_{0};
    std::atomic<TickType_t> save_due_at_{0};
    std::atomic<std::int32_t> ina_current_correction_ppm_{0};
    std::atomic<bool> bus_recovery_requested_{false};
    TemperatureAssignments temperature_assignments_cache_{};
    persistent_settings::Data loaded_settings_{};
    Status status_{};
    TickType_t next_storage_action_at_{0};
    TickType_t next_apply_attempt_at_{0};
    std::uint32_t saved_revision_{0};
    std::uint32_t saving_revision_{0};
    bool initialized_{false};
    bool bus_attached_{false};
    bool eeprom_present_{false};
    bool storage_loaded_{false};
    bool load_requested_{false};
    bool loaded_apply_pending_{false};
    bool absence_reported_{false};
#if LED_CONTROLLER_DS18B20_ENABLED
    Ds18b20Service* temperature_service_{nullptr};
#endif
};

#endif
