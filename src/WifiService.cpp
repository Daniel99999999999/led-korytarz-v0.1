#include "WifiService.hpp"

#include "StackMonitor.hpp"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "lwip/def.h"
#include "nvs_flash.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {
constexpr char kTag[] = "wifi_service";
constexpr char kNtpServer[] = "pool.ntp.org";
constexpr char kWarsawTimezone[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";

#if LED_CONTROLLER_AT24C08_ENABLED
static_assert(WifiService::kMaxSsidLength == persistent_settings::kMaxSsidLength);
static_assert(WifiService::kMaxPasswordLength == persistent_settings::kMaxWifiPasswordLength);
#endif

class MutexGuard final {
  public:
    explicit MutexGuard(const SemaphoreHandle_t mutex,
                        const TickType_t timeout = portMAX_DELAY) noexcept
        : mutex_(mutex) {
        locked_ = (mutex_ != nullptr) && (xSemaphoreTake(mutex_, timeout) == pdTRUE);
    }

    ~MutexGuard() {
        if (locked_) {
            xSemaphoreGive(mutex_);
        }
    }

    [[nodiscard]] bool locked() const noexcept {
        return locked_;
    }

  private:
    SemaphoreHandle_t mutex_{nullptr};
    bool locked_{false};
};

[[nodiscard]] std::size_t boundedLength(const char* const text,
                                        const std::size_t maximum) noexcept {
    if (text == nullptr) {
        return 0;
    }

    std::size_t length = 0;
    while ((length < maximum) && (text[length] != '\0')) {
        ++length;
    }
    return length;
}
} // namespace

esp_err_t WifiService::initialize() noexcept {
    if (initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    mutex_ = xSemaphoreCreateMutexStatic(&mutex_storage_);
    if (mutex_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    reconnect_timer_ =
        xTimerCreateStatic("wifi_retry", pdMS_TO_TICKS(kReconnectIntervalMs), pdFALSE, this,
                           reconnectTimerEntry, &reconnect_timer_storage_);
    status_timer_ = xTimerCreateStatic("wifi_status", pdMS_TO_TICKS(kStatusRefreshIntervalMs),
                                       pdTRUE, this, statusTimerEntry, &status_timer_storage_);
    if ((reconnect_timer_ == nullptr) || (status_timer_ == nullptr)) {
        return ESP_ERR_NO_MEM;
    }

    if (setenv("TZ", kWarsawTimezone, 1) != 0) {
        return ESP_FAIL;
    }
    tzset();

    {
        MutexGuard guard(mutex_);
        if (!guard.locked()) {
            return ESP_FAIL;
        }
        status_.initialized = true;
        status_.state = State::unconfigured;
        updateStatusFromConfigurationLocked();
    }

    initialized_ = true;
    return ESP_OK;
}

esp_err_t WifiService::start() noexcept {
    if (!initialized_ || started_) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = nvs_flash_init();
    if (result == ESP_OK) {
#if LED_CONTROLLER_AT24C08_ENABLED
        ESP_LOGI(kTag, "NVS gotowe dla PHY i klucza ustawien; sterownik Wi-Fi nadal uzywa RAM");
#else
        ESP_LOGI(kTag,
                 "NVS gotowe tylko dla danych kalibracji PHY; konfiguracja Wi-Fi pozostaje w RAM");
#endif
    } else {
        ESP_LOGW(kTag, "NVS kalibracji PHY niedostepne: %s; radio wykona pelna kalibracje",
                 esp_err_to_name(result));
    }

    result = esp_netif_init();
    if (result != ESP_OK) {
        return result;
    }

    result = esp_event_loop_create_default();
    if ((result != ESP_OK) && (result != ESP_ERR_INVALID_STATE)) {
        return result;
    }

    netif_ = esp_netif_create_default_wifi_sta();
    if (netif_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    wifi_init.nvs_enable = 0;
    result = esp_wifi_init(&wifi_init);
    if (result != ESP_OK) {
        return result;
    }
    wifi_initialized_ = true;

    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) {
        return result;
    }

    result = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, eventHandler, this,
                                                 &wifi_event_instance_);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, eventHandler, this,
                                                 &ip_event_instance_);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_event_handler_instance_register(NETIF_SNTP_EVENT, NETIF_SNTP_TIME_SYNC,
                                                 eventHandler, this, &sntp_event_instance_);
    if (result != ESP_OK) {
        return result;
    }

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(kNtpServer);
    sntp_config.start = false;
    result = esp_netif_sntp_init(&sntp_config);
    if (result != ESP_OK) {
        return result;
    }
    sntp_initialized_ = true;

    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_wifi_start();
    if (result != ESP_OK) {
        return result;
    }

    started_ = true;
    {
        MutexGuard guard(mutex_);
        if (guard.locked()) {
            status_.started = true;
            status_.state = State::unconfigured;
        }
    }

    if (xTimerStart(status_timer_, 0) != pdPASS) {
        ESP_LOGW(kTag, "Nie udalo sie uruchomic okresowego odczytu RSSI");
    }

#if LED_CONTROLLER_AT24C08_ENABLED
    ESP_LOGI(kTag, "Wi-Fi STA gotowe; konfiguracja robocza w RAM, zapis przez AT24C08N");
#else
    ESP_LOGI(kTag, "Wi-Fi STA gotowe; konfiguracja i storage tylko RAM");
#endif
    ESP_LOGI(kTag, "NTP: %s, TZ Warszawa: %s", kNtpServer, kWarsawTimezone);
    return ESP_OK;
}

