// Exercise the real LCD/LED adapters against a scripted transport, not physical hardware.
#include "BoardConfig.hpp"
#include "DebugLog.hpp"
#include "LcdDisplay.hpp"
#include "LedChannel.hpp"
#include "esp_system.h"
#include "esp_timer.h"

#include <array>
#include <cstdio>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <process.h>
#include <windows.h>
#else
#include <thread>
#endif
#include <vector>

namespace {
unsigned failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            ++failures;                                                                            \
            std::printf("FAIL line %d: %s\n", __LINE__, #x);                                       \
        }                                                                                          \
    } while (false)
std::vector<std::vector<std::uint8_t>> writes;
std::array<std::uint8_t, 2> reads{};
unsigned read_index = 0;
int fail_read = -1;
int probe_result = ESP_OK;
int token = 0;
void answer(std::uint8_t value) {
    reads = {{static_cast<std::uint8_t>(value & 0xF0U), static_cast<std::uint8_t>(value << 4U)}};
    read_index = 0;
    fail_read = -1;
    writes.clear();
}
void initialize(LcdDisplay& lcd, bool backlight) {
    CHECK(lcd.initialize(&token, 0, backlight) == ESP_OK);
    for (unsigned now = 0; now < 2000 && !lcd.isReady(); ++now) {
        CHECK(lcd.service(now) == ESP_OK);
    }
    CHECK(lcd.isReady());
    CHECK(lcd.isBacklightOn() == backlight);
}
} // namespace
int i2c_master_probe(void*, std::uint16_t, int) {
    return probe_result;
}
int i2c_master_bus_add_device(void*, const i2c_device_config_t*, void** out) {
    *out = &token;
    return ESP_OK;
}
int i2c_master_bus_rm_device(void*) {
    return ESP_OK;
}
int i2c_master_bus_reset(void*) {
    return ESP_OK;
}
int i2c_master_transmit(void*, const std::uint8_t* data, std::size_t size, int) {
    writes.emplace_back(data, data + size);
    return ESP_OK;
}
int i2c_master_receive(void*, std::uint8_t* data, std::size_t size, int) {
    CHECK(size == 1);
    if (static_cast<int>(read_index) == fail_read) {
        ++read_index;
        return ESP_ERR_TIMEOUT;
    }
    CHECK(read_index < reads.size());
    *data = reads[read_index++ % reads.size()];
    return ESP_OK;
}

