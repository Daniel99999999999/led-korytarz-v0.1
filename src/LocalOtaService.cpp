#include "LocalOtaService.hpp"

#include "LocalOtaValidation.hpp"
#include "StackMonitor.hpp"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr char kTag[] = "local_ota";
constexpr std::uint32_t kSessionMs = 600000, kUploadMs = 120000;
constexpr std::size_t kDescriptorOffset = 32;
constexpr std::size_t kHeaderBytes = kDescriptorOffset + sizeof(esp_app_desc_t);
static_assert(kHeaderBytes == local_ota_validation::kHeaderBytes);
constexpr char kPage[] =
    R"HTML(<!doctype html><html lang="pl"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Aktualizacja sterownika LED</title><body><h1>Aktualizacja lokalna</h1><p>Wybierz firmware.bin. Nie odlaczaj zasilania podczas aktualizacji.</p><input id="file" type="file" accept=".bin"><button id="send">Aktualizuj</button><pre id="status"></pre><script>
const token='%s';document.querySelector('#send').onclick=async()=>{const f=document.querySelector('#file').files[0];if(!f)return;const b=document.querySelector('#send'),s=document.querySelector('#status');b.disabled=true;s.textContent='Wysylanie i sprawdzanie obrazu...';try{const r=await fetch('/update',{method:'POST',headers:{'Content-Type':'application/octet-stream','X-OTA-Token':token},body:f});s.textContent=await r.text();}catch(e){s.textContent='Polaczenie przerwane. Sprawdz ekran sterownika.';}b.disabled=false;};
</script></body></html>)HTML";
} // namespace

