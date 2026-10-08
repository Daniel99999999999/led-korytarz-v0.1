#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Pure ring model. DebugLog serializes access with its non-waiting try-lock.
class WebLogBuffer final {
  public:
    static constexpr std::size_t kCapacity = 48;
    static constexpr std::size_t kTextSize = 256;

    struct Entry {
        std::uint32_t sequence{0};
        std::array<char, kTextSize> text{};
    };

    void push(const char* text) noexcept {
        if (text == nullptr || text[0] == '\0')
            return;
        if (count_ == kCapacity) {
            head_ = (head_ + 1U) % kCapacity;
            --count_;
        }
        auto& entry = entries_[(head_ + count_) % kCapacity];
        entry.sequence = ++latest_;
        const std::size_t length = std::min(std::strlen(text), entry.text.size() - 1U);
        std::memcpy(entry.text.data(), text, length);
        entry.text[length] = '\0';
        ++count_;
    }

    [[nodiscard]] bool nextAfter(std::uint32_t after, Entry& output, bool& gap) const noexcept {
        gap = false;
        if (count_ == 0U)
            return false;
        const auto first = entries_[head_].sequence;
        gap = after != 0U && after < first - 1U;
        for (std::size_t index = 0; index < count_; ++index) {
            const auto& entry = entries_[(head_ + index) % kCapacity];
            if (entry.sequence > after) {
                output = entry;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::uint32_t latest() const noexcept {
        return latest_;
    }

  private:
    std::array<Entry, kCapacity> entries_{};
    std::size_t head_{0};
    std::size_t count_{0};
    std::uint32_t latest_{0};
};
