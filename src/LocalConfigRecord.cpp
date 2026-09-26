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
    record[3] = 1;
    put32(record, 4, sequence);
    std::copy(config.button_masks.begin(), config.button_masks.end(), record.begin() + 8);
    for (std::size_t index = 0; index < kPirCount; ++index) {
        record[13 + index * 2] = static_cast<std::uint8_t>(config.off_delay_seconds[index]);
        record[14 + index * 2] = static_cast<std::uint8_t>(config.off_delay_seconds[index] >> 8U);
    }
    record[17] = static_cast<std::uint8_t>(config.lcd_override_seconds);
    record[18] = static_cast<std::uint8_t>(config.lcd_override_seconds >> 8U);
    record[19] = static_cast<std::uint8_t>(config.backlight_mode);
    put32(record, 23, crc(record));
    record[27] = kCommit;
    return record;
}

bool decodeRecord(const Record& record, Config& config, std::uint32_t& sequence) noexcept {
    if (record[0] != 'L' || record[1] != 'C' || record[2] != 'F' || record[3] != 1 ||
        record[27] != kCommit || get32(record, 23) != crc(record)) {
        return false;
    }
    Config decoded{};
    std::copy_n(record.begin() + 8, kChannelCount, decoded.button_masks.begin());
    for (std::size_t index = 0; index < kPirCount; ++index) {
        decoded.off_delay_seconds[index] = static_cast<std::uint16_t>(
            record[13 + index * 2] | (static_cast<unsigned>(record[14 + index * 2]) << 8U));
    }
    decoded.lcd_override_seconds =
        static_cast<std::uint16_t>(record[17] | (static_cast<unsigned>(record[18]) << 8U));
    decoded.backlight_mode = static_cast<BacklightMode>(record[19]);
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