esp_err_t LocalOtaService::start() noexcept {
    if (task_ != nullptr)
        return ESP_ERR_INVALID_STATE;
    screen_mutex_ = xSemaphoreCreateMutexStatic(&screen_mutex_storage_);
    if (!screen_mutex_)
        return ESP_ERR_NO_MEM;
    ap_netif_ = esp_netif_create_default_wifi_ap();
    if (!ap_netif_)
        return ESP_ERR_NO_MEM;
    if (xTaskCreate(taskEntry, "local_ota", 4096, this, 2, &task_) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
bool LocalOtaService::screen(LcdDashboard::Lines& lines) noexcept {
    if (!active_.load(std::memory_order_acquire) || !screen_mutex_ ||
        xSemaphoreTake(screen_mutex_, 0) != pdTRUE)
        return false;
    lines = screen_;
    xSemaphoreGive(screen_mutex_);
    return true;
}
void LocalOtaService::taskEntry(void* context) noexcept {
    static_cast<LocalOtaService*>(context)->taskLoop();
}
void LocalOtaService::taskLoop() noexcept {
    TickType_t session = 0;
    for (;;) {
        stack_monitor::sample(stack_monitor::Task::local_ota, 4096);
        if (reboot_.load(std::memory_order_acquire)) {
            static_cast<void>(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)));
            esp_restart();
        }
        if (requested_.exchange(false, std::memory_order_acq_rel)) {
            if (!active_.load(std::memory_order_acquire)) {
                const auto result = enable();
                if (result != ESP_OK) {
                    disable();
                    ESP_LOGE(kTag, "Hotspot OTA: %s", esp_err_to_name(result));
                }
            }
            session = xTaskGetTickCount();
        }
        if (active_.load(std::memory_order_acquire) &&
            !uploading_.load(std::memory_order_acquire) &&
            xTaskGetTickCount() - session >= pdMS_TO_TICKS(kSessionMs))
            disable();
        static_cast<void>(ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)));
    }
}
esp_err_t LocalOtaService::enable() noexcept {
    wifi_mode_t mode{};
    auto result = esp_wifi_get_mode(&mode);
    if (result != ESP_OK || mode != WIFI_MODE_STA)
        return ESP_ERR_INVALID_STATE;
    esp_netif_ip_info_t ip{};
    IP4_ADDR(&ip.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
    auto* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t station{};
    if (sta && esp_netif_get_ip_info(sta, &station) == ESP_OK && station.ip.addr != 0 &&
        (station.ip.addr & station.netmask.addr) == (ip.ip.addr & station.netmask.addr)) {
        IP4_ADDR(&ip.ip, 10, 42, 0, 1);
        ip.gw = ip.ip;
        if ((station.ip.addr & station.netmask.addr) == (ip.ip.addr & station.netmask.addr))
            return ESP_ERR_INVALID_STATE;
    }
    static_cast<void>(esp_netif_dhcps_stop(ap_netif_));
    result = esp_netif_set_ip_info(ap_netif_, &ip);
    if (result != ESP_OK)
        return result;
    result = esp_netif_dhcps_start(ap_netif_);
    if (result != ESP_OK && result != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
        return result;
    ap_address_ = ip.ip.addr;
    wifi_config_t config{};
    std::uint8_t mac[6]{};
    result = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (result != ESP_OK)
        return result;
    std::snprintf(reinterpret_cast<char*>(config.ap.ssid), sizeof(config.ap.ssid),
                  "LED-OTA-%02X%02X%02X", mac[3], mac[4], mac[5]);
    std::array<std::uint8_t, 28> random{};
    esp_fill_random(random.data(), random.size());
    constexpr char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (unsigned i = 0; i < 12; ++i)
        config.ap.password[i] = static_cast<std::uint8_t>(alphabet[random[i] & 31U]);
    for (unsigned i = 0; i < 16; ++i)
        std::snprintf(token_.data() + i * 2, 3, "%02x", random[12 + i]);
    config.ap.ssid_len =
        static_cast<std::uint8_t>(std::strlen(reinterpret_cast<char*>(config.ap.ssid)));
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.max_connection = 1;
    config.ap.channel = 1;
    config.ap.pmf_cfg.capable = true;
    result = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (result != ESP_OK)
        return result;
    result = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (result != ESP_OK)
        return result;
    if (xSemaphoreTake(screen_mutex_, portMAX_DELAY) == pdTRUE) {
        for (auto& row : screen_) {
            row.fill(' ');
            row.back() = '\0';
        }
        std::snprintf(screen_[0].data(), screen_[0].size(), "%.20s",
                      reinterpret_cast<char*>(config.ap.ssid));
        std::snprintf(screen_[1].data(), screen_[1].size(), "Haslo:%.12s",
                      reinterpret_cast<char*>(config.ap.password));
        std::array<char, 24> address{};
        std::snprintf(address.data(), address.size(), "http://" IPSTR, IP2STR(&ip.ip));
        std::snprintf(screen_[2].data(), screen_[2].size(), "%.20s", address.data());
        std::snprintf(screen_[3].data(), screen_[3].size(), "OTA lokalne: 10 min");
        for (auto& row : screen_) {
            const auto length = std::strlen(row.data());
            std::fill(row.begin() + static_cast<std::ptrdiff_t>(length), row.end() - 1, ' ');
        }
        xSemaphoreGive(screen_mutex_);
    }
    std::memset(config.ap.password, 0, sizeof(config.ap.password));
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.stack_size = 8192;
    http.max_open_sockets = 3;
    http.max_uri_handlers = 2;
    http.recv_wait_timeout = 5;
    http.send_wait_timeout = 5;
    http.lru_purge_enable = true;
    result = httpd_start(&server_, &http);
    if (result != ESP_OK)
        return result;
    httpd_uri_t root{};
    root.uri = "/";
    root.method = HTTP_GET;
    root.handler = page;
    root.user_ctx = this;
    result = httpd_register_uri_handler(server_, &root);
    if (result != ESP_OK)
        return result;
    httpd_uri_t update{};
    update.uri = "/update";
    update.method = HTTP_POST;
    update.handler = upload;
    update.user_ctx = this;
    result = httpd_register_uri_handler(server_, &update);
    if (result != ESP_OK)
        return result;
    active_.store(true, std::memory_order_release);
    ESP_LOGI(kTag, "Hotspot serwisowy aktywny na 10 min; dane dostepu tylko na LCD");
    return ESP_OK;
}
void LocalOtaService::disable() noexcept {
    active_.store(false, std::memory_order_release);
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
    static_cast<void>(esp_wifi_set_mode(WIFI_MODE_STA));
    token_.fill('\0');
    ESP_LOGI(kTag, "Hotspot serwisowy wylaczony; konfiguracja STA zachowana");
}
bool LocalOtaService::fromAp(httpd_req_t* request) const noexcept {
    sockaddr_in local{};
    socklen_t size = sizeof(local);
    return active_.load(std::memory_order_acquire) &&
           getsockname(httpd_req_to_sockfd(request), reinterpret_cast<sockaddr*>(&local), &size) ==
               0 &&
           local.sin_family == AF_INET && local.sin_addr.s_addr == ap_address_;
}
esp_err_t LocalOtaService::page(httpd_req_t* request) {
    stack_monitor::Scope stack_sample(stack_monitor::Task::http, 8192, false);
    auto& service = *static_cast<LocalOtaService*>(request->user_ctx);
    if (!service.fromAp(request))
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Tylko hotspot serwisowy");
    std::array<char, sizeof(kPage) + 64> html{};
    std::snprintf(html.data(), html.size(), kPage, service.token_.data());
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Frame-Options", "DENY");
    return httpd_resp_send(request, html.data(), HTTPD_RESP_USE_STRLEN);
}
esp_err_t LocalOtaService::upload(httpd_req_t* request) {
    stack_monitor::Scope stack_sample(stack_monitor::Task::http, 8192, false);
    auto& service = *static_cast<LocalOtaService*>(request->user_ctx);
    std::array<char, 33> token{};
    if (!service.fromAp(request) || httpd_req_get_hdr_value_len(request, "X-OTA-Token") != 32 ||
        httpd_req_get_hdr_value_str(request, "X-OTA-Token", token.data(), token.size()) != ESP_OK ||
        token != service.token_)
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Brak autoryzacji sesji");
    const auto* partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition || request->content_len < kHeaderBytes || request->content_len > partition->size)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Niepoprawny rozmiar obrazu");
    service.uploading_.store(true, std::memory_order_release);
    if (service.ota_.beginLocalUpdate() != ESP_OK) {
        service.uploading_.store(false, std::memory_order_release);
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                                   "OTA zajete albo obraz czeka na zatwierdzenie");
    }
    std::array<char, 2048> buffer{};
    esp_ota_handle_t handle = 0;
    esp_app_desc_t candidate{};
    std::size_t received = 0;
    auto result = ESP_OK;
    const auto started = xTaskGetTickCount();
    // Accumulate the complete descriptor before erasing or writing the inactive slot.
    while (received < kHeaderBytes) {
        if (xTaskGetTickCount() - started >= pdMS_TO_TICKS(kUploadMs)) {
            result = ESP_ERR_TIMEOUT;
            break;
        }
        const int n = httpd_req_recv(request, buffer.data() + received, kHeaderBytes - received);
        if (n <= 0) {
            result = ESP_FAIL;
            break;
        }
        received += static_cast<std::size_t>(n);
    }
    if (result == ESP_OK) {
        std::memcpy(&candidate, buffer.data() + kDescriptorOffset, sizeof(candidate));
        const auto* current = esp_app_get_description();
        if (!local_ota_validation::header(buffer.data(), received, current->project_name,
                                          current->version))
            result = ESP_ERR_INVALID_ARG;
    }
    if (result == ESP_OK)
        result = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (result == ESP_OK)
        result = esp_ota_write(handle, buffer.data(), received);
    while (result == ESP_OK && received < request->content_len) {
        if (xTaskGetTickCount() - started >= pdMS_TO_TICKS(kUploadMs)) {
            result = ESP_ERR_TIMEOUT;
            break;
        }
        const auto length = std::min(buffer.size(), request->content_len - received);
        const int n = httpd_req_recv(request, buffer.data(), length);
        if (n <= 0) {
            result = ESP_FAIL;
            break;
        }
        result = esp_ota_write(handle, buffer.data(), static_cast<std::size_t>(n));
        received += static_cast<std::size_t>(n);
    }
    if (result == ESP_OK) {
        result = esp_ota_end(handle);
        handle = 0;
    }
    if (handle)
        static_cast<void>(esp_ota_abort(handle));
    if (result == ESP_OK)
        result = esp_ota_set_boot_partition(partition);
    service.ota_.finishLocalUpdate(result, result == ESP_OK ? candidate.version : nullptr);
    if (result == ESP_OK) {
        httpd_resp_sendstr(request, "Obraz przyjety. Sterownik uruchomi sie ponownie.");
        service.reboot_.store(true, std::memory_order_release);
    } else {
        ESP_LOGW(kTag, "Lokalne OTA przerwane: %s", esp_err_to_name(result));
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST,
                            "Aktualizacja odrzucona; sprawdz plik, wersje i log OTA");
    }
    service.uploading_.store(false, std::memory_order_release);
    return result == ESP_OK ? ESP_OK : ESP_FAIL; // close failed requests with unread payload
}
