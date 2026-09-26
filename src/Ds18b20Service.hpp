#pragma once

#include "FeatureConfig.hpp"

#if LED_CONTROLLER_DS18B20_ENABLED

#include "TemperatureTelemetry.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "onewire_bus.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

class Ds18b20Service final {
  public:
    esp_err_t initialize() noexcept;
    void service(TickType_t now) noexcept;
    void requestScan() noexcept;

    esp_err_t assignRole(TemperatureRole role, std::size_t one_based_sensor_index) noexcept;
    esp_err_t clearRole(TemperatureRole role) noexcept;
    esp_err_t clearAssignments() noexcept;
    esp_err_t restoreAssignments(const TemperatureAssignments& assignments) noexcept;
    [[nodiscard]] TemperatureAssignments assignments() const noexcept;
    [[nodiscard]] bool tryGetTelemetry(TemperatureTelemetry& output) const noexcept;

  private:
    enum class State : std::uint8_t {
        idle,
        converting,
        reading,
    };

    esp_err_t createBus(TickType_t now) noexcept;
    void resetBus(esp_err_t error, TickType_t now) noexcept;
    esp_err_t scanBus(TickType_t now, bool explicit_scan) noexcept;
    esp_err_t beginConversion(TickType_t now) noexcept;
    void readNextSensor(TickType_t now) noexcept;
    esp_err_t readSensor(std::uint64_t rom_id, std::int32_t& temperature_millicelsius) noexcept;
    void refreshRolesLocked() noexcept;
    void finishMeasurementCycle(TickType_t now) noexcept;
    void publishBusState(esp_err_t error, bool ready, bool scan_pending) noexcept;

    mutable StaticSemaphore_t mutex_storage_{};
    mutable SemaphoreHandle_t mutex_{nullptr};
    onewire_bus_handle_t bus_{nullptr};
    TemperatureTelemetry telemetry_{};
    TemperatureAssignments assignments_{};
    std::atomic<bool> scan_requested_{false};
    TickType_t next_bus_attempt_at_{0};
    TickType_t next_scan_at_{0};
    TickType_t next_conversion_at_{0};
    TickType_t conversion_ready_at_{0};
    std::size_t read_index_{0};
    State state_{State::idle};
    bool initialized_{false};
    bool absence_reported_{false};
    bool fault_reported_{false};
};

#endif
