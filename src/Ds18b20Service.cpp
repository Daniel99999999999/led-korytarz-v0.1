#include "FeatureConfig.hpp"

#if LED_CONTROLLER_DS18B20_ENABLED

#include "BoardConfig.hpp"
#include "Ds18b20Service.hpp"
#include "TemperatureModel.hpp"
#include "esp_log.h"
#include "onewire_bus_impl_rmt.h"
#include "onewire_cmd.h"
#include "onewire_device.h"

#include <algorithm>
#include <array>
#include <type_traits>

namespace {
constexpr char kTag[] = "ds18b20";
constexpr std::uint8_t kConvertTemperatureCommand = 0x44;
constexpr std::uint8_t kReadScratchpadCommand = 0xBE;

class MutexGuard final {
  public:
    MutexGuard(const SemaphoreHandle_t mutex, const TickType_t wait) noexcept : mutex_(mutex) {
        locked_ = (mutex_ != nullptr) && (xSemaphoreTake(mutex_, wait) == pdTRUE);
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

[[nodiscard]] constexpr std::size_t roleIndex(const TemperatureRole role) noexcept {
    return static_cast<std::size_t>(role);
}
} // namespace

esp_err_t Ds18b20Service::initialize() noexcept {
    if (initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    if (mutex_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    initialized_ = true;
    const TickType_t now = xTaskGetTickCount();
    const esp_err_t result = createBus(now);
    if (result != ESP_OK) {
        next_bus_attempt_at_ = now + pdMS_TO_TICKS(board_config::kDs18b20FaultRetryIntervalMs);
    }
    return result;
}

void Ds18b20Service::service(const TickType_t now) noexcept {
    if (!initialized_) {
        return;
    }
    if ((bus_ == nullptr) && deadlineReached(now, next_bus_attempt_at_)) {
        const esp_err_t result = createBus(now);
        if (result != ESP_OK) {
            next_bus_attempt_at_ = now + pdMS_TO_TICKS(board_config::kDs18b20FaultRetryIntervalMs);
            return;
        }
    }
    if (bus_ == nullptr) {
        return;
    }

    const bool explicit_scan = scan_requested_.exchange(false, std::memory_order_acq_rel);
    if ((state_ == State::idle) && (explicit_scan || deadlineReached(now, next_scan_at_))) {
        const esp_err_t result = scanBus(now, explicit_scan);
        if ((result != ESP_OK) && (result != ESP_ERR_NOT_FOUND)) {
            resetBus(result, now);
            return;
        }
    } else if (explicit_scan) {
        scan_requested_.store(true, std::memory_order_release);
    }

    if ((state_ == State::idle) && deadlineReached(now, next_conversion_at_)) {
        const esp_err_t result = beginConversion(now);
        if (result == ESP_ERR_INVALID_STATE) {
            next_conversion_at_ = now + pdMS_TO_TICKS(board_config::kDs18b20SampleIntervalMs);
        } else if (result == ESP_ERR_NOT_FOUND) {
            scan_requested_.store(true, std::memory_order_release);
            next_conversion_at_ = now + pdMS_TO_TICKS(board_config::kDs18b20SampleIntervalMs);
        } else if (result != ESP_OK) {
            resetBus(result, now);
        }
    } else if ((state_ == State::converting) && deadlineReached(now, conversion_ready_at_)) {
        state_ = State::reading;
        read_index_ = 0U;
    } else if (state_ == State::reading) {
        readNextSensor(now);
    }
}

void Ds18b20Service::requestScan() noexcept {
    scan_requested_.store(true, std::memory_order_release);
    MutexGuard lock(mutex_, portMAX_DELAY);
    if (lock.locked()) {
        telemetry_.scan_pending = true;
    }
}

esp_err_t Ds18b20Service::assignRole(const TemperatureRole role,
                                     const std::size_t one_based_sensor_index) noexcept {
    if ((role >= TemperatureRole::count) || (one_based_sensor_index == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    MutexGuard lock(mutex_, portMAX_DELAY);
    if (!lock.locked()) {
        return ESP_FAIL;
    }
    if (one_based_sensor_index > telemetry_.sensor_count) {
        return ESP_ERR_NOT_FOUND;
    }

    const std::uint64_t rom_id = telemetry_.sensors[one_based_sensor_index - 1U].rom_id;
    const std::size_t target = roleIndex(role);
    for (std::size_t index = 0; index < assignments_.rom_ids.size(); ++index) {
        if ((index != target) && (assignments_.rom_ids[index] == rom_id)) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (assignments_.rom_ids[target] != rom_id) {
        assignments_.rom_ids[target] = rom_id;
        refreshRolesLocked();
        ++telemetry_.revision;
    }
    return ESP_OK;
}

esp_err_t Ds18b20Service::clearRole(const TemperatureRole role) noexcept {
    if (role >= TemperatureRole::count) {
        return ESP_ERR_INVALID_ARG;
    }
    MutexGuard lock(mutex_, portMAX_DELAY);
    if (!lock.locked()) {
        return ESP_FAIL;
    }
    const std::size_t index = roleIndex(role);
    if (assignments_.rom_ids[index] != 0U) {
        assignments_.rom_ids[index] = 0U;
        refreshRolesLocked();
        ++telemetry_.revision;
    }
    return ESP_OK;
}

esp_err_t Ds18b20Service::clearAssignments() noexcept {
    MutexGuard lock(mutex_, portMAX_DELAY);
    if (!lock.locked()) {
        return ESP_FAIL;
    }
    if ((assignments_.rom_ids[0] != 0U) || (assignments_.rom_ids[1] != 0U)) {
        assignments_ = {};
        refreshRolesLocked();
        ++telemetry_.revision;
    }
    return ESP_OK;
}

esp_err_t Ds18b20Service::restoreAssignments(const TemperatureAssignments& assignments) noexcept {
    if (!temperature_model::assignmentsAreValid(assignments)) {
        return ESP_ERR_INVALID_ARG;
    }
    MutexGuard lock(mutex_, portMAX_DELAY);
    if (!lock.locked()) {
        return ESP_FAIL;
    }
    if (assignments_.rom_ids != assignments.rom_ids) {
        assignments_ = assignments;
        refreshRolesLocked();
        ++telemetry_.revision;
    }
    return ESP_OK;
}

TemperatureAssignments Ds18b20Service::assignments() const noexcept {
    MutexGuard lock(mutex_, portMAX_DELAY);
    return lock.locked() ? assignments_ : TemperatureAssignments{};
}

bool Ds18b20Service::tryGetTelemetry(TemperatureTelemetry& output) const noexcept {
    MutexGuard lock(mutex_, 0);
    if (!lock.locked()) {
        return false;
    }
    output = telemetry_;
    return true;
}

esp_err_t Ds18b20Service::createBus(const TickType_t now) noexcept {
    if (bus_ != nullptr) {
        return ESP_OK;
    }
    const onewire_bus_config_t bus_config{
        .bus_gpio_num = static_cast<int>(board_config::kDs18b20DataPin),
        .flags = {.en_pull_up = false},
    };
    const onewire_bus_rmt_config_t rmt_config{
        .max_rx_bytes = static_cast<std::uint32_t>(temperature_model::kScratchpadSize),
    };
    const esp_err_t result = onewire_new_bus_rmt(&bus_config, &rmt_config, &bus_);
    if (result != ESP_OK) {
        bus_ = nullptr;
        publishBusState(result, false, false);
        if (!fault_reported_) {
            ESP_LOGW(kTag, "Nie udalo sie uruchomic 1-Wire na GPIO4: %s", esp_err_to_name(result));
            fault_reported_ = true;
        }
        return result;
    }

    state_ = State::idle;
    next_scan_at_ = now;
    next_conversion_at_ = now;
    publishBusState(ESP_OK, true, true);
    fault_reported_ = false;
    ESP_LOGI(kTag, "1-Wire gotowe na GPIO4; pull-up zewnetrzny 4,7 kOhm, tryb 3-przewodowy");
    return ESP_OK;
}

void Ds18b20Service::resetBus(const esp_err_t error, const TickType_t now) noexcept {
    if (bus_ != nullptr) {
        const esp_err_t delete_result = onewire_bus_del(bus_);
        if ((delete_result != ESP_OK) && !fault_reported_) {
            ESP_LOGW(kTag, "Zwolnienie magistrali 1-Wire: %s", esp_err_to_name(delete_result));
        }
        bus_ = nullptr;
    }
    state_ = State::idle;
    next_bus_attempt_at_ = now + pdMS_TO_TICKS(board_config::kDs18b20FaultRetryIntervalMs);
    publishBusState(error, false, false);
    if (!fault_reported_) {
        ESP_LOGW(kTag, "Blad magistrali 1-Wire: %s; ponowienie za %u s", esp_err_to_name(error),
                 static_cast<unsigned>(board_config::kDs18b20FaultRetryIntervalMs / 1'000U));
        fault_reported_ = true;
    }
}

esp_err_t Ds18b20Service::scanBus(const TickType_t now, const bool explicit_scan) noexcept {
    {
        MutexGuard lock(mutex_, portMAX_DELAY);
        if (lock.locked()) {
            telemetry_.scan_pending = true;
        }
    }

    onewire_device_iter_handle_t iterator = nullptr;
    esp_err_t result = onewire_new_device_iter(bus_, &iterator);
    if (result != ESP_OK) {
        publishBusState(result, true, false);
        return result;
    }

    std::array<std::uint64_t, board_config::kDs18b20MaximumDeviceCount> found{};
    std::size_t found_count = 0U;
    bool overflow = false;
    for (;;) {
        onewire_device_t device{};
        result = onewire_device_iter_get_next(iterator, &device);
        if (result != ESP_OK) {
            break;
        }
        if (!temperature_model::romIsValid(device.address)) {
            continue;
        }
        if (found_count < found.size()) {
            found[found_count++] = device.address;
        } else {
            overflow = true;
        }
    }
    const esp_err_t delete_result = onewire_del_device_iter(iterator);
    if ((result == ESP_ERR_NOT_FOUND) && (delete_result == ESP_OK)) {
        result = ESP_OK;
    } else if ((result == ESP_ERR_NOT_FOUND) && (delete_result != ESP_OK)) {
        result = delete_result;
    }
    if (result != ESP_OK) {
        publishBusState(result, true, false);
        return result;
    }

    std::sort(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(found_count));
    bool topology_changed = false;
    {
        MutexGuard lock(mutex_, portMAX_DELAY);
        if (!lock.locked()) {
            return ESP_FAIL;
        }
        const auto previous = telemetry_.sensors;
        const std::uint8_t previous_count = telemetry_.sensor_count;
        topology_changed = previous_count != found_count;
        for (std::size_t index = 0; !topology_changed && (index < found_count); ++index) {
            topology_changed = previous[index].rom_id != found[index];
        }
        telemetry_.sensors = {};
        telemetry_.sensor_count = static_cast<std::uint8_t>(found_count);
        for (std::size_t index = 0; index < found_count; ++index) {
            TemperatureSensorTelemetry sensor{};
            sensor.rom_id = found[index];
            sensor.present = true;
            const auto match = std::find_if(
                previous.begin(), previous.begin() + previous_count,
                [rom_id = found[index]](const TemperatureSensorTelemetry& candidate) noexcept {
                    return candidate.rom_id == rom_id;
                });
            if (match != (previous.begin() + previous_count)) {
                sensor.temperature_millicelsius = match->temperature_millicelsius;
                sensor.last_error = match->last_error;
                sensor.valid = match->valid;
            }
            telemetry_.sensors[index] = sensor;
        }
        telemetry_.last_error = overflow ? ESP_ERR_INVALID_SIZE : ESP_OK;
        telemetry_.scan_pending = false;
        refreshRolesLocked();
        ++telemetry_.revision;
    }

    next_scan_at_ = now + pdMS_TO_TICKS(board_config::kDs18b20RescanIntervalMs);
    next_conversion_at_ = now;
    if (found_count == 0U) {
        if (explicit_scan || topology_changed || !absence_reported_) {
            ESP_LOGI(kTag, "Brak czujnikow DS18B20; ponowny skan za %u s",
                     static_cast<unsigned>(board_config::kDs18b20RescanIntervalMs / 1'000U));
        }
        absence_reported_ = true;
        return ESP_ERR_NOT_FOUND;
    }

    absence_reported_ = false;
    fault_reported_ = false;
    if (explicit_scan || topology_changed) {
        ESP_LOGI(kTag, "Liczba wykrytych czujnikow DS18B20: %u%s",
                 static_cast<unsigned>(found_count), overflow ? "; pozostale pominieto" : "");
    }
    return ESP_OK;
}

esp_err_t Ds18b20Service::beginConversion(const TickType_t now) noexcept {
    std::uint8_t sensor_count = 0U;
    {
        MutexGuard lock(mutex_, portMAX_DELAY);
        if (!lock.locked()) {
            return ESP_FAIL;
        }
        sensor_count = telemetry_.sensor_count;
    }
    if (sensor_count == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = onewire_bus_reset(bus_);
    if (result != ESP_OK) {
        return result;
    }
    const std::array<std::uint8_t, 2> command{{ONEWIRE_CMD_SKIP_ROM, kConvertTemperatureCommand}};
    result =
        onewire_bus_write_bytes(bus_, command.data(), static_cast<std::uint8_t>(command.size()));
    if (result == ESP_OK) {
        state_ = State::converting;
        conversion_ready_at_ = now + pdMS_TO_TICKS(board_config::kDs18b20ConversionTimeMs);
        next_conversion_at_ = now + pdMS_TO_TICKS(board_config::kDs18b20SampleIntervalMs);
    }
    return result;
}

void Ds18b20Service::readNextSensor(const TickType_t now) noexcept {
    std::uint64_t rom_id = 0U;
    bool cycle_finished = false;
    {
        MutexGuard lock(mutex_, portMAX_DELAY);
        if (!lock.locked()) {
            return;
        }
        if (read_index_ >= telemetry_.sensor_count) {
            cycle_finished = true;
        } else {
            rom_id = telemetry_.sensors[read_index_].rom_id;
        }
    }
    if (cycle_finished) {
        finishMeasurementCycle(now);
        return;
    }

    std::int32_t temperature_millicelsius = 0;
    const esp_err_t result = readSensor(rom_id, temperature_millicelsius);
    if (result == ESP_ERR_NOT_FOUND) {
        scan_requested_.store(true, std::memory_order_release);
    }
    {
        MutexGuard lock(mutex_, portMAX_DELAY);
        if (lock.locked() && (read_index_ < telemetry_.sensor_count) &&
            (telemetry_.sensors[read_index_].rom_id == rom_id)) {
            TemperatureSensorTelemetry& sensor = telemetry_.sensors[read_index_];
            sensor.temperature_millicelsius = temperature_millicelsius;
            sensor.last_error = result;
            sensor.present = result != ESP_ERR_NOT_FOUND;
            sensor.valid = result == ESP_OK;
        }
    }
    ++read_index_;
    {
        MutexGuard lock(mutex_, portMAX_DELAY);
        if (lock.locked()) {
            cycle_finished = read_index_ >= telemetry_.sensor_count;
        }
    }
    if (cycle_finished) {
        finishMeasurementCycle(now);
    }
}

esp_err_t Ds18b20Service::readSensor(const std::uint64_t rom_id,
                                     std::int32_t& temperature_millicelsius) noexcept {
    esp_err_t result = onewire_bus_reset(bus_);
    if (result != ESP_OK) {
        return result;
    }

    std::array<std::uint8_t, 10> command{};
    command[0] = ONEWIRE_CMD_MATCH_ROM;
    std::array<std::uint8_t, temperature_model::kRomSize> rom_bytes{};
    temperature_model::romToBytes(rom_id, rom_bytes);
    std::copy(rom_bytes.begin(), rom_bytes.end(), command.begin() + 1U);
    command.back() = kReadScratchpadCommand;
    result =
        onewire_bus_write_bytes(bus_, command.data(), static_cast<std::uint8_t>(command.size()));
    if (result != ESP_OK) {
        return result;
    }

    std::array<std::uint8_t, temperature_model::kScratchpadSize> scratchpad{};
    result = onewire_bus_read_bytes(bus_, scratchpad.data(), scratchpad.size());
    if (result != ESP_OK) {
        return result;
    }
    return temperature_model::parseScratchpad(scratchpad.data(), scratchpad.size(),
                                              temperature_millicelsius)
               ? ESP_OK
               : ESP_ERR_INVALID_CRC;
}

void Ds18b20Service::refreshRolesLocked() noexcept {
    telemetry_.roles = {};
    for (std::size_t role = 0; role < assignments_.rom_ids.size(); ++role) {
        TemperatureRoleTelemetry& output = telemetry_.roles[role];
        output.rom_id = assignments_.rom_ids[role];
        output.configured = output.rom_id != 0U;
        if (!output.configured) {
            continue;
        }
        const auto match = std::find_if(
            telemetry_.sensors.begin(), telemetry_.sensors.begin() + telemetry_.sensor_count,
            [rom_id = output.rom_id](const TemperatureSensorTelemetry& sensor) noexcept {
                return sensor.rom_id == rom_id;
            });
        if (match != (telemetry_.sensors.begin() + telemetry_.sensor_count)) {
            output.temperature_millicelsius = match->temperature_millicelsius;
            output.last_error = match->last_error;
            output.present = match->present;
            output.valid = match->valid;
        } else {
            output.last_error = ESP_ERR_NOT_FOUND;
        }
    }
}

void Ds18b20Service::finishMeasurementCycle(const TickType_t now) noexcept {
    {
        MutexGuard lock(mutex_, portMAX_DELAY);
        if (lock.locked()) {
            refreshRolesLocked();
            ++telemetry_.revision;
        }
    }
    state_ = State::idle;
    read_index_ = 0U;
    if (deadlineReached(now, next_conversion_at_)) {
        next_conversion_at_ = now;
    }
}

void Ds18b20Service::publishBusState(const esp_err_t error, const bool ready,
                                     const bool scan_pending) noexcept {
    MutexGuard lock(mutex_, portMAX_DELAY);
    if (!lock.locked()) {
        return;
    }
    const bool changed = (telemetry_.last_error != error) || (telemetry_.bus_ready != ready) ||
                         (telemetry_.scan_pending != scan_pending);
    telemetry_.last_error = error;
    telemetry_.bus_ready = ready;
    telemetry_.scan_pending = scan_pending;
    if (!ready) {
        for (std::size_t index = 0; index < telemetry_.sensor_count; ++index) {
            telemetry_.sensors[index].present = false;
            telemetry_.sensors[index].valid = false;
            telemetry_.sensors[index].last_error = error;
        }
        refreshRolesLocked();
    }
    if (changed) {
        ++telemetry_.revision;
    }
}

#endif
