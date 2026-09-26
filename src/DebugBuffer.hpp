#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

// Pure model. The adapter serializes access with a non-waiting try-lock.
class DebugBuffer final {
  public:
    static constexpr std::size_t kCapacity = 32;
    static constexpr std::size_t kTextSize = 256;
    static constexpr std::uint32_t kSessionSeconds = 600;
    using Message = std::array<char, kTextSize>;
    void setEnabled(bool enabled, std::uint32_t now) noexcept {
        expire(now);
        if (enabled && !active_) {
            dropped_ = 0;
            head_ = 0;
            count_ = 0;
        }
        active_ = enabled;
        started_ = now;
        if (!enabled) {
            head_ = 0;
            count_ = 0;
        }
    }
    bool active(std::uint32_t now) noexcept {
        expire(now);
        return active_;
    }
    bool push(const char* text, std::uint32_t now) noexcept {
        if (!active(now) || text == nullptr || text[0] == '\0') {
            return false;
        }
        if (count_ == kCapacity) {
            head_ = (head_ + 1U) % kCapacity;
            --count_;
            noteDropped();
        }
        auto& message = messages_[(head_ + count_) % kCapacity];
        std::size_t length = 0;
        while (length + 1U < message.size() && text[length] != '\0') {
            message[length] = text[length];
            ++length;
        }
        message[length] = '\0';
        ++count_;
        return true;
    }
    bool pop(Message& message, std::uint32_t now) noexcept {
        if (!active(now) || count_ == 0U) {
            return false;
        }
        message = messages_[head_];
        head_ = (head_ + 1U) % kCapacity;
        --count_;
        return true;
    }
    void noteDropped() noexcept {
        if (dropped_ != std::numeric_limits<std::uint32_t>::max()) {
            ++dropped_;
        }
    }
    std::uint32_t dropped() const noexcept {
        return dropped_;
    }

  private:
    void expire(std::uint32_t now) noexcept {
        if (active_ && static_cast<std::uint32_t>(now - started_) >= kSessionSeconds) {
            active_ = false;
            head_ = 0;
            count_ = 0;
        }
    }
    std::array<Message, kCapacity> messages_{};
    std::size_t head_{0}, count_{0};
    std::uint32_t started_{0}, dropped_{0};
    bool active_{false};
};