esp_err_t WifiService::setSsid(const char* const ssid) noexcept {
    if (!initialized_ || (ssid == nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }

    const std::size_t length = boundedLength(ssid, kMaxSsidLength + 1U);
    if ((length == 0U) || (length > kMaxSsidLength)) {
        return ESP_ERR_INVALID_ARG;
    }

    MutexGuard guard(mutex_);
    if (!guard.locked()) {
        return ESP_FAIL;
    }
    copyText(staged_config_.ssid.data(), staged_config_.ssid.size(), ssid, kMaxSsidLength);
    updateStatusFromConfigurationLocked();
    return ESP_OK;
}

esp_err_t WifiService::setPassword(const char* const password) noexcept {
    if (!initialized_ || (password == nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }

    const std::size_t length = boundedLength(password, kMaxPasswordLength + 1U);
    if ((length > kMaxPasswordLength) || ((length > 0U) && (length < 8U))) {
        return ESP_ERR_INVALID_ARG;
    }

    MutexGuard guard(mutex_);
    if (!guard.locked()) {
        return ESP_FAIL;
    }
    secureZero(staged_config_.password.data(), staged_config_.password.size());
    copyText(staged_config_.password.data(), staged_config_.password.size(), password,
             kMaxPasswordLength);
    updateStatusFromConfigurationLocked();
    return ESP_OK;
}

esp_err_t WifiService::useDhcp() noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    MutexGuard guard(mutex_);
    if (!guard.locked()) {
        return ESP_FAIL;
    }
    staged_config_.ip_mode = IpMode::dhcp;
    staged_config_.ip_info = {};
    updateStatusFromConfigurationLocked();
    return ESP_OK;
}

esp_err_t WifiService::useStatic(const esp_ip4_addr_t& ip, const esp_ip4_addr_t& netmask,
                                 const esp_ip4_addr_t& gateway) noexcept {
    if (!initialized_ || (ip.addr == 0U) || (gateway.addr == 0U) || !netmaskIsValid(netmask)) {
        return ESP_ERR_INVALID_ARG;
    }

    MutexGuard guard(mutex_);
    if (!guard.locked()) {
        return ESP_FAIL;
    }
    staged_config_.ip_mode = IpMode::static_ip;
    staged_config_.ip_info.ip = ip;
    staged_config_.ip_info.netmask = netmask;
    staged_config_.ip_info.gw = gateway;
    updateStatusFromConfigurationLocked();
    return ESP_OK;
}

esp_err_t WifiService::setDns(const esp_ip4_addr_t& primary,
                              const esp_ip4_addr_t& secondary) noexcept {
    if (!initialized_ || (primary.addr == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    MutexGuard guard(mutex_);
    if (!guard.locked()) {
        return ESP_FAIL;
    }
    staged_config_.dns_primary = primary;
    staged_config_.dns_secondary = secondary;
    updateStatusFromConfigurationLocked();
    return ESP_OK;
}

esp_err_t WifiService::apply() noexcept {
    if (!started_ || !wifi_initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    RuntimeConfig candidate{};
    {
        MutexGuard guard(mutex_);
        if (!guard.locked()) {
            return ESP_FAIL;
        }
        candidate = staged_config_;
    }

    if (!configurationIsValid(candidate)) {
        secureZero(&candidate, sizeof(candidate));
        return ESP_ERR_INVALID_ARG;
    }

    network_ready_.store(false, std::memory_order_release);
    reconnect_enabled_.store(false, std::memory_order_release);
    static_cast<void>(xTimerStop(reconnect_timer_, 0));
    const esp_err_t disconnect_result = esp_wifi_disconnect();
    if ((disconnect_result != ESP_OK) && (disconnect_result != ESP_ERR_WIFI_NOT_CONNECT)) {
        secureZero(&candidate, sizeof(candidate));
        return disconnect_result;
    }

    esp_err_t result = configureIp(candidate);
    if (result != ESP_OK) {
        secureZero(&candidate, sizeof(candidate));
        return result;
    }

    wifi_config_t wifi_config{};
    const std::size_t ssid_length = boundedLength(candidate.ssid.data(), kMaxSsidLength);
    const std::size_t password_length =
        boundedLength(candidate.password.data(), kMaxPasswordLength);
    std::memcpy(wifi_config.sta.ssid, candidate.ssid.data(), ssid_length);
    std::memcpy(wifi_config.sta.password, candidate.password.data(), password_length);
    wifi_config.sta.threshold.authmode =
        (password_length == 0U) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    result = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    secureZero(&wifi_config, sizeof(wifi_config));
    if (result != ESP_OK) {
        secureZero(&candidate, sizeof(candidate));
        return result;
    }

    {
        MutexGuard guard(mutex_);
        if (!guard.locked()) {
            secureZero(&candidate, sizeof(candidate));
            return ESP_FAIL;
        }
        secureZero(&applied_config_, sizeof(applied_config_));
        applied_config_ = candidate;
        configuration_applied_.store(true, std::memory_order_release);
        status_.state = State::connecting;
        status_.configuration_applied = true;
        status_.disconnect_reason = 0;
        updateStatusFromConfigurationLocked();
    }

    ESP_LOGI(kTag, "=== WIFI CONFIG === SSID=%s, mode=%s", candidate.ssid.data(),
             candidate.ip_mode == IpMode::dhcp ? "DHCP" : "static");
    ESP_LOGI(kTag, "Configured IPv4=" IPSTR ", Mask=" IPSTR ", Gateway=" IPSTR,
             IP2STR(&candidate.ip_info.ip), IP2STR(&candidate.ip_info.netmask),
             IP2STR(&candidate.ip_info.gw));
    ESP_LOGI(kTag, "Configured DNS1=" IPSTR ", DNS2=" IPSTR " (0.0.0.0 = automatic)",
             IP2STR(&candidate.dns_primary), IP2STR(&candidate.dns_secondary));
    ESP_LOGI(kTag, "=== NTP === Server=%s, TZ=%s", kNtpServer, kWarsawTimezone);
    reconnect_enabled_.store(true, std::memory_order_release);
    result = esp_wifi_connect();
    if (result != ESP_OK) {
        reconnect_enabled_.store(false, std::memory_order_release);
        MutexGuard guard(mutex_);
        if (guard.locked()) {
            status_.state = State::disconnected;
        }
    }

    secureZero(&candidate, sizeof(candidate));
    return result;
}

esp_err_t WifiService::disconnect() noexcept {
    if (!started_) {
        return ESP_ERR_INVALID_STATE;
    }

    network_ready_.store(false, std::memory_order_release);
    reconnect_enabled_.store(false, std::memory_order_release);
    static_cast<void>(xTimerStop(reconnect_timer_, 0));
    const esp_err_t result = esp_wifi_disconnect();
    if ((result != ESP_OK) && (result != ESP_ERR_WIFI_NOT_CONNECT)) {
        return result;
    }

    MutexGuard guard(mutex_);
    if (guard.locked()) {
        status_.state = configuration_applied_.load(std::memory_order_acquire)
                            ? State::disconnected
                            : State::unconfigured;
    }
    return ESP_OK;
}

esp_err_t WifiService::clear() noexcept {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    if (started_) {
        result = disconnect();
        if (result != ESP_OK) {
            return result;
        }

        wifi_config_t empty_config{};
        result = esp_wifi_set_config(WIFI_IF_STA, &empty_config);
        secureZero(&empty_config, sizeof(empty_config));
        if (result != ESP_OK) {
            return result;
        }
    }

    MutexGuard guard(mutex_);
    if (!guard.locked()) {
        return ESP_FAIL;
    }
    secureZero(&staged_config_, sizeof(staged_config_));
    secureZero(&applied_config_, sizeof(applied_config_));
    staged_config_.ip_mode = IpMode::dhcp;
    applied_config_.ip_mode = IpMode::dhcp;
    configuration_applied_.store(false, std::memory_order_release);
    status_ = {};
    status_.initialized = initialized_;
    status_.started = started_;
    status_.state = started_ ? State::unconfigured : State::unavailable;
    updateStatusFromConfigurationLocked();
    return ESP_OK;
}

esp_err_t WifiService::scan() noexcept {
    if (!started_) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_wifi_scan_start(nullptr, false);
}

WifiService::Status WifiService::status() const noexcept {
    Status snapshot{};
    {
        MutexGuard guard(mutex_);
        if (!guard.locked()) {
            return snapshot;
        }
        snapshot = status_;
    }

    formatLocalTime(snapshot);
    return snapshot;
}

bool WifiService::tryGetStatus(Status& output) const noexcept {
    Status snapshot{};
    {
        MutexGuard guard(mutex_, 0);
        if (!guard.locked()) {
            return false;
        }
        snapshot = status_;
    }

    formatLocalTime(snapshot);
    output = snapshot;
    return true;
}

#if LED_CONTROLLER_AT24C08_ENABLED
esp_err_t
WifiService::exportPersistentConfig(persistent_settings::WifiConfig& output) const noexcept {
    output = {};
    MutexGuard guard(mutex_, 0);
    if (!guard.locked()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!configuration_applied_.load(std::memory_order_acquire)) {
        return ESP_OK;
    }

    output.configured = true;
    output.dhcp = applied_config_.ip_mode == IpMode::dhcp;
    output.ip = applied_config_.ip_info.ip.addr;
    output.netmask = applied_config_.ip_info.netmask.addr;
    output.gateway = applied_config_.ip_info.gw.addr;
    output.dns_primary = applied_config_.dns_primary.addr;
    output.dns_secondary = applied_config_.dns_secondary.addr;
    copyText(output.ssid.data(), output.ssid.size(), applied_config_.ssid.data(), kMaxSsidLength);
    copyText(output.password.data(), output.password.size(), applied_config_.password.data(),
             kMaxPasswordLength);
    return ESP_OK;
}

esp_err_t
WifiService::restorePersistentConfig(const persistent_settings::WifiConfig& input) noexcept {
    if (!initialized_ || !input.configured) {
        return ESP_ERR_INVALID_ARG;
    }

    RuntimeConfig candidate{};
    copyText(candidate.ssid.data(), candidate.ssid.size(), input.ssid.data(), kMaxSsidLength);
    copyText(candidate.password.data(), candidate.password.size(), input.password.data(),
             kMaxPasswordLength);
    candidate.ip_mode = input.dhcp ? IpMode::dhcp : IpMode::static_ip;
    candidate.ip_info.ip.addr = input.ip;
    candidate.ip_info.netmask.addr = input.netmask;
    candidate.ip_info.gw.addr = input.gateway;
    candidate.dns_primary.addr = input.dns_primary;
    candidate.dns_secondary.addr = input.dns_secondary;
    if (!configurationIsValid(candidate)) {
        secureZero(&candidate, sizeof(candidate));
        return ESP_ERR_INVALID_ARG;
    }

    MutexGuard guard(mutex_);
    if (!guard.locked()) {
        secureZero(&candidate, sizeof(candidate));
        return ESP_FAIL;
    }
    secureZero(&staged_config_, sizeof(staged_config_));
    staged_config_ = candidate;
    secureZero(&candidate, sizeof(candidate));
    updateStatusFromConfigurationLocked();
    return ESP_OK;
}
#endif

void WifiService::eventHandler(void* const context, const esp_event_base_t event_base,
                               const std::int32_t event_id, void* const event_data) noexcept {

    stack_monitor::Scope stack_sample(stack_monitor::Task::event,
                                      CONFIG_ESP_SYSTEM_EVENT_TASK_STACK_SIZE, false);
    auto* const service = static_cast<WifiService*>(context);
    if (event_base == WIFI_EVENT) {
        service->handleWifiEvent(event_id, event_data);
    } else if (event_base == IP_EVENT) {
        service->handleIpEvent(event_id, event_data);
    } else if (event_base == NETIF_SNTP_EVENT) {
        service->handleSntpEvent(event_id);
    }
}

void WifiService::reconnectTimerEntry(const TimerHandle_t timer) noexcept {
    auto* const service = static_cast<WifiService*>(pvTimerGetTimerID(timer));
    service->reconnectFromTimer();
}

void WifiService::statusTimerEntry(const TimerHandle_t timer) noexcept {
    auto* const service = static_cast<WifiService*>(pvTimerGetTimerID(timer));
    service->refreshLinkDetails();
}

void WifiService::handleWifiEvent(const std::int32_t event_id, void* const event_data) noexcept {
    switch (event_id) {
    case WIFI_EVENT_STA_START:
        break;

    case WIFI_EVENT_STA_CONNECTED: {
        MutexGuard guard(mutex_);
        if (guard.locked()) {
            status_.state = State::connecting;
            status_.disconnect_reason = 0;
        }
        break;
    }

    case WIFI_EVENT_STA_DISCONNECTED: {
        network_ready_.store(false, std::memory_order_release);
        std::uint8_t reason = 0;
        if (event_data != nullptr) {
            const auto* const disconnected =
                static_cast<wifi_event_sta_disconnected_t*>(event_data);
            reason = disconnected->reason;
        }
        {
            MutexGuard guard(mutex_);
            if (guard.locked()) {
                status_.state = configuration_applied_.load(std::memory_order_acquire)
                                    ? State::disconnected
                                    : State::unconfigured;
                status_.disconnect_reason = reason;
                status_.rssi_dbm = 0;
                status_.ip.fill('\0');
                status_.netmask.fill('\0');
                status_.gateway.fill('\0');
                status_.dns_primary.fill('\0');
                status_.dns_secondary.fill('\0');
            }
        }
        if (reconnect_enabled_.load(std::memory_order_acquire) &&
            configuration_applied_.load(std::memory_order_acquire)) {
            ESP_LOGW(kTag, "Wi-Fi rozlaczone, reason=%u; ponowienie za %u ms",
                     static_cast<unsigned>(reason), static_cast<unsigned>(kReconnectIntervalMs));
            scheduleReconnect();
        }
        break;
    }

    case WIFI_EVENT_SCAN_DONE:
        handleScanDone();
        break;

    default:
        break;
    }
}

void WifiService::handleIpEvent(const std::int32_t event_id, void* const event_data) noexcept {
    if (event_id == IP_EVENT_STA_LOST_IP) {
        network_ready_.store(false, std::memory_order_release);
        return;
    }
    if ((event_id != IP_EVENT_STA_GOT_IP) || (event_data == nullptr)) {
        return;
    }

    const auto* const got_ip = static_cast<ip_event_got_ip_t*>(event_data);
    {
        MutexGuard guard(mutex_);
        if (guard.locked()) {
            status_.state = State::connected;
            formatIp(status_.ip.data(), status_.ip.size(), got_ip->ip_info.ip);
            formatIp(status_.netmask.data(), status_.netmask.size(), got_ip->ip_info.netmask);
            formatIp(status_.gateway.data(), status_.gateway.size(), got_ip->ip_info.gw);
        }
    }

    if (configuration_applied_.load(std::memory_order_acquire)) {
        RuntimeConfig applied{};
        {
            MutexGuard guard(mutex_);
            if (guard.locked()) {
                applied = applied_config_;
            }
        }
        if (applied.dns_primary.addr != 0U) {
            static_cast<void>(applyDnsAddress(ESP_NETIF_DNS_MAIN, applied.dns_primary));
        }
        if (applied.dns_secondary.addr != 0U) {
            static_cast<void>(applyDnsAddress(ESP_NETIF_DNS_BACKUP, applied.dns_secondary));
        }
        secureZero(&applied, sizeof(applied));
    }

    updateNetworkAddresses();
    refreshLinkDetails();
    const esp_err_t sntp_result = startSntp();
    if ((sntp_result != ESP_OK) && (sntp_result != ESP_ERR_INVALID_STATE)) {
        ESP_LOGW(kTag, "Nie udalo sie uruchomic SNTP: %s", esp_err_to_name(sntp_result));
    }

    const Status snapshot = status();
    ESP_LOGI(kTag, "=== NETWORK RUNTIME === SSID=%s", snapshot.ssid.data());
    ESP_LOGI(kTag, "Polaczono; IPv4=%s, maska=%s, brama=%s, RSSI=%d dBm", snapshot.ip.data(),
             snapshot.netmask.data(), snapshot.gateway.data(), static_cast<int>(snapshot.rssi_dbm));
    ESP_LOGI(kTag, "Siec runtime: tryb=%s, DNS1=%s, DNS2=%s, NTP=%s",
             snapshot.dhcp ? "DHCP" : "statyczny", snapshot.dns_primary.data(),
             snapshot.dns_secondary.data(),
             snapshot.time_synchronized ? "zsynchronizowany" : "oczekiwanie");
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ESP_LOGI(kTag, "BSSID=%02x:%02x:%02x:%02x:%02x:%02x", ap.bssid[0], ap.bssid[1], ap.bssid[2],
                 ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    }
    // Publish readiness only after runtime DNS and the diagnostic snapshot are ready.
    network_ready_.store(true, std::memory_order_release);
}

void WifiService::handleSntpEvent(const std::int32_t event_id) noexcept {
    if (event_id != NETIF_SNTP_TIME_SYNC) {
        return;
    }

    {
        MutexGuard guard(mutex_);
        if (guard.locked()) {
            status_.time_synchronized = true;
        }
    }
    const Status snapshot = status();
    ESP_LOGI(kTag, "NTP zsynchronizowany; czas Warszawa: %s", snapshot.local_time.data());
}

void WifiService::handleScanDone() noexcept {
    std::uint16_t found = 0;
    esp_err_t result = esp_wifi_scan_get_ap_num(&found);
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "Nie udalo sie odczytac liczby sieci: %s", esp_err_to_name(result));
        return;
    }

    std::array<wifi_ap_record_t, kMaxScanResults> records{};
    std::uint16_t requested = static_cast<std::uint16_t>(records.size());
    result = esp_wifi_scan_get_ap_records(&requested, records.data());
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "Nie udalo sie odczytac wynikow skanu: %s", esp_err_to_name(result));
        return;
    }

    ESP_LOGI(kTag, "Skan Wi-Fi: znaleziono %u, pokazuje %u", static_cast<unsigned>(found),
             static_cast<unsigned>(requested));
    for (std::uint16_t index = 0; index < requested; ++index) {
        const wifi_ap_record_t& record = records[index];
        const std::size_t ssid_length =
            boundedLength(reinterpret_cast<const char*>(record.ssid), sizeof(record.ssid));
        ESP_LOGI(kTag, "%u: %.*s, RSSI=%d dBm, kanal=%u", static_cast<unsigned>(index + 1U),
                 static_cast<int>(ssid_length), reinterpret_cast<const char*>(record.ssid),
                 static_cast<int>(record.rssi), static_cast<unsigned>(record.primary));
    }
}

