#pragma once

#include "OtaRequestValidator.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class OtaService final {
  public:
    enum class State : std::uint8_t {
        unavailable,
        validating,
        idle,
        queued,
        downloading,
        verifying,
        rebooting,
        failed,
    };

    struct Status {
        State state{State::unavailable};
        std::array<char, 32> current_version{};
        std::array<char, 32> candidate_version{};
        std::uint32_t revision{0};
        std::uint8_t progress_percent{0};
        esp_err_t last_error{ESP_OK};
    };

    using StatusCallback = void (*)(void* context, const Status& status) noexcept;

    OtaService() noexcept = default;

    esp_err_t initialize() noexcept;
    esp_err_t start() noexcept;
    esp_err_t requestUpdate(const char* url, std::size_t length) noexcept;
    esp_err_t beginLocalUpdate() noexcept;
    void finishLocalUpdate(esp_err_t result, const char* version) noexcept;
    esp_err_t setStatusCallback(StatusCallback callback, void* context) noexcept;

    [[nodiscard]] Status status() const noexcept;
    [[nodiscard]] static const char* stateName(State state) noexcept;

  private:
    static constexpr std::uint32_t kValidationDelayMs = 10'000U;
    static constexpr std::uint32_t kRebootStatusDelayMs = 750U;
    static constexpr std::uint32_t kHttpTimeoutMs = 15'000U;
    static constexpr std::uint32_t kTaskStackSize = 8'192U;
    static constexpr UBaseType_t kTaskPriority = 3U;
    static constexpr std::uint8_t kProgressStepPercent = 10U;

    struct Request {
        std::array<char, ota_request::kMaxUrlLength + 1U> url{};
    };

    static void taskEntry(void* context) noexcept;
    [[noreturn]] void taskLoop() noexcept;
    esp_err_t validatePendingImage() noexcept;
    esp_err_t performUpdate(const Request& request) noexcept;
    void updateStatus(State state, std::uint8_t progress_percent, esp_err_t error,
                      const char* candidate_version = nullptr) noexcept;
    void publishCurrentStatus() noexcept;

    mutable StaticSemaphore_t mutex_storage_{};
    mutable SemaphoreHandle_t mutex_{nullptr};
    StaticQueue_t request_queue_storage_{};
    std::array<std::uint8_t, sizeof(Request)> request_queue_buffer_{};
    QueueHandle_t request_queue_{nullptr};
    TaskHandle_t task_handle_{nullptr};
    Status status_{};
    StatusCallback status_callback_{nullptr};
    void* status_callback_context_{nullptr};
    std::atomic<bool> request_in_flight_{false};
    bool validation_pending_{false};
    bool initialized_{false};
    bool started_{false};
};
