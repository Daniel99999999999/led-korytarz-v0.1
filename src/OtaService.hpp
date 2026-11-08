#pragma once

#include "AutoOtaModel.hpp"
#include "OtaRequestValidator.hpp"
#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class WifiService;

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
        std::uint32_t auto_checks{0}, auto_last_check_s{0}, auto_next_check_s{0};
        esp_err_t auto_error{ESP_OK};
        bool auto_checking{false};
    };

    using StatusCallback = void (*)(void* context, const Status& status) noexcept;

    explicit OtaService(WifiService& wifi) noexcept : wifi_(wifi) {}

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
        bool automatic{false};
        auto_ota::Manifest manifest{};
    };

    static void taskEntry(void* context) noexcept;
    [[noreturn]] void taskLoop() noexcept;
    esp_err_t validatePendingImage() noexcept;
    esp_err_t performUpdate(const Request& request) noexcept;
    void checkAutomaticUpdate() noexcept;
    esp_err_t fetchManifest(auto_ota::Manifest& manifest) noexcept;
    static esp_err_t manifestEvent(esp_http_client_event_t* event) noexcept;
    esp_err_t verifyAutomaticImage(const Request& request, int received) noexcept;
    WifiService& wifi_;
    std::array<char, auto_ota::kManifestLimit + 1> manifest_buffer_{};
    bool manifest_redirect_secure_{false};
    std::uint64_t next_auto_check_s_{0};
    unsigned auto_failures_{0};
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
