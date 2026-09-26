#include "FeatureConfig.hpp"

#if LED_CONTROLLER_INA228_ENABLED

#include "BoardConfig.hpp"
#include "Ina228.hpp"
#include "Ina228Scaling.hpp"

#include <array>

namespace {
constexpr std::uint8_t kConfigRegister = 0x00;
constexpr std::uint8_t kAdcConfigRegister = 0x01;
constexpr std::uint8_t kShuntCalibrationRegister = 0x02;
constexpr std::uint8_t kShuntVoltageRegister = 0x04;
constexpr std::uint8_t kBusVoltageRegister = 0x05;
constexpr std::uint8_t kDieTemperatureRegister = 0x06;
constexpr std::uint8_t kCurrentRegister = 0x07;
constexpr std::uint8_t kPowerRegister = 0x08;
constexpr std::uint8_t kEnergyRegister = 0x09;
constexpr std::uint8_t kDiagnosticRegister = 0x0B;
constexpr std::uint8_t kManufacturerIdRegister = 0x3E;
constexpr std::uint8_t kDeviceIdRegister = 0x3F;

constexpr std::uint16_t kManufacturerId = 0x5449;
constexpr std::uint16_t kDeviceIdMask = 0xFFF0;
constexpr std::uint16_t kDeviceIdValue = 0x2280;
constexpr std::uint16_t kConfigAdcRangeWide = 0x0000;
// Ciagle VBUS + VSHUNT + temperatura, 1052 us na kanal i usrednianie 64 probek.
constexpr std::uint16_t kAdcConfig = 0xFB6B;
constexpr std::uint16_t kDiagnosticMemoryValidMask = 1U << 0U;
constexpr std::uint16_t kDiagnosticMathOverflowMask = 1U << 9U;

[[nodiscard]] constexpr std::uint16_t readBigEndian16(const std::uint8_t* const data) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) | data[1]);
}

[[nodiscard]] constexpr std::uint32_t readBigEndian24(const std::uint8_t* const data) noexcept {
    return (static_cast<std::uint32_t>(data[0]) << 16U) |
           (static_cast<std::uint32_t>(data[1]) << 8U) | static_cast<std::uint32_t>(data[2]);
}

[[nodiscard]] constexpr std::uint64_t readBigEndian40(const std::uint8_t* const data) noexcept {
    return (static_cast<std::uint64_t>(data[0]) << 32U) |
           (static_cast<std::uint64_t>(data[1]) << 24U) |
           (static_cast<std::uint64_t>(data[2]) << 16U) |
           (static_cast<std::uint64_t>(data[3]) << 8U) | static_cast<std::uint64_t>(data[4]);
}
} // namespace