void WifiService::scheduleReconnect() noexcept {
    if (xTimerReset(reconnect_timer_, 0) != pdPASS) {
        ESP_LOGW(kTag, "Nie udalo sie zaplanowac reconnectu Wi-Fi");
    }
}

void WifiService::reconnectFromTimer() noexcept {
    State state = State::unavailable;
    {
        MutexGuard guard(mutex_);
        if (guard.locked()) {
            state = status_.state;
            if (reconnect_enabled_.load(std::memory_order_acquire) &&
                configuration_applied_.load(std::memory_order_acquire) &&
                (state == State::disconnected)) {
                status_.state = State::connecting;
            }
        }
    }

    if (!reconnect_enabled_.load(std::memory_order_acquire) ||
        !configuration_applied_.load(std::memory_order_acquire) || (state != State::disconnected)) {
        return;
    }

    const esp_err_t result = esp_wifi_connect();
    if (result != ESP_OK) {
        {
            MutexGuard guard(mutex_);
            if (guard.locked()) {
                status_.state = State::disconnected;
            }
        }
        scheduleReconnect();
    }
}

void WifiService::refreshLinkDetails() noexcept {
    bool connected = false;
    {
        MutexGuard guard(mutex_);
        if (guard.locked()) {
            connected = status_.state == State::connected;
        }
    }
    if (!connected) {
        return;
    }

    wifi_ap_record_t access_point{};
    if (esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) {
        MutexGuard guard(mutex_);
        if (guard.locked()) {
            status_.rssi_dbm = access_point.rssi;
        }
    }
}

