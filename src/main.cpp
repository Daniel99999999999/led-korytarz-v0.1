#include "DebugLog.hpp"
#include "DeviceIdentity.hpp"
#include "FeatureConfig.hpp"
#include "StackMonitor.hpp"
#if LED_CONTROLLER_IR_NEC_ENABLED
#include "IrReceiverService.hpp"
#endif
#include "LedController.hpp"
#include "LocalOtaService.hpp"
#include "MqttService.hpp"
#include "OtaService.hpp"
#include "ServiceConsole.hpp"
#if LED_CONTROLLER_AT24C08_ENABLED
#include "SettingsService.hpp"
#endif
#include "WifiService.hpp"
#if LED_CONTROLLER_AT24C08_ENABLED
#include "WebDiagnosticsService.hpp"
#endif
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdint>
#include <cstdio>

namespace {
constexpr char kTag[] = "main";
#if LED_CONTROLLER_AT24C08_ENABLED
constexpr std::uint32_t kWebStartupStackSize = 16U * 1024U;

void webStartupTask(void* context) {
    auto& web = *static_cast<WebDiagnosticsService*>(context);
    esp_err_t result = web.initialize();
    if (result == ESP_OK)
        result = web.start();
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "Panel HTTPS niedostepny: %s", esp_err_to_name(result));
        std::printf("Panel HTTPS niedostepny: %s\n", esp_err_to_name(result));
    }
    ESP_LOGI(kTag, "Zadanie startowe HTTPS: minimalny wolny stos %u B",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    stack_monitor::sample(stack_monitor::Task::web_start, kWebStartupStackSize, true, true);
    vTaskDelete(nullptr);
}
#endif
} // namespace

extern "C" void app_main() {
    stack_monitor::Scope stack_sample(stack_monitor::Task::main, CONFIG_ESP_MAIN_TASK_STACK_SIZE,
                                      true);
    DebugLog::instance().install();
    ESP_LOGI(kTag, "%s - firmware v%s", device_identity::kDisplayName,
             esp_app_get_description()->version);

    static WifiService wifi_service;
    static LedController controller(&wifi_service);
    static OtaService ota_service;
    static LocalOtaService local_ota(ota_service);
    controller.attachLocalOta(local_ota);
    static MqttService mqtt_service(controller, ota_service, wifi_service);
#if LED_CONTROLLER_AT24C08_ENABLED
    static SettingsService settings_service(wifi_service, mqtt_service);
    static ServiceConsole service_console(wifi_service, mqtt_service, controller, settings_service);
    static WebDiagnosticsService web_diagnostics(wifi_service, mqtt_service, ota_service,
                                                 controller, settings_service);
#else
    static ServiceConsole service_console(wifi_service, mqtt_service, controller);
#endif
#if LED_CONTROLLER_IR_NEC_ENABLED
    static IrReceiverService ir_receiver_service(controller);
#endif

    const esp_err_t wifi_prepare_result = wifi_service.initialize();
    if (wifi_prepare_result != ESP_OK) {
        ESP_LOGE(kTag, "Nie udalo sie przygotowac uslugi Wi-Fi: %s",
                 esp_err_to_name(wifi_prepare_result));
    }

    const esp_err_t ota_prepare_result = ota_service.initialize();
    if (ota_prepare_result != ESP_OK) {
        ESP_LOGE(kTag, "OTA niedostepne; sterowanie lokalne pozostaje aktywne: %s",
                 esp_err_to_name(ota_prepare_result));
    }

    const esp_err_t mqtt_prepare_result = mqtt_service.initialize();
    if (mqtt_prepare_result == ESP_OK) {
        ESP_ERROR_CHECK(controller.attachMqttService(&mqtt_service));
    } else {
        ESP_LOGE(kTag, "Nie udalo sie przygotowac uslugi MQTT: %s",
                 esp_err_to_name(mqtt_prepare_result));
    }

#if LED_CONTROLLER_AT24C08_ENABLED
    ESP_ERROR_CHECK(controller.attachSettingsService(&settings_service));
#endif

    ESP_ERROR_CHECK(controller.initialize());
#if LED_CONTROLLER_IR_NEC_ENABLED
    const esp_err_t ir_prepare_result = ir_receiver_service.initialize();
    if (ir_prepare_result != ESP_OK) {
        ESP_LOGE(kTag, "Pilot IR niedostepny; pozostale sterowanie dziala: %s",
                 esp_err_to_name(ir_prepare_result));
    }
#endif
    ESP_ERROR_CHECK(controller.start());

    if (ota_prepare_result == ESP_OK) {
        const esp_err_t ota_start_result = ota_service.start();
        if (ota_start_result != ESP_OK) {
            ESP_LOGE(kTag, "Nie udalo sie uruchomic zadania OTA: %s",
                     esp_err_to_name(ota_start_result));
        }
    }

#if LED_CONTROLLER_IR_NEC_ENABLED
    if (ir_prepare_result == ESP_OK) {
        const esp_err_t ir_start_result = ir_receiver_service.start();
        if (ir_start_result != ESP_OK) {
            ESP_LOGE(kTag, "Nie udalo sie uruchomic zadania pilota IR: %s",
                     esp_err_to_name(ir_start_result));
        }
    }
#endif

    if (wifi_prepare_result == ESP_OK) {
        const esp_err_t wifi_start_result = wifi_service.start();
        if (wifi_start_result == ESP_OK && ota_prepare_result == ESP_OK) {
            const auto local_result = local_ota.start();
            if (local_result != ESP_OK)
                ESP_LOGW(kTag, "Lokalne OTA niedostepne: %s", esp_err_to_name(local_result));
        }
        if (wifi_start_result != ESP_OK) {
            ESP_LOGE(kTag, "Wi-Fi niedostepne; sterowanie lokalne pozostaje aktywne: %s",
                     esp_err_to_name(wifi_start_result));
        }
    }
#if LED_CONTROLLER_AT24C08_ENABLED
    const esp_err_t settings_prepare_result = settings_service.initialize();
    if (settings_prepare_result != ESP_OK) {
        ESP_LOGE(kTag,
                 "Szyfrowane ustawienia niedostepne; sterowanie lokalne i konfiguracja RAM "
                 "pozostaja aktywne: %s",
                 esp_err_to_name(settings_prepare_result));
    }
#endif

    if (mqtt_prepare_result == ESP_OK) {
        const esp_err_t mqtt_start_result = mqtt_service.start();
        if (mqtt_start_result != ESP_OK) {
            ESP_LOGE(kTag, "MQTT niedostepne; sterowanie lokalne pozostaje aktywne: %s",
                     esp_err_to_name(mqtt_start_result));
        }
    }

    esp_err_t console_result = service_console.initialize();
    if (console_result == ESP_OK)
        console_result = service_console.start();
    if (console_result != ESP_OK)
        ESP_LOGE(kTag, "Konsola serwisowa niedostepna: %s", esp_err_to_name(console_result));

#if LED_CONTROLLER_AT24C08_ENABLED
    if (wifi_prepare_result == ESP_OK && settings_prepare_result == ESP_OK &&
        xTaskCreate(webStartupTask, "web_start", kWebStartupStackSize, &web_diagnostics, 2,
                    nullptr) != pdPASS) {
        ESP_LOGW(kTag, "Nie mozna utworzyc zadania HTTPS; sterowanie lokalne dziala");
        std::printf("Panel HTTPS niedostepny: brak pamieci zadania\n");
    }
#endif

    // app_main moze sie zakonczyc. Sterowanie przejmuje zadanie FreeRTOS.
}
