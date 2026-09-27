#include "LocalConfigRecord.hpp"

#include <algorithm>

namespace local_control {
namespace {
std::uint32_t crc(const Record& record) noexcept {
    std::uint32_t value = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < 23; ++index) {
        value ^= record[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            value = (value >> 1U) ^ ((value & 1U) ? 0xEDB88320U : 0U);
        }
    }
    return ~value;
}
void put32(Record& record, const std::size_t offset, const std::uint32_t value) noexcept {
    for (unsigned byte = 0; byte < 4; ++byte) {
        record[offset + byte] = static_cast<std::uint8_t>(value >> (8U * byte));
    }
}
std::uint32_t get32(const Record& record, const std::size_t offset) noexcept {
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) {
        value |= static_cast<std::uint32_t>(record[offset + byte]) << (8U * byte);
    }
    return value;
}
} // namespace

Record encodeRecord(const Config& config, const std::uint32_t sequence) noexcept {
    Record record{};
    if (!valid(config)) {
        return record;
    }
    record[0] = 'L';
    record[1] = 'C';
    record[2] = 'F';
    record[3] = 2;
    put32(record, 4, sequence);
    unsigned bit = 64;
    const auto put = [&](std::uint32_t value, unsigned count) {
        for (unsigned i = 0; i < count; ++i, ++bit)
            record[bit / 8] |= static_cast<std::uint8_t>(((value >> i) & 1U) << (bit % 8));
    };
    for (auto mask : config.button_masks) {
        const auto found = std::find(kButtonMasks.begin(), kButtonMasks.end(), mask);
        put(static_cast<std::uint32_t>(found - kButtonMasks.begin()), 3);
    }
    for (auto delay : config.off_delay_seconds)
        put(delay, 12);
    put(config.lcd_override_seconds, 12);
    put(static_cast<unsigned>(config.backlight_mode), 2);
    for (const auto& pwm : config.pwm) {
        put(pwm.frequency_hz, 15);
        put(pwm.minimum_percent, 7);
        put(pwm.on_index, 4);
        put(pwm.off_index, 4);
        put(pwm.on_off ? 1U : 0U, 1);
    }
    // 115 payload bits fit in bytes 8..22; CRC and commit retain their v1 offsets.
    put32(record, 23, crc(record));
    record[27] = kCommit;
    return record;
}

bool decodeRecord(const Record& record, Config& config, std::uint32_t& sequence) noexcept {
    if (record[0] != 'L' || record[1] != 'C' || record[2] != 'F' ||
        (record[3] != 1 && record[3] != 2) || record[27] != kCommit ||
        get32(record, 23) != crc(record)) {
        return false;
    }
    Config decoded{};
    if (record[3] == 1) {
        std::copy_n(record.begin() + 8, kChannelCount, decoded.button_masks.begin());
        for (std::size_t index = 0; index < kPirCount; ++index) {
            decoded.off_delay_seconds[index] = static_cast<std::uint16_t>(
                record[13 + index * 2] | (static_cast<unsigned>(record[14 + index * 2]) << 8U));
        }
        decoded.lcd_override_seconds =
            static_cast<std::uint16_t>(record[17] | (static_cast<unsigned>(record[18]) << 8U));
        decoded.backlight_mode = static_cast<BacklightMode>(record[19]);
    } else {
        unsigned bit = 64;
        const auto get = [&](unsigned count) {
            std::uint32_t value = 0;
            for (unsigned i = 0; i < count; ++i, ++bit)
                value |= static_cast<std::uint32_t>((record[bit / 8] >> (bit % 8)) & 1U) << i;
            return value;
        };
        for (auto& mask : decoded.button_masks)
            mask = kButtonMasks[get(3)];
        for (auto& delay : decoded.off_delay_seconds)
            delay = static_cast<std::uint16_t>(get(12));
        decoded.lcd_override_seconds = static_cast<std::uint16_t>(get(12));
        decoded.backlight_mode = static_cast<BacklightMode>(get(2));
        for (auto& pwm : decoded.pwm) {
            pwm.frequency_hz = static_cast<std::uint16_t>(get(15));
            pwm.minimum_percent = static_cast<std::uint8_t>(get(7));
            pwm.on_index = static_cast<std::uint8_t>(get(4));
            pwm.off_index = static_cast<std::uint8_t>(get(4));
            pwm.on_off = get(1) != 0;
        }
    }
    if (!valid(decoded)) {
        return false;
    }
    config = decoded;
    sequence = get32(record, 4);
    return true;
}

int newestRecord(const std::array<Record, 2>& records, Config& config,
                 std::uint32_t& sequence) noexcept {
    int selected = -1;
    for (int index = 0; index < 2; ++index) {
        Config candidate{};
        std::uint32_t candidate_sequence = 0;
        if (decodeRecord(records[static_cast<std::size_t>(index)], candidate, candidate_sequence) &&
            (selected < 0 ||
             (candidate_sequence != sequence &&
              static_cast<std::uint32_t>(candidate_sequence - sequence) < 0x80000000U))) {
            selected = index;
            config = candidate;
            sequence = candidate_sequence;
        }
    }
    return selected;
}
} // namespace local_control