void WifiService::updateNetworkAddresses() noexcept {
    if (netif_ == nullptr) {
        return;
    }

    esp_netif_ip_info_t ip_info{};
    esp_netif_dns_info_t primary{};
    esp_netif_dns_info_t secondary{};
    static_cast<void>(esp_netif_get_ip_info(netif_, &ip_info));
    static_cast<void>(esp_netif_get_dns_info(netif_, ESP_NETIF_DNS_MAIN, &primary));
    static_cast<void>(esp_netif_get_dns_info(netif_, ESP_NETIF_DNS_BACKUP, &secondary));

    MutexGuard guard(mutex_);
    if (!guard.locked()) {
        return;
    }
    formatIp(status_.ip.data(), status_.ip.size(), ip_info.ip);
    formatIp(status_.netmask.data(), status_.netmask.size(), ip_info.netmask);
    formatIp(status_.gateway.data(), status_.gateway.size(), ip_info.gw);
    formatIp(status_.dns_primary.data(), status_.dns_primary.size(), primary.ip.u_addr.ip4);
    formatIp(status_.dns_secondary.data(), status_.dns_secondary.size(), secondary.ip.u_addr.ip4);
}

void WifiService::updateStatusFromConfigurationLocked() noexcept {
    const std::size_t ssid_length = boundedLength(staged_config_.ssid.data(), kMaxSsidLength);
    const std::size_t password_length =
        boundedLength(staged_config_.password.data(), kMaxPasswordLength);
    status_.configuration_ready =
        (ssid_length > 0U) && ((password_length == 0U) || (password_length >= 8U));
    status_.password_set = password_length > 0U;
    const bool configuration_applied = configuration_applied_.load(std::memory_order_acquire);
    status_.configuration_applied = configuration_applied;
    status_.dhcp = staged_config_.ip_mode == IpMode::dhcp;
    status_.pending_changes =
        configuration_applied && !configurationsEqual(staged_config_, applied_config_);

    const RuntimeConfig& visible_config = configuration_applied ? applied_config_ : staged_config_;
    copyText(status_.ssid.data(), status_.ssid.size(), visible_config.ssid.data(), kMaxSsidLength);
    if (!configuration_applied && (ssid_length == 0U) && started_) {
        status_.state = State::unconfigured;
    } else if (!configuration_applied && (ssid_length > 0U) && started_) {
        status_.state = State::ready;
    }
}