namespace {
std::atomic<unsigned> uartCalls{0};
int uartSink(const char*, va_list) {
    ++uartCalls;
    return 7;
}
int emitLog(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int result = fakeLogOutput(format, args);
    va_end(args);
    return result;
}
void produceLogs(unsigned worker) {
    for (unsigned entry = 0; entry < 1000; ++entry) {
        emitLog("worker %u:%u", worker, entry);
    }
}
#if defined(_WIN32)
unsigned __stdcall produceLogsWin(void* context) {
    produceLogs(*static_cast<unsigned*>(context));
    return 0;
}
#endif
void testDebugHook() {
    fakeLogOutput = uartSink;
    auto& logger = DebugLog::instance();
    logger.install();
    logger.install();
    DebugBuffer::Message message{};
    CHECK(emitLog("off %d", 1) == 7);
    CHECK(uartCalls == 1);
    CHECK(!logger.pop(message));
    CHECK(logger.setEnabled(true));
    CHECK(emitLog("\x1b[31mW (1) test: %d\x1b[0m\r\n", 42) == 7);
    CHECK(logger.pop(message));
    CHECK(std::strcmp(message.data(), "W (1) test: 42\n") == 0);
    logger.excludePublisherTask(fakeTask);
    emitLog("publisher recursion");
    CHECK(!logger.pop(message));
    logger.excludePublisherTask(nullptr);
    fakeIsr = true;
    emitLog("isr");
    fakeIsr = false;
    CHECK(!logger.pop(message));
    std::array<char, 512> long_line{};
    long_line.fill('a');
    long_line.back() = 0;
    emitLog("%s", long_line.data());
    CHECK(logger.pop(message));
    CHECK(std::strlen(message.data()) == 255);
    DebugLog::Status status{};
    CHECK(logger.status(status));
    CHECK(status.dropped == 1);
    logger.noteTransportDrop();
    CHECK(logger.status(status));
    CHECK(status.dropped == 2);
    fakeMicroseconds = 600'000'000;
    CHECK(logger.status(status));
    CHECK(!status.active);
    emitLog("expired");
    CHECK(!logger.pop(message));
    CHECK(logger.setEnabled(true));
    emitLog("pending");
    CHECK(logger.setEnabled(false));
    CHECK(!logger.pop(message));
    CHECK(logger.status(status));
    CHECK(!status.active);
    CHECK(std::strcmp(logger.resetReason(), "task_watchdog") == 0);
    fakeResetReason = ESP_RST_BROWNOUT;
    CHECK(std::strcmp(logger.resetReason(), "brownout") == 0);
    CHECK(uartCalls == 7);
    CHECK(logger.setEnabled(true));
#if defined(_WIN32)
    std::array<HANDLE, 4> workers{};
    std::array<unsigned, 4> identities{{0, 1, 2, 3}};
    for (unsigned worker = 0; worker < workers.size(); ++worker) {
        workers[worker] = reinterpret_cast<HANDLE>(
            _beginthreadex(nullptr, 0, produceLogsWin, &identities[worker], 0, nullptr));
        CHECK(workers[worker] != nullptr);
    }
#else
    std::array<std::thread, 4> workers;
    for (unsigned worker = 0; worker < workers.size(); ++worker) {
        workers[worker] = std::thread(produceLogs, worker);
    }
#endif
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        if (logger.pop(message)) {
            CHECK(std::strncmp(message.data(), "worker ", 7) == 0);
        }
        if (attempt % 100 == 0) {
            static_cast<void>(logger.setEnabled(false));
            static_cast<void>(logger.setEnabled(true));
        }
        static_cast<void>(logger.status(status));
    }
    for (auto& worker : workers) {
#if defined(_WIN32)
        CHECK(WaitForSingleObject(worker, 10000) == WAIT_OBJECT_0);
        CloseHandle(worker);
#else
        worker.join();
#endif
    }
    CHECK(logger.setEnabled(false));
    CHECK(logger.status(status));
    CHECK(!status.active);
    CHECK(!logger.pop(message));
    CHECK(uartCalls == 4007);
}
} // namespace

