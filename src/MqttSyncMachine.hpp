#pragma once

#include <cstddef>
#include <cstdint>

class MqttSyncMachine final {
  public:
    static constexpr std::size_t kSubscriptionCount = 28;
    static constexpr std::uint32_t kSubackTimeoutMs = 5'000;
    // Longer than ESP-MQTT's default 30 s outbox expiry; avoid duplicate large QoS1 payloads.
    static constexpr std::uint32_t kPubackTimeoutMs = 35'000;

    enum class SubackResult : std::uint8_t { ignored, accepted, rejected };

    void onConnected() noexcept {
        *this = MqttSyncMachine{};
        birth_pending_ = true;
        discovery_pending_ = true;
    }

    void onBirth() noexcept {
        birth_pending_ = true;
        discovery_pending_ = true;
        discovery_queued_ = false;
        discovery_confirmed_ = false;
        discovery_id_ = 0;
        birth_retry_.reset();
        discovery_retry_.reset();
    }

    [[nodiscard]] bool canQueueBirth(const std::uint32_t now_ms) const noexcept {
        return birth_pending_ && birth_retry_.ready(now_ms);
    }

    void birthQueued() noexcept {
        birth_pending_ = false;
        birth_retry_.reset();
    }

    void birthFailed(const std::uint32_t now_ms) noexcept {
        birth_retry_.fail(now_ms, 2'000, 30'000);
    }

    [[nodiscard]] bool canSubscribe(const std::uint32_t now_ms) const noexcept {
        return subscription_index_ < kSubscriptionCount && pending_subscribe_id_ == 0 &&
               subscribe_retry_.ready(now_ms);
    }

    void subscribeSent(const int message_id, const std::uint32_t now_ms) noexcept {
        pending_subscribe_id_ = message_id;
        subscribe_sent_ms_ = now_ms;
    }

    void subscribeFailed(const std::uint32_t now_ms) noexcept {
        pending_subscribe_id_ = 0;
        subscribe_retry_.fail(now_ms, 2'000, 30'000);
    }

    [[nodiscard]] SubackResult onSuback(const int message_id, const bool accepted,
                                        const std::uint32_t now_ms) noexcept {
        if (message_id == 0 || message_id != pending_subscribe_id_) {
            return SubackResult::ignored;
        }
        pending_subscribe_id_ = 0;
        if (!accepted) {
            subscribeFailed(now_ms);
            return SubackResult::rejected;
        }
        ++subscription_index_;
        subscribe_retry_.reset();
        return SubackResult::accepted;
    }

    [[nodiscard]] bool pollSubackTimeout(const std::uint32_t now_ms) noexcept {
        if (pending_subscribe_id_ == 0 || now_ms - subscribe_sent_ms_ < kSubackTimeoutMs) {
            return false;
        }
        subscribeFailed(now_ms);
        return true;
    }

    [[nodiscard]] bool subscriptionsReady() const noexcept {
        return subscription_index_ == kSubscriptionCount;
    }

    [[nodiscard]] std::size_t subscriptionIndex() const noexcept {
        return subscription_index_;
    }

    [[nodiscard]] int pendingSubscribeId() const noexcept {
        return pending_subscribe_id_;
    }

    [[nodiscard]] bool canQueueDiscovery(const std::uint32_t now_ms) const noexcept {
        return subscriptionsReady() && !birth_pending_ && discovery_pending_ &&
               discovery_retry_.ready(now_ms);
    }

    void discoveryQueued(const int message_id, const std::uint32_t now_ms) noexcept {
        discovery_pending_ = false;
        discovery_queued_ = true;
        discovery_confirmed_ = false;
        discovery_id_ = message_id;
        discovery_sent_ms_ = now_ms;
    }

    void discoveryFailed(const std::uint32_t now_ms) noexcept {
        discovery_retry_.fail(now_ms, 5'000, 60'000);
    }

    [[nodiscard]] bool pollPubackTimeout(const std::uint32_t now_ms) noexcept {
        if (!discovery_queued_ || discovery_confirmed_ ||
            now_ms - discovery_sent_ms_ < kPubackTimeoutMs)
            return false;
        discovery_queued_ = false;
        discovery_pending_ = true;
        discovery_retry_.fail(now_ms, 5'000, 60'000);
        return true;
    }

    [[nodiscard]] bool confirmDiscovery(const int message_id) noexcept {
        if ((!discovery_queued_ && !discovery_pending_) || discovery_confirmed_ ||
            message_id == 0 || message_id != discovery_id_) {
            return false;
        }
        discovery_pending_ = false;
        discovery_queued_ = true;
        discovery_confirmed_ = true;
        discovery_retry_.reset();
        return true;
    }

    [[nodiscard]] bool discoveryQueued() const noexcept {
        return discovery_queued_;
    }

    [[nodiscard]] bool discoveryConfirmed() const noexcept {
        return discovery_confirmed_;
    }

    [[nodiscard]] int discoveryMessageId() const noexcept {
        return discovery_id_;
    }

  private:
    class RetryGate final {
      public:
        [[nodiscard]] bool ready(const std::uint32_t now_ms) const noexcept {
            return delay_ms_ == 0 || now_ms - failed_at_ms_ >= delay_ms_;
        }

        void fail(const std::uint32_t now_ms, const std::uint32_t first_ms,
                  const std::uint32_t maximum_ms) noexcept {
            delay_ms_ = delay_ms_ == 0 ? first_ms
                                       : (delay_ms_ >= maximum_ms / 2 ? maximum_ms : delay_ms_ * 2);
            failed_at_ms_ = now_ms;
        }

        void reset() noexcept {
            delay_ms_ = 0;
        }

      private:
        std::uint32_t failed_at_ms_{0};
        std::uint32_t delay_ms_{0};
    };

    RetryGate subscribe_retry_{};
    RetryGate birth_retry_{};
    RetryGate discovery_retry_{};
    std::size_t subscription_index_{0};
    int pending_subscribe_id_{0};
    int discovery_id_{0};
    std::uint32_t subscribe_sent_ms_{0};
    std::uint32_t discovery_sent_ms_{0};
    bool birth_pending_{false};
    bool discovery_pending_{false};
    bool discovery_queued_{false};
    bool discovery_confirmed_{false};
};