esp_err_t Ina228::initialize(const i2c_master_bus_handle_t bus_handle) noexcept {
    if (isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (bus_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = board_config::kIna228I2cAddress;
    config.scl_speed_hz = board_config::kSharedI2cFrequencyHz;
    config.scl_wait_us = 0;
    config.flags.disable_ack_check = false;

    const esp_err_t result = i2c_master_bus_add_device(bus_handle, &config, &device_handle_);
    if (result == ESP_OK) {
        bus_handle_ = bus_handle;
    }
    return result;
}

esp_err_t Ina228::reset() noexcept {
    if (device_handle_ == nullptr) {
        bus_handle_ = nullptr;
        return ESP_OK;
    }

    const esp_err_t result = i2c_master_bus_rm_device(device_handle_);
    if (result == ESP_OK) {
        device_handle_ = nullptr;
        bus_handle_ = nullptr;
    }
    return result;
}

esp_err_t Ina228::probe(bool& present) noexcept {
    present = false;
    if (!isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        i2c_master_probe(bus_handle_, board_config::kIna228I2cAddress,
                         static_cast<int>(board_config::kIna228ProbeTimeoutMs));
    if (result == ESP_OK) {
        present = true;
        return ESP_OK;
    }
    return (result == ESP_ERR_NOT_FOUND) ? ESP_OK : result;
}

esp_err_t Ina228::identifyAndConfigure(std::uint16_t& device_id) noexcept {
    device_id = 0;
    if (!isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    std::uint16_t manufacturer_id = 0;
    esp_err_t result = readRegister16(kManufacturerIdRegister, manufacturer_id);
    if (result != ESP_OK) {
        return result;
    }
    result = readRegister16(kDeviceIdRegister, device_id);
    if (result != ESP_OK) {
        return result;
    }
    if ((manufacturer_id != kManufacturerId) || ((device_id & kDeviceIdMask) != kDeviceIdValue)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    result = writeRegister16(kConfigRegister, kConfigAdcRangeWide);
    if (result == ESP_OK) {
        result = writeRegister16(kAdcConfigRegister, kAdcConfig);
    }
    if (result == ESP_OK) {
        result =
            writeRegister16(kShuntCalibrationRegister, ina228_scaling::kNominalShuntCalibration);
    }
    if (result != ESP_OK) {
        return result;
    }

    std::uint16_t readback = 0;
    result = readRegister16(kConfigRegister, readback);
    if ((result != ESP_OK) || ((readback & 0x0030U) != kConfigAdcRangeWide)) {
        return (result == ESP_OK) ? ESP_ERR_INVALID_RESPONSE : result;
    }
    result = readRegister16(kAdcConfigRegister, readback);
    if ((result != ESP_OK) || (readback != kAdcConfig)) {
        return (result == ESP_OK) ? ESP_ERR_INVALID_RESPONSE : result;
    }
    result = readRegister16(kShuntCalibrationRegister, readback);
    if ((result != ESP_OK) || (readback != ina228_scaling::kNominalShuntCalibration)) {
        return (result == ESP_OK) ? ESP_ERR_INVALID_RESPONSE : result;
    }
    return ESP_OK;
}

esp_err_t Ina228::readTelemetry(Ina228Telemetry& telemetry) noexcept {
    if (!isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    std::array<std::uint8_t, 5> data{};
    esp_err_t result = readRegister(kShuntVoltageRegister, data.data(), 3);
    if (result != ESP_OK) {
        return result;
    }
    const std::int32_t shunt_counts = ina228_scaling::decodeSigned20(readBigEndian24(data.data()));

    result = readRegister(kBusVoltageRegister, data.data(), 3);
    if (result != ESP_OK) {
        return result;
    }
    const std::uint32_t bus_counts = ina228_scaling::decodeUnsigned20(readBigEndian24(data.data()));

    result = readRegister(kDieTemperatureRegister, data.data(), 2);
    if (result != ESP_OK) {
        return result;
    }
    const std::int16_t temperature_counts = static_cast<std::int16_t>(readBigEndian16(data.data()));

    result = readRegister(kCurrentRegister, data.data(), 3);
    if (result != ESP_OK) {
        return result;
    }
    const std::int32_t current_counts =
        ina228_scaling::decodeSigned20(readBigEndian24(data.data()));

    result = readRegister(kPowerRegister, data.data(), 3);
    if (result != ESP_OK) {
        return result;
    }
    const std::uint32_t power_counts = readBigEndian24(data.data());

    result = readRegister(kEnergyRegister, data.data(), data.size());
    if (result != ESP_OK) {
        return result;
    }
    const std::uint64_t energy_counts = readBigEndian40(data.data());

    std::uint16_t diagnostic_flags = 0;
    result = readRegister16(kDiagnosticRegister, diagnostic_flags);
    if (result != ESP_OK) {
        return result;
    }

    telemetry.shunt_microvolts = ina228_scaling::shuntMicrovolts(shunt_counts);
    telemetry.bus_voltage_millivolts = ina228_scaling::busVoltageMillivolts(bus_counts);
    telemetry.die_temperature_millicelsius =
        ina228_scaling::dieTemperatureMillicelsius(temperature_counts);
    telemetry.current_milliamps = ina228_scaling::currentMilliamps(current_counts);
    telemetry.power_milliwatts = ina228_scaling::powerMilliwatts(power_counts);
    telemetry.energy_milliwatt_hours = ina228_scaling::energyMilliwattHours(energy_counts);
    telemetry.diagnostic_flags = diagnostic_flags;

    if (((diagnostic_flags & kDiagnosticMemoryValidMask) == 0U) ||
        ((diagnostic_flags & kDiagnosticMathOverflowMask) != 0U)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

bool Ina228::isInitialized() const noexcept {
    return (bus_handle_ != nullptr) && (device_handle_ != nullptr);
}

esp_err_t Ina228::readRegister(const std::uint8_t address, std::uint8_t* const data,
                               const std::size_t size) noexcept {
    if (!isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((data == nullptr) || (size == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(device_handle_, &address, 1, data, size,
                                       static_cast<int>(board_config::kSharedI2cTimeoutMs));
}

esp_err_t Ina228::readRegister16(const std::uint8_t address, std::uint16_t& value) noexcept {
    std::array<std::uint8_t, 2> data{};
    const esp_err_t result = readRegister(address, data.data(), data.size());
    if (result == ESP_OK) {
        value = readBigEndian16(data.data());
    }
    return result;
}

esp_err_t Ina228::writeRegister16(const std::uint8_t address, const std::uint16_t value) noexcept {
    if (!isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    const std::array<std::uint8_t, 3> data{{
        address,
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value & 0xFFU),
    }};
    return i2c_master_transmit(device_handle_, data.data(), data.size(),
                               static_cast<int>(board_config::kSharedI2cTimeoutMs));
}

#endif
