#pragma once

#include "FeatureConfig.hpp"

#if LED_CONTROLLER_AT24C08_ENABLED

#include <array>
#include <cstddef>
#include <cstdint>

class SecureSettingsRecord final {
  public:
    static constexpr std::size_t kEepromSize = 1'024;
    static constexpr std::size_t kSlotCount = 2;
    static constexpr std::size_t kSlotSize = kEepromSize / kSlotCount;
    static constexpr std::size_t kHeaderSize = 48;
    static constexpr std::size_t kNonceSize = 12;
    static constexpr std::size_t kTagSize = 16;
    static constexpr std::size_t kMaxPayloadSize = kSlotSize - kHeaderSize;
    static constexpr std::size_t kKeySize = 32;

    static constexpr std::uint8_t kInvalidMarker = 0x00;
    static constexpr std::uint8_t kCommittedMarker = 0xA5;
    static constexpr std::uint8_t kFormatVersion = 1;
    static constexpr std::uint8_t kAlgorithmAes256Gcm = 1;

    using Slot = std::array<std::uint8_t, kSlotSize>;
    using Key = std::array<std::uint8_t, kKeySize>;
    using Nonce = std::array<std::uint8_t, kNonceSize>;
    using Payload = std::array<std::uint8_t, kMaxPayloadSize>;

    struct Metadata {
        std::uint32_t sequence{0};
        std::uint32_t key_generation{0};
        std::uint16_t payload_size{0};
        Nonce nonce{};
    };

    enum class SlotSelection : std::int8_t {
        none = -1,
        first = 0,
        second = 1,
    };

    static bool prepareStaged(Slot& slot, std::uint32_t sequence, std::uint32_t key_generation,
                              std::size_t payload_size, const Nonce& nonce) noexcept;
    static bool inspect(const Slot& slot, bool require_committed, Metadata& metadata) noexcept;
    static void markCommitted(Slot& slot) noexcept;
    static void markInvalid(Slot& slot) noexcept;

    [[nodiscard]] static bool sequenceIsNewer(std::uint32_t candidate,
                                              std::uint32_t reference) noexcept;
    [[nodiscard]] static SlotSelection chooseNewest(bool first_authenticated,
                                                    std::uint32_t first_sequence,
                                                    bool second_authenticated,
                                                    std::uint32_t second_sequence) noexcept;

    [[nodiscard]] static const std::uint8_t* aadData(const Slot& slot) noexcept;
    [[nodiscard]] static std::size_t aadSize() noexcept;
    [[nodiscard]] static std::uint8_t* tagData(Slot& slot) noexcept;
    [[nodiscard]] static const std::uint8_t* tagData(const Slot& slot) noexcept;
    [[nodiscard]] static std::uint8_t* ciphertextData(Slot& slot) noexcept;
    [[nodiscard]] static const std::uint8_t* ciphertextData(const Slot& slot) noexcept;

  private:
    static constexpr std::size_t kCommitOffset = 0;
    static constexpr std::size_t kMagicOffset = 1;
    static constexpr std::size_t kFormatOffset = 5;
    static constexpr std::size_t kAlgorithmOffset = 6;
    static constexpr std::size_t kHeaderSizeOffset = 7;
    static constexpr std::size_t kSequenceOffset = 8;
    static constexpr std::size_t kKeyGenerationOffset = 12;
    static constexpr std::size_t kPayloadSizeOffset = 16;
    static constexpr std::size_t kReservedOffset = 18;
    static constexpr std::size_t kNonceOffset = 20;
    static constexpr std::size_t kTagOffset = 32;
    static constexpr std::size_t kCiphertextOffset = kHeaderSize;
    static constexpr std::size_t kAadOffset = kMagicOffset;
    static constexpr std::size_t kAadSize = kTagOffset - kAadOffset;
};

static_assert(SecureSettingsRecord::kSlotSize == 512U,
              "AT24C08N musi byc podzielony na dwa sloty po 512 B");
static_assert(SecureSettingsRecord::kMaxPayloadSize == 464U,
              "Naglowek AES-GCM powinien pozostawiac 464 B danych");

#endif
