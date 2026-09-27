#pragma once
#include "LcdDashboard.hpp"
#include "OtaService.hpp"
#include "esp_http_server.h"
#include "esp_netif.h"

#include <atomic>

class LocalOtaService final {
  public:
    explicit LocalOtaService(OtaService& ota) noexcept : ota_(ota) {}
    esp_err_t start() noexcept;
    void requestStart() noexcept {
        requested_.store(true, std::memory_order_release);
    }
    bool screen(LcdDashboard::Lines& lines) noexcept;

  private:
    static void taskEntry(void* context) noexcept;
    void taskLoop() noexcept;
    esp_err_t enable() noexcept;
    void disable() noexcept;
    static esp_err_t page(httpd_req_t* request);
    static esp_err_t upload(httpd_req_t* request);
    bool fromAp(httpd_req_t* request) const noexcept;
    OtaService& ota_;
    esp_netif_t* ap_netif_{nullptr};
    httpd_handle_t server_{nullptr};
    TaskHandle_t task_{nullptr};
    StaticSemaphore_t screen_mutex_storage_{};
    SemaphoreHandle_t screen_mutex_{nullptr};
    LcdDashboard::Lines screen_{};
    std::array<char, 33> token_{};
    std::uint32_t ap_address_{0};
    std::atomic<bool> requested_{false}, active_{false}, uploading_{false}, reboot_{false};
};
