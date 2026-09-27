#include "LcdService.hpp"

#include "BoardConfig.hpp"
#if LED_CONTROLLER_INA228_ENABLED
#include "Ina228Scaling.hpp"
#endif
#include "esp_log.h"

#include <algorithm>
#include <limits>
#include <type_traits>

namespace {
constexpr char kTag[] = "lcd_service";
static_assert(std::is_trivially_copyable_v<LcdService::Snapshot>,
              "Snapshot LCD musi nadawac sie do kopiowania przez kolejke FreeRTOS");

#if LED_CONTROLLER_INA228_ENABLED || LED_CONTROLLER_DS18B20_ENABLED
[[nodiscard]] bool deadlineReached(const TickType_t now, const TickType_t deadline) noexcept {
    using SignedTick = std::make_signed_t<TickType_t>;
    return static_cast<SignedTick>(now - deadline) >= 0;
}
#endif

#if LED_CONTROLLER_INA228_ENABLED && LED_CONTROLLER_AT24C08_ENABLED
[[nodiscard]] std::int32_t applySignedCorrection(const std::int32_t value,
                                                 const std::int32_t correction_ppm) noexcept {
    constexpr std::int64_t kScale = 1'000'000;
    const std::int64_t numerator = static_cast<std::int64_t>(value) * (kScale + correction_ppm);
    const std::int64_t corrected = (numerator >= 0) ? ((numerator + (kScale / 2)) / kScale)
                                                    : ((numerator - (kScale / 2)) / kScale);
    return static_cast<std::int32_t>(
        std::clamp(corrected, static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()),
                   static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())));
}

[[nodiscard]] std::uint32_t applyUnsignedCorrection(const std::uint32_t value,
                                                    const std::int32_t correction_ppm) noexcept {
    constexpr std::uint64_t kScale = 1'000'000;
    const std::uint64_t multiplier = static_cast<std::uint64_t>(1'000'000 + correction_ppm);
    const std::uint64_t corrected =
        (static_cast<std::uint64_t>(value) * multiplier + (kScale / 2U)) / kScale;
    return static_cast<std::uint32_t>(
        std::min(corrected, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())));
}
#endif
} // namespace

#if LED_CONTROLLER_DS1307_NVRAM_ENABLED
void LcdService::attachLocalController(LedController& controller) noexcept {
    local_controller_ = &controller;
}
#endif

