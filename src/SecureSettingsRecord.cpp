#include "FeatureConfig.hpp"

#if LED_CONTROLLER_AT24C08_ENABLED

#include "SecureSettingsRecord.hpp"

#include <algorithm>

namespace {
constexpr std::array<std::uint8_t, 4> kMagic{{'L', 'K', 'C', '1'}};

void writeUint16(std::uint8_t* const destination, const std::uint16_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void writeUint32(std::uint8_t* const destination, const std::uint32_t value) noexcept {
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    destination[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

[[nodiscard]] std::uint16_t readUint16(const std::uint8_t* const source) noexcept {
    return static_cast<std::uint16_t>(source[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(source[1]) << 8U);
}

[[nodiscard]] std::uint32_t readUint32(const std::uint8_t* const source) noexcept {
    return static_cast<std::uint32_t>(source[0]) | (static_cast<std::uint32_t>(source[1]) << 8U) |
           (static_cast<std::uint32_t>(source[2]) << 16U) |
           (static_cast<std::uint32_t>(source[3]) << 24U);
}
} // namespace

bool SecureSettingsRecord::prepareStaged(Slot& slot, const std::uint32_t sequence,
                                         const std::uint32_t key_generation,
                                         const std::size_t payload_size,
                                         const Nonce& nonce) noexcept {
    if (payload_size > kMaxPayloadSize) {
        return false;
    }

    slot.fill(0);
    slot[kCommitOffset] = kInvalidMarker;
    std::copy(kMagic.begin(), kMagic.end(), slot.begin() + kMagicOffset);
    slot[kFormatOffset] = kFormatVersion;
    slot[kAlgorithmOffset] = kAlgorithmAes256Gcm;
    slot[kHeaderSizeOffset] = static_cast<std::uint8_t>(kHeaderSize);
    writeUint32(slot.data() + kSequenceOffset, sequence);
    writeUint32(slot.data() + kKeyGenerationOffset, key_generation);
    writeUint16(slot.data() + kPayloadSizeOffset, static_cast<std::uint16_t>(payload_size));
    slot[kReservedOffset] = 0;
    slot[kReservedOffset + 1U] = 0;
    std::copy(nonce.begin(), nonce.end(), slot.begin() + kNonceOffset);
    return true;
}

bool SecureSettingsRecord::inspect(const Slot& slot, const bool require_committed,
                                   Metadata& metadata) noexcept {
    const std::uint8_t marker = slot[kCommitOffset];
    if (require_committed) {
        if (marker != kCommittedMarker) {
            return false;
        }
    } else if ((marker != kInvalidMarker) && (marker != kCommittedMarker)) {
        return false;
    }

    if (!std::equal(kMagic.begin(), kMagic.end(), slot.begin() + kMagicOffset) ||
        (slot[kFormatOffset] != kFormatVersion) ||
        (slot[kAlgorithmOffset] != kAlgorithmAes256Gcm) ||
        (slot[kHeaderSizeOffset] != static_cast<std::uint8_t>(kHeaderSize)) ||
        (slot[kReservedOffset] != 0U) || (slot[kReservedOffset + 1U] != 0U)) {
        return false;
    }

    const std::uint16_t payload_size = readUint16(slot.data() + kPayloadSizeOffset);
    if (payload_size > kMaxPayloadSize) {
        return false;
    }

    metadata.sequence = readUint32(slot.data() + kSequenceOffset);
    metadata.key_generation = readUint32(slot.data() + kKeyGenerationOffset);
    metadata.payload_size = payload_size;
    std::copy_n(slot.begin() + kNonceOffset, kNonceSize, metadata.nonce.begin());
    return true;
}

void SecureSettingsRecord::markCommitted(Slot& slot) noexcept {
    slot[kCommitOffset] = kCommittedMarker;
}

void SecureSettingsRecord::markInvalid(Slot& slot) noexcept {
    slot[kCommitOffset] = kInvalidMarker;
}

bool SecureSettingsRecord::sequenceIsNewer(const std::uint32_t candidate,
                                           const std::uint32_t reference) noexcept {
    const std::uint32_t difference = candidate - reference;
    return (difference != 0U) && (difference < 0x8000'0000U);
}

SecureSettingsRecord::SlotSelection SecureSettingsRecord::chooseNewest(
    const bool first_authenticated, const std::uint32_t first_sequence,
    const bool second_authenticated, const std::uint32_t second_sequence) noexcept {
    if (!first_authenticated && !second_authenticated) {
        return SlotSelection::none;
    }
    if (!second_authenticated) {
        return SlotSelection::first;
    }
    if (!first_authenticated) {
        return SlotSelection::second;
    }
    return sequenceIsNewer(second_sequence, first_sequence) ? SlotSelection::second
                                                            : SlotSelection::first;
}

const std::uint8_t* SecureSettingsRecord::aadData(const Slot& slot) noexcept {
    return slot.data() + kAadOffset;
}

std::size_t SecureSettingsRecord::aadSize() noexcept {
    return kAadSize;
}

std::uint8_t* SecureSettingsRecord::tagData(Slot& slot) noexcept {
    return slot.data() + kTagOffset;
}

const std::uint8_t* SecureSettingsRecord::tagData(const Slot& slot) noexcept {
    return slot.data() + kTagOffset;
}

std::uint8_t* SecureSettingsRecord::ciphertextData(Slot& slot) noexcept {
    return slot.data() + kCiphertextOffset;
}

const std::uint8_t* SecureSettingsRecord::ciphertextData(const Slot& slot) noexcept {
    return slot.data() + kCiphertextOffset;
}

#endif
