#pragma once

#include "FeatureConfig.hpp"
#if LED_CONTROLLER_AT24C08_ENABLED
#include "PersistentSettings.hpp"
#endif
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class WifiService final {
  public:
    static constexpr std::size_t kMaxSsidLength = 32;
    static constexpr std::size_t kMaxPasswordLength = 64;

    enum class State : std::uint8_t {
        unavailable,
        unconfigured,
        ready,
        connecting,
        connected,
        disconnected,
    };

    struct Status {
        State state{State::unavailable};
        std::array<char, kMaxSsidLength + 1U> ssid{};
        std::array<char, 16> ip{};
        std::array<char, 16> netmask{};
        std::array<char, 16> gateway{};
        std::array<char, 16> dns_primary{};
        std::array<char, 16> dns_secondary{};
        std::array<char, 32> local_time{};
        std::int8_t rssi_dbm{0};
        std::uint8_t disconnect_reason{0};
        bool initialized{false};
        bool started{false};
        bool configuration_ready{false};
        bool configuration_applied{false};
        bool password_set{false};
        bool dhcp{true};
        bool pending_changes{false};
        bool time_synchronized{false};
    };

    esp_err_t initialize() noexcept;
    esp_err_t start() noexcept;
    [[nodiscard]] bool networkReady() const noexcept {
        return network_ready_.load(std::memory_order_acquire);
    }

    esp_err_t setSsid(const char* ssid) noexcept;
    esp_err_t setPassword(const char* password) noexcept;
    esp_err_t useDhcp() noexcept;
    esp_err_t useStatic(const esp_ip4_addr_t& ip, const esp_ip4_addr_t& netmask,
                        const esp_ip4_addr_t& gateway) noexcept;
    esp_err_t setDns(const esp_ip4_addr_t& primary, const esp_ip4_addr_t& secondary) noexcept;

    esp_err_t apply() noexcept;
    esp_err_t disconnect() noexcept;
    esp_err_t clear() noexcept;
    esp_err_t scan() noexcept;

    [[nodiscard]] Status status() const noexcept;
    [[nodiscard]] bool tryGetStatus(Status& output) const noexcept;
#if LED_CONTROLLER_AT24C08_ENABLED
    esp_err_t exportPersistentConfig(persistent_settings::WifiConfig& output) const noexcept;
    esp_err_t restorePersistentConfig(const persistent_settings::WifiConfig& input) noexcept;
#endif

  private:
    static constexpr std::uint32_t kReconnectIntervalMs = 5'000;
    static constexpr std::uint32_t kStatusRefreshIntervalMs = 5'000;
    static constexpr std::size_t kMaxScanResults = 10;

    enum class IpMode : std::uint8_t {
        dhcp,
        static_ip,
    };

    struct RuntimeConfig {
        std::array<char, kMaxSsidLength + 1U> ssid{};
        std::array<char, kMaxPasswordLength + 1U> password{};
        esp_netif_ip_info_t ip_info{};
        esp_ip4_addr_t dns_primary{};
        esp_ip4_addr_t dns_secondary{};
        IpMode ip_mode{IpMode::dhcp};
    };

    static void eventHandler(void* context, esp_event_base_t event_base, std::int32_t event_id,
                             void* event_data) noexcept;
    static void reconnectTimerEntry(TimerHandle_t timer) noexcept;
    static void statusTimerEntry(TimerHandle_t timer) noexcept;

    void handleWifiEvent(std::int32_t event_id, void* event_data) noexcept;
    void handleIpEvent(std::int32_t event_id, void* event_data) noexcept;
    void handleSntpEvent(std::int32_t event_id) noexcept;
    void handleScanDone() noexcept;
    void scheduleReconnect() noexcept;
    void reconnectFromTimer() noexcept;
    void refreshLinkDetails() noexcept;
    void updateNetworkAddresses() noexcept;
    void updateStatusFromConfigurationLocked() noexcept;
    esp_err_t configureIp(const RuntimeConfig& config) noexcept;
    esp_err_t applyDnsAddress(esp_netif_dns_type_t type, const esp_ip4_addr_t& address) noexcept;
    esp_err_t startSntp() noexcept;

    [[nodiscard]] bool configurationIsValid(const RuntimeConfig& config) const noexcept;
    [[nodiscard]] static bool netmaskIsValid(const esp_ip4_addr_t& netmask) noexcept;
    [[nodiscard]] static bool configurationsEqual(const RuntimeConfig& left,
                                                  const RuntimeConfig& right) noexcept;
    static void copyText(char* destination, std::size_t destination_size, const char* source,
                         std::size_t source_limit) noexcept;
    static void formatIp(char* destination, std::size_t destination_size,
                         const esp_ip4_addr_t& address) noexcept;
    static void formatLocalTime(Status& status) noexcept;
    static void secureZero(void* data, std::size_t size) noexcept;

    mutable StaticSemaphore_t mutex_storage_{};
    mutable SemaphoreHandle_t mutex_{nullptr};
    StaticTimer_t reconnect_timer_storage_{};
    StaticTimer_t status_timer_storage_{};
    TimerHandle_t reconnect_timer_{nullptr};
    TimerHandle_t status_timer_{nullptr};
    esp_netif_t* netif_{nullptr};
    esp_event_handler_instance_t wifi_event_instance_{nullptr};
    esp_event_handler_instance_t ip_event_instance_{nullptr};
    esp_event_handler_instance_t sntp_event_instance_{nullptr};
    RuntimeConfig staged_config_{};
    RuntimeConfig applied_config_{};
    Status status_{};
    bool initialized_{false};
    bool wifi_initialized_{false};
    bool started_{false};
    std::atomic<bool> configuration_applied_{false};
    std::atomic<bool> reconnect_enabled_{false};
    std::atomic<bool> network_ready_{false};
    bool sntp_initialized_{false};
    bool sntp_started_{false};
};