esp_err_t LcdService::initialize() noexcept {
    if (initialized_ || (snapshot_queue_ != nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }

    snapshot_queue_ =
        xQueueCreateStatic(1, sizeof(Snapshot), queue_buffer_.data(), &queue_storage_);
    if (snapshot_queue_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
#if LED_CONTROLLER_INA228_ENABLED
    ina228_mutex_ = xSemaphoreCreateMutexStatic(&ina228_mutex_storage_);
    if (ina228_mutex_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
#endif

#if LED_CONTROLLER_DS18B20_ENABLED
    const esp_err_t temperature_result = temperature_service_.initialize();
    if ((temperature_result != ESP_OK) && (temperature_result != ESP_ERR_INVALID_STATE)) {
        ESP_LOGW(kTag, "DS18B20: inicjalizacja odroczona: %s", esp_err_to_name(temperature_result));
    }
#endif

    initialized_ = true;
    return ESP_OK;
}

esp_err_t LcdService::start() noexcept {
    if (!initialized_ || (task_handle_ != nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }

    const BaseType_t result =
        xTaskCreate(taskEntry, "lcd_service", kTaskStackSize, this, kTaskPriority, &task_handle_);
    return (result == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t LcdService::publish(const Snapshot& snapshot) noexcept {
    if ((snapshot_queue_ == nullptr) || (task_handle_ == nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }

    return (xQueueOverwrite(snapshot_queue_, &snapshot) == pdPASS) ? ESP_OK : ESP_FAIL;
}

esp_err_t LcdService::requestBacklight(const bool enabled) noexcept {
    if (task_handle_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    desired_backlight_.store(enabled, std::memory_order_release);
    return ESP_OK;
}

#if LED_CONTROLLER_AT24C08_ENABLED
esp_err_t LcdService::attachSettingsService(SettingsService* const settings_service) noexcept {
    if (initialized_ || (settings_service == nullptr) || (settings_service_ != nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }
#if LED_CONTROLLER_DS18B20_ENABLED
    const esp_err_t temperature_result =
        settings_service->attachTemperatureService(&temperature_service_);
    if (temperature_result != ESP_OK) {
        return temperature_result;
    }
#endif
    settings_service_ = settings_service;
    return ESP_OK;
}
#endif

#if LED_CONTROLLER_DS18B20_ENABLED
void LcdService::requestTemperatureScan() noexcept {
    temperature_service_.requestScan();
}

esp_err_t LcdService::assignTemperatureRole(const TemperatureRole role,
                                            const std::size_t one_based_sensor_index) noexcept {
    return temperature_service_.assignRole(role, one_based_sensor_index);
}

esp_err_t LcdService::clearTemperatureRole(const TemperatureRole role) noexcept {
    return temperature_service_.clearRole(role);
}

esp_err_t LcdService::clearTemperatureAssignments() noexcept {
    return temperature_service_.clearAssignments();
}

bool LcdService::tryGetTemperatureTelemetry(TemperatureTelemetry& output) const noexcept {
    return temperature_service_.tryGetTelemetry(output);
}
#endif

esp_err_t LcdService::currentFault() const noexcept {
    return static_cast<esp_err_t>(fault_code_.load(std::memory_order_acquire));
}

#if LED_CONTROLLER_INA228_ENABLED
bool LcdService::tryGetIna228Telemetry(Ina228Telemetry& output) const noexcept {
    if ((ina228_mutex_ == nullptr) || (xSemaphoreTake(ina228_mutex_, 0) != pdTRUE)) {
        return false;
    }
    output = ina228_telemetry_;
    static_cast<void>(xSemaphoreGive(ina228_mutex_));
    return true;
}
#endif

void LcdService::taskEntry(void* const context) noexcept {
    auto* const service = static_cast<LcdService*>(context);
    service->taskLoop();
}

[[noreturn]] void LcdService::taskLoop() noexcept {
    TickType_t next_wake = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(kTaskPeriodMs);
    const TickType_t recovery_interval = pdMS_TO_TICKS(board_config::kLcdRecoveryRetryIntervalMs);
    const TickType_t recovery_frame_interval =
        pdMS_TO_TICKS(board_config::kLcdRecoveryFrameIntervalMs);
    const TickType_t information_page_interval =
        pdMS_TO_TICKS(board_config::kLcdInformationPageIntervalMs);
    const TickType_t measurement_page_interval =
        pdMS_TO_TICKS(board_config::kLcdMeasurementPageIntervalMs);
    Snapshot latest_snapshot{};
    TickType_t bus_retry_started_at = next_wake;
    TickType_t display_retry_started_at = next_wake;
#if LED_CONTROLLER_AT24C08_ENABLED
    TickType_t settings_attach_retry_started_at = next_wake;
#endif
    TickType_t verification_started_at = next_wake;
    TickType_t verification_interval = pdMS_TO_TICKS(10'000);
    TickType_t recovery_frame_started_at = next_wake;
    TickType_t information_page_started_at = next_wake;
    TickType_t measurement_page_started_at = next_wake;
    std::uint8_t recovery_frames_written = 0;
    bool snapshot_available = false;
    bool snapshot_dirty = false;
    bool display_started = false;
    bool bus_retry_pending = false;
    bool display_retry_pending = false;
    bool display_recovery_active = false;
    bool display_absence_reported = false;
    bool recovery_frame_waiting = false;
#if LED_CONTROLLER_AT24C08_ENABLED
    bool settings_attach_retry_pending = false;
    bool settings_attach_fault_reported = false;
#endif
    bool desired_backlight_on = board_config::kLcdBacklightDefaultOn;
    LcdDashboard::InformationPage information_page = LcdDashboard::InformationPage::status;
    LcdDashboard::MeasurementPage measurement_page =
        LcdDashboard::MeasurementPage::radiator_temperature;

    const auto schedule_display_retry =
        [this, &display_started, &display_retry_pending, &display_retry_started_at,
         &display_recovery_active, &display_absence_reported, &recovery_frames_written,
         &recovery_frame_waiting, &snapshot_available, &snapshot_dirty](
            const char* const operation, const esp_err_t error, const TickType_t now) noexcept {
            const bool endpoint_unavailable =
                (error == ESP_ERR_NOT_FOUND) || (error == ESP_ERR_INVALID_RESPONSE);
            if (endpoint_unavailable) {
                reportBusRecovery();
                if (!display_absence_reported) {
                    ESP_LOGI(kTag,
                             "LCD: PCF8574AT niedostepny pod adresem 0x%02X; pozostale "
                             "urzadzenia I2C pozostaja aktywne; ponowienie co %u ms",
                             static_cast<unsigned>(board_config::kLcdI2cAddress),
                             static_cast<unsigned>(board_config::kLcdRecoveryRetryIntervalMs));
                    display_absence_reported = true;
                }
            } else if (error == ESP_ERR_TIMEOUT) {
                recoverSharedBusAfterTimeout(operation, error);
            } else {
                reportFault(operation, error);
            }

            const esp_err_t reset_result = display_.reset();
            if (reset_result != ESP_OK) {
                reportFault("zwolnienie adaptera I2C", reset_result);
            }
            display_started = false;
            display_retry_pending = true;
            display_retry_started_at = now;
            display_recovery_active = true;
            recovery_frames_written = 0;
            recovery_frame_waiting = false;
            snapshot_dirty = snapshot_available;
        };

    const auto start_display = [this, &display_started, &display_retry_pending,
                                &display_recovery_active, &display_absence_reported,
                                &snapshot_available, &snapshot_dirty,
                                &schedule_display_retry](const TickType_t now) noexcept {
        bool present = false;
        esp_err_t start_result = display_.probe(bus_.handle(), present);
        if (start_result != ESP_OK) {
            schedule_display_retry("sondowanie PCF8574AT", start_result, now);
            return;
        }
        reportBusRecovery();
        if (!present) {
            schedule_display_retry("sondowanie PCF8574AT", ESP_ERR_NOT_FOUND, now);
            return;
        }

        start_result = display_.initialize(bus_.handle(), now,
                                           desired_backlight_.load(std::memory_order_acquire));
        if (start_result != ESP_OK) {
            schedule_display_retry("dodanie PCF8574AT", start_result, now);
            return;
        }

        display_started = true;
        display_retry_pending = false;
        if (display_absence_reported || (currentFault() != ESP_OK)) {
            display_recovery_active = true;
            snapshot_dirty = snapshot_available;
        }
    };

    esp_err_t result = bus_.initialize();
    if (result == ESP_OK) {
        start_display(next_wake);
    } else {
        reportFault("uruchomienie wspolnej magistrali I2C", result, true);
        bus_retry_pending = true;
        bus_retry_started_at = next_wake;
    }

    for (;;) {
        Snapshot received_snapshot{};
        if (xQueueReceive(snapshot_queue_, &received_snapshot, 0) == pdPASS) {
            latest_snapshot = received_snapshot;
            snapshot_available = true;
            snapshot_dirty = true;
        }

        desired_backlight_on = desired_backlight_.load(std::memory_order_acquire);

        const TickType_t now = xTaskGetTickCount();
        if (snapshot_available &&
            !LcdDashboard::measurementPageEnabled(measurement_page, latest_snapshot.measurement)) {
            const LcdDashboard::MeasurementPage available_page =
                LcdDashboard::nextMeasurementPage(measurement_page, latest_snapshot.measurement);
            if (available_page != measurement_page) {
                measurement_page = available_page;
                measurement_page_started_at = now;
                snapshot_dirty = true;
            }
        }
        if (snapshot_available && (latest_snapshot.status == LcdDashboard::Status::ok) &&
            ((now - information_page_started_at) >= information_page_interval)) {
            information_page = LcdDashboard::nextInformationPage(information_page);
            information_page_started_at = now;
            snapshot_dirty = true;
        } else if (snapshot_available && (latest_snapshot.status != LcdDashboard::Status::ok) &&
                   (information_page != LcdDashboard::InformationPage::status)) {
            information_page = LcdDashboard::InformationPage::status;
            information_page_started_at = now;
            snapshot_dirty = true;
        }

        if (snapshot_available &&
            ((now - measurement_page_started_at) >= measurement_page_interval)) {
            measurement_page =
                LcdDashboard::nextMeasurementPage(measurement_page, latest_snapshot.measurement);
            measurement_page_started_at = now;
            snapshot_dirty = true;
        }

        if (!bus_.isInitialized() &&
            (!bus_retry_pending || ((now - bus_retry_started_at) >= recovery_interval))) {
            result = bus_.initialize();
            if (result == ESP_OK) {
                bus_retry_pending = false;
#if LED_CONTROLLER_AT24C08_ENABLED
                settings_attach_retry_pending = false;
#endif
#if LED_CONTROLLER_INA228_ENABLED
                ina228_next_action_at_ = now;
#endif
                start_display(now);
            } else {
                reportFault("odtworzenie wspolnej magistrali I2C", result, true);
                bus_retry_pending = true;
                bus_retry_started_at = now;
            }
        }

        if (bus_.isInitialized() && !display_started &&
            (!display_retry_pending || ((now - display_retry_started_at) >= recovery_interval))) {
            start_display(now);
        }

#if LED_CONTROLLER_AT24C08_ENABLED
        if (bus_.isInitialized() && (settings_service_ != nullptr) &&
            settings_service_->isInitialized()) {
            if (!settings_service_->isBusAttached() &&
                (!settings_attach_retry_pending ||
                 ((now - settings_attach_retry_started_at) >= recovery_interval))) {
                const esp_err_t settings_result = settings_service_->attachBus(bus_.handle(), now);
                if (settings_result == ESP_OK) {
                    settings_attach_retry_pending = false;
                    settings_attach_fault_reported = false;
                } else {
                    if (!settings_attach_fault_reported) {
                        ESP_LOGW(kTag, "AT24C08N: nie udalo sie dodac adaptera: %s",
                                 esp_err_to_name(settings_result));
                        settings_attach_fault_reported = true;
                    }
                    settings_attach_retry_pending = true;
                    settings_attach_retry_started_at = now;
                }
            }
            if (settings_service_->isBusAttached()) {
                settings_service_->service(now);
                if (settings_service_->takeBusRecoveryRequest()) {
                    recoverSharedBusAfterTimeout("transakcja AT24C08N", ESP_ERR_TIMEOUT);
                }
            }
        }
#endif

#if LED_CONTROLLER_DS1307_NVRAM_ENABLED
        if (bus_.isInitialized() && snapshot_available && local_controller_ != nullptr) {
            const esp_err_t nvram_result =
                nvram_.service(bus_.handle(), *local_controller_, latest_snapshot.local_config,
                               latest_snapshot.local_config_revision, now);
            if (nvram_result == ESP_ERR_TIMEOUT) {
                recoverSharedBusAfterTimeout("transakcja DS1307", nvram_result);
            }
        }
#endif
#if LED_CONTROLLER_INA228_ENABLED
        serviceIna228(now);
#endif
#if LED_CONTROLLER_DS18B20_ENABLED
        temperature_service_.service(now);
#endif

        if (display_started) {
            result = display_.service(now);
            if (result != ESP_OK) {
                schedule_display_retry("inicjalizacja HD44780", result, now);
            } else if (display_.isReady()) {
                const char* failed_operation = nullptr;
                const bool recovery_frame_due =
                    !recovery_frame_waiting ||
                    ((now - recovery_frame_started_at) >= recovery_frame_interval);
                const bool frame_due = snapshot_dirty && recovery_frame_due;
                const bool backlight_change_pending =
                    display_.isBacklightOn() != desired_backlight_on;

                if (backlight_change_pending || frame_due) {
                    result = display_.validateExpanderState();
                    failed_operation = "kontrola stanu PCF8574AT";
                    if (result == ESP_OK) {
                        reportBusRecovery();
                    }
                }

                if ((result == ESP_OK) && backlight_change_pending) {
                    result = display_.setBacklight(desired_backlight_on);
                    failed_operation = "przelaczenie podswietlenia";
                    if (result == ESP_OK) {
                        ESP_LOGI(kTag, "Podswietlenie LCD -> %s",
                                 display_.isBacklightOn() ? "ON" : "OFF");
                    }
                }

                if ((result == ESP_OK) && frame_due) {
                    const LcdDashboard::Lines lines =
                        latest_snapshot.service_active
                            ? latest_snapshot.service_lines
                            : LcdDashboard::render(latest_snapshot.channels, latest_snapshot.status,
                                                   latest_snapshot.uptime_seconds,
                                                   latest_snapshot.network, latest_snapshot.mqtt,
                                                   information_page, latest_snapshot.notice,
                                                   latest_snapshot.measurement, measurement_page);
                    result = display_.show(lines);
                    failed_operation = "odswiezenie ekranu";
                    if (result == ESP_OK) {
                        reportBusRecovery();
                        if (display_recovery_active) {
                            ++recovery_frames_written;
                            recovery_frame_waiting =
                                recovery_frames_written < board_config::kLcdRecoveryFrameCount;
                            snapshot_dirty = recovery_frame_waiting;
                            recovery_frame_started_at = now;
                            if (!recovery_frame_waiting) {
                                display_recovery_active = false;
                                reportRecovery();
                                if (display_absence_reported) {
                                    ESP_LOGI(kTag,
                                             "LCD: PCF8574AT ponownie dostepny; inicjalizacja, "
                                             "czyszczenie i dwie pelne ramki zakonczone");
                                    display_absence_reported = false;
                                }
                            }
                        } else {
                            snapshot_dirty = false;
                            recovery_frames_written = 0;
                            recovery_frame_waiting = false;
                        }
                    }
                }

                if (result == ESP_OK && !display_recovery_active &&
                    (now - verification_started_at) >= verification_interval) {
                    result = display_.verifyNextCharacter();
                    failed_operation = "weryfikacja DDRAM LCD";
                    verification_started_at = now;
                    verification_interval = pdMS_TO_TICKS(result == ESP_OK ? 125 : 10'000);
                }
                if (result != ESP_OK) {
                    verification_started_at = now;
                    verification_interval = pdMS_TO_TICKS(10'000);
                    schedule_display_retry(failed_operation, result, now);
                }
            }
        }

        xTaskDelayUntil(&next_wake, interval);
    }
}

void LcdService::reportFault(const char* const operation, const esp_err_t error,
                             const bool shared_bus_fault) noexcept {
    std::int32_t expected = ESP_OK;
    if (fault_code_.compare_exchange_strong(expected, static_cast<std::int32_t>(error),
                                            std::memory_order_acq_rel)) {
        fault_is_shared_bus_ = shared_bus_fault;
        ESP_LOGE(kTag, "%s: %s nie powiodlo sie: %s; ponowienie co %u ms",
                 shared_bus_fault ? "I2C" : "LCD", operation, esp_err_to_name(error),
                 static_cast<unsigned>(board_config::kLcdRecoveryRetryIntervalMs));
    }
}

void LcdService::recoverSharedBusAfterTimeout(const char* const operation,
                                              const esp_err_t error) noexcept {
    if (error != ESP_ERR_TIMEOUT) {
        return;
    }

    reportFault(operation, error, true);
    if (bus_.isInitialized()) {
        const esp_err_t recovery_result = bus_.recover();
        if (recovery_result != ESP_OK) {
            reportFault("reset kontrolera wspolnej magistrali I2C", recovery_result, true);
        }
    }
}

void LcdService::reportBusRecovery() noexcept {
    if (!fault_is_shared_bus_) {
        return;
    }
    const std::int32_t previous = fault_code_.exchange(ESP_OK, std::memory_order_acq_rel);
    fault_is_shared_bus_ = false;
    if (previous != ESP_OK) {
        ESP_LOGI(kTag, "I2C: magistrala ponownie odpowiada; klienci wznawiaja prace niezaleznie");
    }
}

void LcdService::reportRecovery() noexcept {
    const std::int32_t previous = fault_code_.exchange(ESP_OK, std::memory_order_acq_rel);
    fault_is_shared_bus_ = false;
    if (previous != ESP_OK) {
        ESP_LOGI(kTag, "LCD: lacznosc przywrocona, podwojna inicjalizacja i dwie ramki zakonczone");
    }
}

#if LED_CONTROLLER_INA228_ENABLED
esp_err_t LcdService::initializeIna228(const TickType_t now) noexcept {
    const esp_err_t reset_result = resetIna228();
    if (reset_result != ESP_OK) {
        return reset_result;
    }
    if (!bus_.isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result = ina228_.initialize(bus_.handle());
    if (result == ESP_OK) {
        ina228_next_action_at_ = now;
        ina228_configured_ = false;
        ina228_device_id_ = 0;
    }
    return result;
}

esp_err_t LcdService::resetIna228() noexcept {
    const esp_err_t result = ina228_.reset();
    if (result == ESP_OK) {
        ina228_configured_ = false;
        ina228_device_id_ = 0;
    }
    return result;
}

void LcdService::serviceIna228(const TickType_t now) noexcept {
    if (!bus_.isInitialized() || !deadlineReached(now, ina228_next_action_at_)) {
        return;
    }

    if (!ina228_.isInitialized()) {
        const esp_err_t initialization_result = initializeIna228(now);
        if (initialization_result != ESP_OK) {
            recoverSharedBusAfterTimeout("utworzenie klienta INA228", initialization_result);
            markIna228Unavailable(initialization_result, false);
            if (!ina228_fault_reported_) {
                ESP_LOGW(kTag, "INA228: nie udalo sie dodac adaptera: %s",
                         esp_err_to_name(initialization_result));
                ina228_fault_reported_ = true;
            }
            ina228_next_action_at_ = now + pdMS_TO_TICKS(board_config::kIna228FaultRetryIntervalMs);
            return;
        }
    }

    if (!ina228_configured_) {
        bool present = false;
        esp_err_t result = ina228_.probe(present);
        if (result != ESP_OK) {
            recoverSharedBusAfterTimeout("sondowanie INA228", result);
            markIna228Unavailable(result, false);
            if (!ina228_fault_reported_) {
                ESP_LOGW(kTag, "INA228: blad sondowania: %s", esp_err_to_name(result));
                ina228_fault_reported_ = true;
            }
            ina228_next_action_at_ = now + pdMS_TO_TICKS(board_config::kIna228FaultRetryIntervalMs);
            return;
        }
        reportBusRecovery();
        if (!present) {
            markIna228Unavailable(ESP_ERR_NOT_FOUND, false);
            if (!ina228_absence_reported_) {
                ESP_LOGI(
                    kTag, "INA228: uklad nieobecny pod adresem 0x%02X; ponowienie co %u s",
                    static_cast<unsigned>(board_config::kIna228I2cAddress),
                    static_cast<unsigned>(board_config::kIna228AbsentRetryIntervalMs / 1'000U));
                ina228_absence_reported_ = true;
            }
            ina228_fault_reported_ = false;
            ina228_next_action_at_ =
                now + pdMS_TO_TICKS(board_config::kIna228AbsentRetryIntervalMs);
            return;
        }

        std::uint16_t device_id = 0;
        result = ina228_.identifyAndConfigure(device_id);
        if (result != ESP_OK) {
            recoverSharedBusAfterTimeout("konfiguracja INA228", result);
            markIna228Unavailable(result, true);
            if (!ina228_fault_reported_) {
                ESP_LOGW(kTag, "INA228: identyfikacja lub konfiguracja nie powiodla sie: %s",
                         esp_err_to_name(result));
                ina228_fault_reported_ = true;
            }
            ina228_next_action_at_ = now + pdMS_TO_TICKS(board_config::kIna228FaultRetryIntervalMs);
            return;
        }

        ina228_configured_ = true;
        ina228_device_id_ = device_id;
        ina228_absence_reported_ = false;
        ina228_fault_reported_ = false;
        ESP_LOGI(kTag, "INA228: wykryto ID=0x%04X, adres 0x%02X, bocznik 10 mOhm, SHUNT_CAL=%u",
                 static_cast<unsigned>(device_id),
                 static_cast<unsigned>(board_config::kIna228I2cAddress),
                 static_cast<unsigned>(ina228_scaling::kNominalShuntCalibration));
    }

    Ina228Telemetry telemetry{};
    const esp_err_t result = ina228_.readTelemetry(telemetry);
    if (result != ESP_OK) {
        recoverSharedBusAfterTimeout("odczyt INA228", result);
        markIna228Unavailable(result, true);
        ina228_configured_ = false;
        if (!ina228_fault_reported_) {
            ESP_LOGW(kTag, "INA228: odczyt nie powiodl sie: %s; ponowienie konfiguracji",
                     esp_err_to_name(result));
            ina228_fault_reported_ = true;
        }
        ina228_next_action_at_ = now + pdMS_TO_TICKS(board_config::kIna228FaultRetryIntervalMs);
        return;
    }

    reportBusRecovery();
    telemetry.device_id = ina228_device_id_;
    telemetry.last_error = ESP_OK;
    telemetry.present = true;
    telemetry.valid = true;
#if LED_CONTROLLER_AT24C08_ENABLED
    if (settings_service_ != nullptr) {
        const std::int32_t correction_ppm = settings_service_->inaCurrentCorrectionPpm();
        telemetry.current_milliamps =
            applySignedCorrection(telemetry.current_milliamps, correction_ppm);
        telemetry.power_milliwatts =
            applyUnsignedCorrection(telemetry.power_milliwatts, correction_ppm);
        telemetry.energy_milliwatt_hours =
            applyUnsignedCorrection(telemetry.energy_milliwatt_hours, correction_ppm);
    }
#endif
    publishIna228Telemetry(telemetry);
    ina228_fault_reported_ = false;
    ina228_next_action_at_ = now + pdMS_TO_TICKS(board_config::kIna228SampleIntervalMs);
}

void LcdService::publishIna228Telemetry(const Ina228Telemetry& telemetry) noexcept {
    if ((ina228_mutex_ == nullptr) || (xSemaphoreTake(ina228_mutex_, portMAX_DELAY) != pdTRUE)) {
        return;
    }
    Ina228Telemetry next = telemetry;
    next.revision = ina228_telemetry_.revision + 1U;
    ina228_telemetry_ = next;
    static_cast<void>(xSemaphoreGive(ina228_mutex_));
}

void LcdService::markIna228Unavailable(const esp_err_t error, const bool present) noexcept {
    if ((ina228_mutex_ == nullptr) || (xSemaphoreTake(ina228_mutex_, portMAX_DELAY) != pdTRUE)) {
        return;
    }
    if ((ina228_telemetry_.valid || (ina228_telemetry_.present != present) ||
         (ina228_telemetry_.last_error != error))) {
        ++ina228_telemetry_.revision;
    }
    ina228_telemetry_.present = present;
    ina228_telemetry_.valid = false;
    ina228_telemetry_.last_error = error;
    static_cast<void>(xSemaphoreGive(ina228_mutex_));
}
#endif
