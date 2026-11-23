#include "Ds1307Nvram.hpp"

#include "BoardConfig.hpp"
#include "LedController.hpp"
#include "esp_log.h"
#include "esp_timer.h"

#include <algorithm>
#include <type_traits>

namespace {
constexpr char kTag[] = "ds1307_nvram";
constexpr std::uint32_t kRetryMs = 30'000;
constexpr std::uint32_t kSaveDelayMs = 2'000;
} // namespace

esp_err_t Ds1307Nvram::write(const std::uint8_t address, const std::uint8_t* const data,
                             const std::size_t size) noexcept {
    if (address < 0x08U || address >= 0x40U || size == 0U || size > 28U || size > 0x40U - address) {
        return ESP_ERR_INVALID_ARG;
    }
    std::array<std::uint8_t, 29> buffer{};
    buffer[0] = address;
    std::copy_n(data, size, buffer.begin() + 1);
    const auto result =
        i2c_master_transmit(device_, buffer.data(), size + 1U, board_config::kSharedI2cTimeoutMs);
    diagnostics_.io.record(result, io_seconds_);
    return result;
}

esp_err_t Ds1307Nvram::read(const std::uint8_t address, std::uint8_t* const data,
                            const std::size_t size) noexcept {
    if (address < 0x08U || address >= 0x40U || size == 0U || size > 0x40U - address) {
        return ESP_ERR_INVALID_ARG;
    }
    const auto result = i2c_master_transmit_receive(device_, &address, 1, data, size,
                                                    board_config::kSharedI2cTimeoutMs);
    diagnostics_.io.record(result, io_seconds_);
    return result;
}

esp_err_t Ds1307Nvram::service(i2c_master_bus_handle_t bus, LedController& controller,
                               const local_control::Config& config, std::uint32_t revision,
                               TickType_t now) noexcept {
    io_seconds_ = static_cast<std::uint32_t>(esp_timer_get_time() / 1000000);
    const auto before = diagnostics_.io.attempts;
    const auto result = serviceStep(bus, controller, config, revision, now);
    diagnostics_.enabled = true;
    if (result != ESP_OK) {
        if (!unavailable_reported_ || diagnostics_.failed_operation != diagnostics_.operation) {
            ESP_LOGW(kTag, "DS1307 addr=0x68 stage=%s error=%s; retry=30000ms; settings in RAM",
                     nvramOperationName(diagnostics_.operation), esp_err_to_name(result));
        }
        unavailable_reported_ = true;
    }
    diagnostics_.completeOperation(result, diagnostics_.io.attempts != before);
    diagnostics_.record_valid = stored_valid_;
    diagnostics_.runtime_matches = stored_valid_ && local_control::equal(stored_, config);
    diagnostics_.save_pending = revision != 0U && !diagnostics_.runtime_matches;
    diagnostics_.retry_pending = retry_pending_;
    diagnostics_.phase = static_cast<unsigned>(phase_);
    diagnostics_.sequence = sequence_;
    diagnostics_.slot = active_slot_;
    diagnostics_.runtime_revision = revision;
    diagnostics_.ram_key1 = config.button_masks[0];
    diagnostics_.saved_key1 = stored_valid_ ? stored_.button_masks[0] : 0;
    return result;
}

