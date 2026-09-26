#pragma once

#include "IrRemoteMapper.hpp"
#include "NecDecoder.hpp"
#include "driver/rmt_rx.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <array>
#include <cstddef>
#include <cstdint>

class LedController;

class IrReceiverService final {
  public:
    explicit IrReceiverService(LedController& controller) noexcept;

    esp_err_t initialize() noexcept;
    esp_err_t start() noexcept;

  private:
    struct ReceiveEvent final {
        std::size_t symbol_count{0};
    };

    static constexpr std::size_t kRawSymbolCount = 64;
    static constexpr std::size_t kPulseCount = kRawSymbolCount * 2U;
    static constexpr UBaseType_t kEventQueueLength = 1;
    static constexpr std::uint32_t kTaskStackSize = 4096;
    static constexpr UBaseType_t kTaskPriority = 4;

    static bool IRAM_ATTR receiveDoneCallback(rmt_channel_handle_t channel,
                                              const rmt_rx_done_event_data_t* event,
                                              void* context) noexcept;
    static void taskEntry(void* context) noexcept;
    [[noreturn]] void taskLoop() noexcept;
    void processSymbols(std::size_t symbol_count) noexcept;

    LedController& controller_;
    IrRemoteMapper mapper_{};
    rmt_channel_handle_t channel_{nullptr};
    rmt_receive_config_t receive_config_{};
    std::array<rmt_symbol_word_t, kRawSymbolCount> raw_symbols_{};
    std::array<nec_remote::Pulse, kPulseCount> pulses_{};
    StaticQueue_t event_queue_storage_{};
    std::array<std::uint8_t, kEventQueueLength * sizeof(ReceiveEvent)> event_queue_buffer_{};
    QueueHandle_t event_queue_{nullptr};
    TaskHandle_t task_handle_{nullptr};
    bool initialized_{false};
    bool queue_error_reported_{false};
};