esp_err_t WifiService::configureIp(const RuntimeConfig& config) noexcept {
    if (netif_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (config.ip_mode == IpMode::dhcp) {
        const esp_err_t result = esp_netif_dhcpc_start(netif_);
        if ((result != ESP_OK) && (result != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)) {
            return result;
        }
        return ESP_OK;
    }

    esp_err_t result = esp_netif_dhcpc_stop(netif_);
    if ((result != ESP_OK) && (result != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)) {
        return result;
    }
    result = esp_netif_set_ip_info(netif_, &config.ip_info);
    if (result != ESP_OK) {
        return result;
    }
    if (config.dns_primary.addr != 0U) {
        result = applyDnsAddress(ESP_NETIF_DNS_MAIN, config.dns_primary);
    }
    if ((result == ESP_OK) && (config.dns_secondary.addr != 0U)) {
        result = applyDnsAddress(ESP_NETIF_DNS_BACKUP, config.dns_secondary);
    }
    return result;
}

esp_err_t WifiService::applyDnsAddress(const esp_netif_dns_type_t type,
                                       const esp_ip4_addr_t& address) noexcept {
    esp_netif_dns_info_t dns{};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4 = address;
    return esp_netif_set_dns_info(netif_, type, &dns);
}

esp_err_t WifiService::startSntp() noexcept {
    if (!sntp_initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sntp_started_) {
        return ESP_OK;
    }

    const esp_err_t result = esp_netif_sntp_start();
    if (result == ESP_OK) {
        sntp_started_ = true;
        ESP_LOGI(kTag, "SNTP uruchomiony; oczekiwanie na %s", kNtpServer);
    }
    return result;
}

bool WifiService::configurationIsValid(const RuntimeConfig& config) const noexcept {
    const std::size_t ssid_length = boundedLength(config.ssid.data(), kMaxSsidLength + 1U);
    const std::size_t password_length =
        boundedLength(config.password.data(), kMaxPasswordLength + 1U);
    if ((ssid_length == 0U) || (ssid_length > kMaxSsidLength) ||
        (password_length > kMaxPasswordLength) ||
        ((password_length > 0U) && (password_length < 8U))) {
        return false;
    }
    if (config.ip_mode == IpMode::static_ip) {
        return (config.ip_info.ip.addr != 0U) && (config.ip_info.gw.addr != 0U) &&
               netmaskIsValid(config.ip_info.netmask);
    }
    return true;
}

bool WifiService::netmaskIsValid(const esp_ip4_addr_t& netmask) noexcept {
    const std::uint32_t host_mask = lwip_ntohl(netmask.addr);
    if (host_mask == 0U) {
        return false;
    }
    const std::uint32_t inverse = ~host_mask;
    return (inverse & (inverse + 1U)) == 0U;
}

bool WifiService::configurationsEqual(const RuntimeConfig& left,
                                      const RuntimeConfig& right) noexcept {
    return (left.ssid == right.ssid) && (left.password == right.password) &&
           (left.ip_info.ip.addr == right.ip_info.ip.addr) &&
           (left.ip_info.netmask.addr == right.ip_info.netmask.addr) &&
           (left.ip_info.gw.addr == right.ip_info.gw.addr) &&
           (left.dns_primary.addr == right.dns_primary.addr) &&
           (left.dns_secondary.addr == right.dns_secondary.addr) && (left.ip_mode == right.ip_mode);
}

void WifiService::copyText(char* const destination, const std::size_t destination_size,
                           const char* const source, const std::size_t source_limit) noexcept {
    if ((destination == nullptr) || (destination_size == 0U)) {
        return;
    }
    destination[0] = '\0';
    if (source == nullptr) {
        return;
    }

    const std::size_t length = std::min(boundedLength(source, source_limit), destination_size - 1U);
    std::memcpy(destination, source, length);
    destination[length] = '\0';
}

void WifiService::formatIp(char* const destination, const std::size_t destination_size,
                           const esp_ip4_addr_t& address) noexcept {
    if ((destination == nullptr) || (destination_size == 0U)) {
        return;
    }
    static_cast<void>(std::snprintf(destination, destination_size, IPSTR, IP2STR(&address)));
}

void WifiService::formatLocalTime(Status& status) noexcept {
    if (!status.time_synchronized) {
        return;
    }

    const std::time_t now = std::time(nullptr);
    std::tm local{};
    if (localtime_r(&now, &local) != nullptr) {
        static_cast<void>(std::strftime(status.local_time.data(), status.local_time.size(),
                                        "%d.%m.%Y %H:%M:%S %Z", &local));
    }
}

void WifiService::secureZero(void* const data, const std::size_t size) noexcept {
    auto* output = static_cast<volatile std::uint8_t*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        output[index] = 0;
    }
}