esp_err_t Ds1307Nvram::serviceStep(const i2c_master_bus_handle_t bus, LedController& controller,
                                   const local_control::Config& config,
                                   const std::uint32_t revision, const TickType_t now) noexcept {
    using SignedTick = std::make_signed_t<TickType_t>;
    if (observed_revision_ != revision) {
        observed_revision_ = revision;
        changed_at_ = now;
    }
    if (retry_pending_ && static_cast<SignedTick>(now - retry_at_) < 0) {
        return ESP_OK;
    }
    retry_pending_ = false;
    if (device_ == nullptr) {
        diagnostics_.operation = NvramOperation::attach;
        i2c_device_config_t settings{};
        settings.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        settings.device_address = board_config::kDs1307Address;
        settings.scl_speed_hz = board_config::kSharedI2cFrequencyHz;
        const esp_err_t result = i2c_master_bus_add_device(bus, &settings, &device_);
        if (result != ESP_OK) {
            retry_pending_ = true;
            retry_at_ = now + pdMS_TO_TICKS(kRetryMs);
        }
        return result;
    }
    esp_err_t result = ESP_OK;
    switch (phase_) {
    case Phase::probe:
        diagnostics_.operation = NvramOperation::probe;
        result =
            i2c_master_probe(bus, board_config::kDs1307Address, board_config::kSharedI2cTimeoutMs);
        diagnostics_.io.record(result, io_seconds_);
        if (result == ESP_OK) {
            ESP_LOGI(kTag, "DS1307 stage=probe_0x68 ACK");
            phase_ = Phase::pointer;
        }
        break;
    case Phase::pointer: {
        diagnostics_.operation = NvramOperation::pointer;
        const std::uint8_t address = 0x08;
        // Set the register pointer only; no RAM or RTC data is written.
        result = i2c_master_transmit(device_, &address, 1, board_config::kSharedI2cTimeoutMs);
        diagnostics_.io.record(result, io_seconds_);
        if (result == ESP_OK) {
            ESP_LOGI(kTag, "DS1307 stage=pointer_0x08 ACK");
            phase_ = Phase::read_byte;
        }
        break;
    }
    case Phase::read_byte: {
        diagnostics_.operation = NvramOperation::read_byte;
        std::uint8_t byte = 0;
        result = i2c_master_receive(device_, &byte, 1, board_config::kSharedI2cTimeoutMs);
        diagnostics_.io.record(result, io_seconds_);
        if (result == ESP_OK) {
            ESP_LOGI(kTag, "DS1307 stage=read_byte_0x08 OK; checking combined read next");
            phase_ = Phase::load;
        }
        break;
    }
    case Phase::load: {
        diagnostics_.operation = NvramOperation::read_record;
        std::array<std::uint8_t, 56> bytes{};
        result = read(0x08, bytes.data(), bytes.size());
        if (result != ESP_OK) {
            break;
        }
        std::array<local_control::Record, 2> records{};
        std::copy_n(bytes.begin(), 28, records[0].begin());
        std::copy_n(bytes.begin() + 28, 28, records[1].begin());
        active_slot_ = local_control::newestRecord(records, stored_, sequence_);
        stored_valid_ = active_slot_ >= 0;
        phase_ = stored_valid_ && revision == 0U ? Phase::restore : Phase::idle;
        if (unavailable_reported_) {
            ESP_LOGI(kTag, "DS1307 ponownie dostepny");
        }
        unavailable_reported_ = false;
        ESP_LOGI(kTag, "NVRAM: %s",
                 stored_valid_ ? "poprawny rekord konfiguracji" : "brak poprawnego rekordu");
        break;
    }
    case Phase::restore: {
        if (revision != 0U) {
            phase_ = Phase::idle;
            break;
        }
        ControlCommand command{};
        command.kind = ControlCommand::Kind::restore_local_config;
        command.local_config = stored_;
        if (controller.enqueueCommand(command) == ESP_OK) {
            diagnostics_.restore_queued = true;
            ESP_LOGI(kTag, "NVRAM: odtworzenie zlecone, KEY1 mask=%u, seq=%lu",
                     static_cast<unsigned>(stored_.button_masks[0]),
                     static_cast<unsigned long>(sequence_));
            phase_ = Phase::idle;
        }
        break;
    }
    case Phase::idle:
        if (revision != 0U && (!stored_valid_ || !local_control::equal(stored_, config)) &&
            (now - changed_at_) >= pdMS_TO_TICKS(kSaveDelayMs)) {
            saving_ = config;
            record_ = local_control::encodeRecord(saving_, sequence_ + 1U);
            target_address_ = active_slot_ == 0 ? 0x24 : 0x08;
            phase_ = Phase::invalidate;
        }
        break;
    case Phase::invalidate: {
        diagnostics_.operation = NvramOperation::invalidate;
        const std::uint8_t zero = 0;
        result = write(static_cast<std::uint8_t>(target_address_ + 27U), &zero, 1);
        if (result == ESP_OK) {
            phase_ = Phase::write;
        }
        break;
    }
    case Phase::write:
        diagnostics_.operation = NvramOperation::write_payload;
        result = write(target_address_, record_.data(), record_.size() - 1U);
        if (result == ESP_OK) {
            phase_ = Phase::commit;
        }
        break;
    case Phase::commit:
        diagnostics_.operation = NvramOperation::commit;
        result =
            write(static_cast<std::uint8_t>(target_address_ + 27U), &local_control::kCommit, 1);
        if (result == ESP_OK) {
            phase_ = Phase::verify;
        }
        break;
    case Phase::verify: {
        diagnostics_.operation = NvramOperation::verify;
        local_control::Record actual{};
        result = read(target_address_, actual.data(), actual.size());
        if (result == ESP_OK && actual != record_) {
            result = ESP_ERR_INVALID_CRC;
        }
        if (result == ESP_OK) {
            ++diagnostics_.saves;
            stored_ = saving_;
            stored_valid_ = true;
            ++sequence_;
            active_slot_ = target_address_ == 0x08 ? 0 : 1;
            phase_ = Phase::idle;
            ESP_LOGI(kTag, "Konfiguracja KEY/PIR/LCD zapisana w NVRAM");
        }
        break;
    }
    }
    if (result != ESP_OK) {
        phase_ = Phase::probe;
        retry_pending_ = true;
        retry_at_ = now + pdMS_TO_TICKS(kRetryMs);
    }
    return result;
}
