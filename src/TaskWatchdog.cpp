#include "TaskWatchdog.hpp"

#include "esp_task_wdt.h"
#include "sdkconfig.h"

esp_err_t TaskWatchdog::initialize(const std::uint32_t timeout_ms) noexcept {
    esp_task_wdt_config_t config{};
    config.timeout_ms = timeout_ms;
    config.idle_core_mask = (1U << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1U;
    config.trigger_panic = true;

    const esp_err_t result = esp_task_wdt_reconfigure(&config);
    return (result == ESP_ERR_INVALID_STATE) ? esp_task_wdt_init(&config) : result;
}

esp_err_t TaskWatchdog::subscribeCurrentTask() noexcept {
    return esp_task_wdt_add(nullptr);
}

esp_err_t TaskWatchdog::feed() noexcept {
    return esp_task_wdt_reset();
}
