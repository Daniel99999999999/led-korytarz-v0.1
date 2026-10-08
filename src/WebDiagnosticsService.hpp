#pragma once

#include "DeviceTlsIdentity.hpp"
#include "FeatureConfig.hpp"
#include "esp_err.h"
#include "esp_http_server.h"

#include <array>
#include <cstddef>
#include <cstdint>

class LedController;
class MqttService;
class OtaService;
class WifiService;
class SettingsService;

class WebDiagnosticsService final {
  public:
    WebDiagnosticsService(WifiService& wifi, MqttService& mqtt, OtaService& ota,
                          LedController& controller, SettingsService& settings) noexcept;

    esp_err_t initialize() noexcept;
    esp_err_t start() noexcept;

  private:
    static esp_err_t page(httpd_req_t* request);
    static esp_err_t status(httpd_req_t* request);
    static esp_err_t logs(httpd_req_t* request);
    static esp_err_t scanWifi(httpd_req_t* request);
    static esp_err_t changePassword(httpd_req_t* request);
#if LED_CONTROLLER_DS18B20_ENABLED
    static esp_err_t scanTemperature(httpd_req_t* request);
#endif
    bool authorized(httpd_req_t* request, bool allow_setup = false) noexcept;
    bool commandAuthorized(httpd_req_t* request) noexcept;
    void refreshSessionToken() noexcept;
    esp_err_t registerRoutes() noexcept;
    static void setResponseHeaders(httpd_req_t* request) noexcept;

    WifiService& wifi_;
    MqttService& mqtt_;
    OtaService& ota_;
    LedController& controller_;
    SettingsService& settings_;
    DeviceTlsIdentity tls_{};
    httpd_handle_t server_{nullptr};
    std::array<char, 33> session_token_{};
    std::array<char, 96> session_cookie_{};
    std::array<std::uint8_t, 16> session_salt_{};
    std::array<char, 8192> response_buffer_{};
    std::int64_t locked_until_us_{0};
    std::int64_t session_deadline_us_{0};
    unsigned failed_logins_{0};
    bool session_issued_{false};
    bool initialized_{false};
    bool mdns_started_{false};
};