int main() {
    testDebugHook();
    LcdDisplay lcd;
    initialize(lcd, false);
    // No backlight flash in either initialization pass.
    for (const auto& transfer : writes) {
        for (const auto byte : transfer) {
            CHECK((byte & 8U) == 0U);
        }
    }
    LcdDashboard::Lines lines{};
    for (auto& line : lines) {
        line.fill(' ');
    }
    lines[0][0] = static_cast<char>(0xDF);
    CHECK(lcd.show(lines) == ESP_OK);
    // Complete two scans: DDRAM row offsets, high/low nibble order, wrap and degree symbol.
    for (unsigned index = 0; index < 160; ++index) {
        const unsigned pos = index % 80, row = pos / 20, col = pos % 20;
        answer(static_cast<std::uint8_t>(lines[row][col]));
        CHECK(lcd.verifyNextCharacter() == ESP_OK);
        CHECK(read_index == 2);
        CHECK(writes.size() == 4);
        const auto& command = writes[0];
        CHECK(command.size() == 6);
        const auto address =
            static_cast<std::uint8_t>(0x80U | (board_config::kLcdDdramRowOffsets[row] + col));
        CHECK((command[0] & 0xF0U) == (address & 0xF0U));
        CHECK((command[3] & 0xF0U) == ((address << 4U) & 0xF0U));
        CHECK(writes[1][0] == 0xF3U && writes[1][1] == 0xF7U);
        CHECK(writes[2] == writes[1]);
        CHECK(writes[3][0] == 0xF3U && writes[3][1] == 0xF0U);
    }
    answer('X');
    CHECK(lcd.verifyNextCharacter() == ESP_ERR_INVALID_CRC);
    // Both interrupted read phases must lower E and release RW.
    for (int failure = 0; failure < 2; ++failure) {
        answer(0xDF);
        fail_read = failure;
        CHECK(lcd.verifyNextCharacter() == ESP_ERR_TIMEOUT);
        CHECK(writes.back().back() == 0xF0U);
    }
    CHECK(lcd.reset() == ESP_OK);
    answer(0);
    CHECK(lcd.verifyNextCharacter() == ESP_OK);
    CHECK(writes.empty());
    bool present = true;
    probe_result = ESP_ERR_NOT_FOUND;
    CHECK(lcd.probe(&token, present) == ESP_OK);
    CHECK(!present);
    probe_result = ESP_ERR_TIMEOUT;
    CHECK(lcd.probe(&token, present) == ESP_ERR_TIMEOUT);

    CHECK(LedChannel::initializePeripheral() == ESP_OK);
    {
        LedChannel fourth("fourth", GPIO_NUM_32, LEDC_CHANNEL_3),
            fifth("fifth", GPIO_NUM_33, LEDC_CHANNEL_4);
        CHECK(fourth.initialize() == ESP_OK);
        CHECK(fifth.initialize() == ESP_OK);
        CHECK(fake_ledc::timers[3] == LEDC_TIMER_1);
        CHECK(fake_ledc::timers[4] == LEDC_TIMER_2);
        pwm_config::Config config{};
        config.frequency_hz = 1000;
        config.minimum_percent = 20;
        CHECK(fourth.configurePwm(config) == ESP_OK);
        CHECK(fake_ledc::frequencies[0] == 19531);
        CHECK(fake_ledc::frequencies[1] == 1000);
        CHECK(fake_ledc::frequencies[2] == 19531);
        CHECK(fake_ledc::duties[3] == 0); // configuring an OFF lamp never turns it on
        CHECK(fourth.setBrightness(1, 0, 0) == ESP_OK);
        CHECK(fourth.turnOn(0, 250) == ESP_OK);
        CHECK(fourth.update(5) == ESP_OK);
        CHECK(fake_ledc::duties[3] >= 819);
        CHECK(fourth.turnOff(6, 250) == ESP_OK);
        config.on_off = true;
        CHECK(fourth.configurePwm(config) == ESP_OK);
        CHECK(fake_ledc::duties[3] == 0);
        CHECK(fourth.turnOn(7, 250) == ESP_OK);
        CHECK(fake_ledc::duties[3] == 4096);
        CHECK(fourth.turnOff(8, 250) == ESP_OK);
        CHECK(fake_ledc::duties[3] == 0);
        fake_ledc::fail_frequency = true;
        config.frequency_hz = 500;
        CHECK(fourth.configurePwm(config) == ESP_FAIL);
        CHECK(fake_ledc::duties[3] == 0);
        CHECK(fourth.turnOn(9, 0) == ESP_FAIL);
        CHECK(fake_ledc::duties[4] == 0);
        fake_ledc::fail_frequency = false;

        CHECK(fifth.turnOn(10, 0) == ESP_OK);
        CHECK(fake_ledc::duties[4] > 0);
        fake_ledc::fail_stop = true;
        fake_gpio::levels[GPIO_NUM_33] = 1;
        fake_gpio::fail_set_level_once = true;
        config.frequency_hz = 500;
        CHECK(fifth.configurePwm(config) == ESP_FAIL);
        CHECK(fake_ledc::duties[4] > 0); // LEDC stop failed in the adapter
        CHECK(fake_gpio::gpio_output[GPIO_NUM_33]);
        CHECK(fake_gpio::levels[GPIO_NUM_33] == 0);
        CHECK(fifth.operationalStatus() == ESP_FAIL);
        CHECK(fifth.turnOn(11, 0) == ESP_FAIL);
        fake_ledc::fail_stop = false;
    }

    LedChannel led("test", GPIO_NUM_25, LEDC_CHANNEL_0);
    CHECK(led.prepareOutputLow() == ESP_OK);
    CHECK(led.initialize() == ESP_OK);
    CHECK(led.setBrightness(65, 0, 0) == ESP_OK);
    CHECK(led.turnOn(0, 250) == ESP_OK);
    for (unsigned t = 0; t <= 250; t += 5) {
        CHECK(led.update(t) == ESP_OK);
        CHECK(led.displayBrightness() == 65);
    }
    CHECK(led.turnOff(250, 250) == ESP_OK);
    for (unsigned t = 250; t <= 500; t += 5) {
        CHECK(led.update(t) == ESP_OK);
        CHECK(led.displayBrightness() == 65);
    }
    CHECK(led.turnOn(500, 0) == ESP_OK);
    CHECK(led.setBrightness(30, 500, 250) == ESP_OK);
    CHECK(led.update(625) == ESP_OK);
    CHECK(led.displayBrightness() > 30 && led.displayBrightness() < 65);
    CHECK(led.update(750) == ESP_OK);
    CHECK(led.displayBrightness() == 30);
    std::printf("Adapter checks: %s (%u failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
