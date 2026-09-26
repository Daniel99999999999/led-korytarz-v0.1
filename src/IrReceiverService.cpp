#include "IrReceiverService.hpp"

#include "BoardConfig.hpp"
#include "ControlCommand.hpp"
#include "LedController.hpp"
#include "esp_log.h"

#include <algorithm>

namespace {
constexpr char kTag[] = "ir_remote";
constexpr std::uint32_t kRmtResolutionHz = 1'000'000;
// Klasyczny ESP32 ogranicza filtr zaklocen RMT RX do mniej niz 3187 ns.
constexpr std::uint32_t kClassicEsp32GlitchFilterLimitNs = 3'187;
constexpr std::uint32_t kMinimumPulseNs = 2'000;
constexpr std::uint32_t kMaximumPulseNs = 12'000'000;
constexpr std::uint32_t kReceiveRetryMs = 1'000;
static_assert(kMinimumPulseNs < kClassicEsp32GlitchFilterLimitNs);
} // namespace

IrReceiverService::IrReceiverService(LedController& controller) noexcept
    : controller_(controller) {}

esp_err_t IrReceiverService::initialize() noexcept {
    if (initialized_ || (channel_ != nullptr) || (event_queue_ != nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }

    event_queue_ = xQueueCreateStatic(kEventQueueLength, sizeof(ReceiveEvent),
                                      event_queue_buffer_.data(), &event_queue_storage_);
    if (event_queue_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    rmt_rx_channel_config_t channel_config{};
    channel_config.gpio_num = board_config::kIrReceiverPin;
    channel_config.clk_src = RMT_CLK_SRC_DEFAULT;
    channel_config.resolution_hz = kRmtResolutionHz;
    channel_config.mem_block_symbols = kRawSymbolCount;
    channel_config.intr_priority = 0;
    channel_config.flags.invert_in = 0;
    channel_config.flags.with_dma = 0;
    channel_config.flags.allow_pd = 0;

    esp_err_t result = rmt_new_rx_channel(&channel_config, &channel_);
    if (result != ESP_OK) {
        channel_ = nullptr;
        return result;
    }

    rmt_rx_event_callbacks_t callbacks{};
    callbacks.on_recv_done = receiveDoneCallback;
    result = rmt_rx_register_event_callbacks(channel_, &callbacks, this);
    if (result == ESP_OK) {
        result = rmt_enable(channel_);
    }
    if (result != ESP_OK) {
        static_cast<void>(rmt_del_channel(channel_));
        channel_ = nullptr;
        return result;
    }

    receive_config_.signal_range_min_ns = kMinimumPulseNs;
    receive_config_.signal_range_max_ns = kMaximumPulseNs;
    receive_config_.flags.en_partial_rx = 0;
    initialized_ = true;
    ESP_LOGI(kTag, "Odbiornik NEC gotowy na GPIO%u; po resecie wybierz tryb BIALE lub RGB",
             static_cast<unsigned>(board_config::kIrReceiverPin));
    return ESP_OK;
}

esp_err_t IrReceiverService::start() noexcept {
    if (!initialized_ || (task_handle_ != nullptr)) {
        return ESP_ERR_INVALID_STATE;
    }

    const BaseType_t result =
        xTaskCreate(taskEntry, "ir_remote", kTaskStackSize, this, kTaskPriority, &task_handle_);
    return (result == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

bool IRAM_ATTR IrReceiverService::receiveDoneCallback(rmt_channel_handle_t,
                                                      const rmt_rx_done_event_data_t* const event,
                                                      void* const context) noexcept {
    auto* const service = static_cast<IrReceiverService*>(context);
    const ReceiveEvent queued_event{event->num_symbols};
    BaseType_t higher_priority_task_woken = pdFALSE;
    static_cast<void>(
        xQueueSendFromISR(service->event_queue_, &queued_event, &higher_priority_task_woken));
    return higher_priority_task_woken == pdTRUE;
}

void IrReceiverService::taskEntry(void* const context) noexcept {
    static_cast<IrReceiverService*>(context)->taskLoop();
}

[[noreturn]] void IrReceiverService::taskLoop() noexcept {
    bool receive_error_reported = false;
    for (;;) {
        const esp_err_t receive_result =
            rmt_receive(channel_, raw_symbols_.data(), sizeof(raw_symbols_), &receive_config_);
        if (receive_result != ESP_OK) {
            if (!receive_error_reported) {
                ESP_LOGE(kTag, "Nie mozna uruchomic odbioru RMT: %s",
                         esp_err_to_name(receive_result));
                receive_error_reported = true;
            }
            if (receive_result == ESP_ERR_INVALID_ARG) {
                ESP_LOGE(kTag, "Bledna konfiguracja RMT; zadanie IR zostaje zatrzymane");
                vTaskSuspend(nullptr);
            }
            vTaskDelay(pdMS_TO_TICKS(kReceiveRetryMs));
            continue;
        }
        receive_error_reported = false;

        ReceiveEvent event{};
        if (xQueueReceive(event_queue_, &event, portMAX_DELAY) == pdPASS) {
            processSymbols(event.symbol_count);
        }
    }
}

void IrReceiverService::processSymbols(const std::size_t symbol_count) noexcept {
    const std::size_t limited_symbol_count = std::min(symbol_count, raw_symbols_.size());
    std::size_t pulse_count = 0;
    for (std::size_t index = 0; index < limited_symbol_count; ++index) {
        const rmt_symbol_word_t& symbol = raw_symbols_[index];
        if ((symbol.duration0 != 0U) && (pulse_count < pulses_.size())) {
            pulses_[pulse_count++] = nec_remote::Pulse{symbol.duration0, symbol.level0 != 0U};
        }
        if ((symbol.duration1 != 0U) && (pulse_count < pulses_.size())) {
            pulses_[pulse_count++] = nec_remote::Pulse{symbol.duration1, symbol.level1 != 0U};
        }
    }

    const nec_remote::Frame frame = nec_remote::decode(pulses_.data(), pulse_count);
    const std::uint32_t now_ms = static_cast<std::uint32_t>(pdTICKS_TO_MS(xTaskGetTickCount()));
    const IrRemoteAction action = mapper_.process(frame, now_ms);
    if (action.type == IrRemoteAction::Type::none) {
        return;
    }

    ControlCommand command{};
    command.kind = ControlCommand::Kind::infrared;
    command.infrared = action;
    const esp_err_t result = controller_.enqueueCommand(command);
    if ((result != ESP_OK) && !queue_error_reported_) {
        ESP_LOGW(kTag, "Kolejka sterownika odrzucila komende IR: %s", esp_err_to_name(result));
        queue_error_reported_ = true;
    } else if (result == ESP_OK) {
        queue_error_reported_ = false;
    }
}
