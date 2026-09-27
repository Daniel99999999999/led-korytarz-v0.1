#include "BacklightPolicy.hpp"
#include "ButtonDebouncer.hpp"
#include "ButtonGesture.hpp"
#include "ChannelState.hpp"
#include "DebugBuffer.hpp"
#include "DebugDiscovery.hpp"
#include "DeviceIdentity.hpp"
#include "DutyTransition.hpp"
#include "Ina228Scaling.hpp"
#include "IrRemoteMapper.hpp"
#include "LcdDashboard.hpp"
#include "LocalConfigRecord.hpp"
#include "LocalControlDiscovery.hpp"
#include "LocalOtaValidation.hpp"
#include "MqttCommandParser.hpp"
#include "MqttPublishTracker.hpp"
#include "NecDecoder.hpp"
#include "OtaRequestValidator.hpp"
#include "PersistentSettings.hpp"
#include "PirAutomation.hpp"
#include "SecureSettingsRecord.hpp"
#include "StatusIndicator.hpp"
#include "TemperatureModel.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
unsigned failure_count = 0;

void expect(const bool condition, const char* const expression, const int line) noexcept {
    if (condition) {
        return;
    }

    std::printf("FAIL line %d: %s\n", line, expression);
    ++failure_count;
}

#define EXPECT_TRUE(expression) expect((expression), #expression, __LINE__)
#define EXPECT_EQ(expected, actual)                                                                \
    expect(((expected) == (actual)), #expected " == " #actual, __LINE__)

void expectLine(const LcdDashboard::Line& actual, const char* const expected,
                const int line) noexcept {
    bool equal = true;
    for (std::size_t index = 0; index < LcdDashboard::kColumnCount; ++index) {
        const char expected_character = (expected[index] == '\0') ? ' ' : expected[index];
        if (actual[index] != expected_character) {
            equal = false;
            break;
        }
        if (expected[index] == '\0') {
            for (std::size_t padding = index + 1U; padding < LcdDashboard::kColumnCount;
                 ++padding) {
                if (actual[padding] != ' ') {
                    equal = false;
                    break;
                }
            }
            break;
        }
    }
    expect(equal, expected, line);
}

#define EXPECT_LINE(expected, actual) expectLine((actual), (expected), __LINE__)

std::uint64_t makeDs18b20Rom(const std::uint8_t serial_seed) noexcept {
    std::array<std::uint8_t, temperature_model::kRomSize> bytes{{
        temperature_model::kDs18b20FamilyCode,
        serial_seed,
        static_cast<std::uint8_t>(serial_seed + 1U),
        static_cast<std::uint8_t>(serial_seed + 2U),
        static_cast<std::uint8_t>(serial_seed + 3U),
        static_cast<std::uint8_t>(serial_seed + 4U),
        static_cast<std::uint8_t>(serial_seed + 5U),
        0U,
    }};
    bytes.back() = temperature_model::dallasCrc8(bytes.data(), bytes.size() - 1U);
    std::uint64_t rom_id = 0U;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        rom_id |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    }
    return rom_id;
}

struct NecPulseSequence final {
    std::array<nec_remote::Pulse, 67> pulses{};
    std::size_t count{0};
};

void appendPulse(NecPulseSequence& sequence, const std::uint32_t duration_us,
                 const bool level_high) noexcept {
    sequence.pulses[sequence.count++] = nec_remote::Pulse{duration_us, level_high};
}

NecPulseSequence makeNecCommand(const std::uint8_t address_first, const std::uint8_t address_second,
                                const std::uint8_t command,
                                const std::uint8_t command_inverse) noexcept {
    NecPulseSequence sequence{};
    appendPulse(sequence, 9'000, false);
    appendPulse(sequence, 4'500, true);

    const std::array<std::uint8_t, 4> bytes{
        {address_first, address_second, command, command_inverse}};
    for (const std::uint8_t byte : bytes) {
        for (std::uint8_t bit = 0; bit < 8U; ++bit) {
            appendPulse(sequence, 560, false);
            appendPulse(sequence, ((byte & (1U << bit)) != 0U) ? 1'690U : 560U, true);
        }
    }
    appendPulse(sequence, 560, false);
    return sequence;
}

nec_remote::Frame makeRemoteFrame(const IrRemoteMapper::Command command) noexcept {
    return nec_remote::Frame{nec_remote::FrameType::command, IrRemoteMapper::kAddressFirst,
                             IrRemoteMapper::kAddressSecond, static_cast<std::uint8_t>(command),
                             static_cast<std::uint8_t>(~static_cast<std::uint8_t>(command))};
}

void testNecDecoderCommand() noexcept {
    const NecPulseSequence sequence = makeNecCommand(0x87, 0x5A, 0x45, 0xBA);
    const nec_remote::Frame frame = nec_remote::decode(sequence.pulses.data(), sequence.count);

    EXPECT_EQ(nec_remote::FrameType::command, frame.type);
    EXPECT_EQ(0x87U, frame.address_first);
    EXPECT_EQ(0x5AU, frame.address_second);
    EXPECT_EQ(0x45U, frame.command);
    EXPECT_EQ(0xBAU, frame.command_inverse);
}

void testNecDecoderRepeatAndInvalidTiming() noexcept {
    const std::array<nec_remote::Pulse, 3> repeat{{
        {9'000, false},
        {2'250, true},
        {560, false},
    }};
    EXPECT_EQ(nec_remote::FrameType::repeat, nec_remote::decode(repeat.data(), repeat.size()).type);

    NecPulseSequence malformed = makeNecCommand(0x87, 0x5A, 0x16, 0xE9);
    malformed.pulses[3].duration_us = 1'000;
    EXPECT_EQ(nec_remote::FrameType::invalid,
              nec_remote::decode(malformed.pulses.data(), malformed.count).type);
}

void testIrRemoteRoleSelection() noexcept {
    IrRemoteMapper mapper;
    nec_remote::Frame invalid = makeRemoteFrame(IrRemoteMapper::Command::power);
    invalid.address_first = 0x00;
    EXPECT_EQ(IrRemoteAction::Type::none, mapper.process(invalid, 10).type);
    invalid = makeRemoteFrame(IrRemoteMapper::Command::power);
    invalid.command_inverse = invalid.command;
    EXPECT_EQ(IrRemoteAction::Type::none, mapper.process(invalid, 20).type);

    EXPECT_EQ(IrRemoteAction::Type::none,
              mapper.process(makeRemoteFrame(IrRemoteMapper::Command::power), 100).type);

    IrRemoteAction action =
        mapper.process(makeRemoteFrame(IrRemoteMapper::Command::select_white_controller), 200);
    EXPECT_EQ(IrRemoteAction::Type::white_role_selected, action.type);
    EXPECT_EQ(IrRemoteMapper::Role::white_controller, mapper.activeRole());

    action = mapper.process(makeRemoteFrame(IrRemoteMapper::Command::power), 300);
    EXPECT_EQ(IrRemoteAction::Type::master_power, action.type);
    EXPECT_EQ(IrRemoteAction::kAllChannelsTarget, action.target);

    action = mapper.process(makeRemoteFrame(IrRemoteMapper::Command::select_rgb_controller), 400);
    EXPECT_EQ(IrRemoteAction::Type::rgb_role_selected, action.type);
    EXPECT_EQ(IrRemoteMapper::Role::rgb_controller, mapper.activeRole());
    EXPECT_EQ(IrRemoteAction::Type::none,
              mapper.process(makeRemoteFrame(IrRemoteMapper::Command::power), 500).type);
}

void testIrRemoteTargetAndTransitionSelection() noexcept {
    IrRemoteMapper mapper;
    static_cast<void>(
        mapper.process(makeRemoteFrame(IrRemoteMapper::Command::select_white_controller), 0));

    IrRemoteAction action =
        mapper.process(makeRemoteFrame(IrRemoteMapper::Command::target_previous), 100);
    EXPECT_EQ(IrRemoteAction::Type::target_changed, action.type);
    EXPECT_EQ(IrRemoteAction::kAllChannelsTarget, action.target);

    action = mapper.process(makeRemoteFrame(IrRemoteMapper::Command::target_next), 200);
    EXPECT_EQ(0U, action.target);
    EXPECT_EQ(500U, mapper.transitionMs());

    action = mapper.process(makeRemoteFrame(IrRemoteMapper::Command::cycle_transition), 300);
    EXPECT_EQ(IrRemoteAction::Type::transition_changed, action.type);
    EXPECT_EQ(2'000U, action.transition_ms);
    action = mapper.process(makeRemoteFrame(IrRemoteMapper::Command::cycle_transition), 400);
    EXPECT_EQ(0U, action.transition_ms);
}

void testIrRemoteRepeatPolicy() noexcept {
    IrRemoteMapper mapper;
    static_cast<void>(
        mapper.process(makeRemoteFrame(IrRemoteMapper::Command::select_white_controller), 0));

    IrRemoteAction action =
        mapper.process(makeRemoteFrame(IrRemoteMapper::Command::brightness_up), 1'000);
    EXPECT_EQ(IrRemoteAction::Type::brightness_up, action.type);
    EXPECT_TRUE(!action.repeated);

    const nec_remote::Frame repeat{nec_remote::FrameType::repeat, 0, 0, 0, 0};
    EXPECT_EQ(IrRemoteAction::Type::none, mapper.process(repeat, 1'110).type);
    EXPECT_EQ(IrRemoteAction::Type::none, mapper.process(repeat, 1'220).type);
    EXPECT_EQ(IrRemoteAction::Type::none, mapper.process(repeat, 1'330).type);
    EXPECT_EQ(IrRemoteAction::Type::none, mapper.process(repeat, 1'440).type);

    action = mapper.process(repeat, 1'550);
    EXPECT_EQ(IrRemoteAction::Type::brightness_up, action.type);
    EXPECT_TRUE(action.repeated);

    EXPECT_EQ(IrRemoteAction::Type::none, mapper.process(repeat, 1'900).type);
    static_cast<void>(mapper.process(makeRemoteFrame(IrRemoteMapper::Command::power), 2'000));
    EXPECT_EQ(IrRemoteAction::Type::none, mapper.process(repeat, 2'100).type);
}

void testSinglePressAndRelease() noexcept {
    ButtonDebouncer debouncer(25);
    debouncer.reset(false, 0);

    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(true, 10));
    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(true, 34));
    EXPECT_EQ(ButtonDebouncer::Event::pressed, debouncer.update(true, 35));
    EXPECT_TRUE(debouncer.isPressed());
    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(false, 40));
    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(false, 64));
    EXPECT_EQ(ButtonDebouncer::Event::released, debouncer.update(false, 65));
    EXPECT_TRUE(!debouncer.isPressed());
}

void testBounceShorterThanThreshold() noexcept {
    ButtonDebouncer debouncer(25);
    debouncer.reset(false, 0);

    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(true, 5));
    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(false, 10));
    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(true, 15));
    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(false, 20));
    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(true, 30));
    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(true, 54));
    EXPECT_EQ(ButtonDebouncer::Event::pressed, debouncer.update(true, 55));
}

void testPirActiveHighContactDebounce() noexcept {
    ButtonDebouncer input(25);
    input.reset(false, 0); // LOW: zamkniety styk NC, stan normalny.

    EXPECT_EQ(ButtonDebouncer::Event::none, input.update(true, 10));
    EXPECT_EQ(ButtonDebouncer::Event::none, input.update(false, 15));
    EXPECT_EQ(ButtonDebouncer::Event::none, input.update(true, 20));
    EXPECT_EQ(ButtonDebouncer::Event::none, input.update(true, 44));
    EXPECT_EQ(ButtonDebouncer::Event::pressed, input.update(true, 45));
    EXPECT_TRUE(input.isPressed());

    EXPECT_EQ(ButtonDebouncer::Event::none, input.update(false, 50));
    EXPECT_EQ(ButtonDebouncer::Event::released, input.update(false, 75));
    EXPECT_TRUE(!input.isPressed());
}

void testHeldButtonAtStartup() noexcept {
    ButtonDebouncer debouncer(25);
    debouncer.reset(true, 100);

    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(true, 200));
    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(false, 201));
    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(false, 225));
    EXPECT_EQ(ButtonDebouncer::Event::released, debouncer.update(false, 226));
    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(true, 300));
    EXPECT_EQ(ButtonDebouncer::Event::pressed, debouncer.update(true, 325));
}

void testTimeCounterOverflow() noexcept {
    constexpr std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max();
    ButtonDebouncer debouncer(25);
    debouncer.reset(false, maximum - 100U);

    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(true, maximum - 10U));
    EXPECT_EQ(ButtonDebouncer::Event::none, debouncer.update(true, 13));
    EXPECT_EQ(ButtonDebouncer::Event::pressed, debouncer.update(true, 14));
}

void testChannelToggle() noexcept {
    ChannelState state(100);

    EXPECT_TRUE(!state.isOn());
    EXPECT_EQ(100U, state.brightness());
    EXPECT_EQ(0U, state.targetDuty(4095));

    state.toggle();
    EXPECT_TRUE(state.isOn());
    EXPECT_EQ(4095U, state.targetDuty(4095));

    state.toggle();
    EXPECT_TRUE(!state.isOn());
    EXPECT_EQ(0U, state.targetDuty(4095));
}

void testBrightnessWhileOff() noexcept {
    ChannelState state(100);

    state.setBrightness(50);
    EXPECT_EQ(50U, state.brightness());
    EXPECT_EQ(0U, state.targetDuty(4095));

    state.turnOn();
    EXPECT_EQ(2048U, state.targetDuty(4095));

    state.turnOff();
    state.setBrightness(0);
    EXPECT_EQ(1U, state.brightness());
    state.turnOn();
    EXPECT_EQ(41U, state.targetDuty(4095));

    state.setBrightness(255);
    EXPECT_EQ(100U, state.brightness());
    EXPECT_EQ(4095U, state.targetDuty(4095));
}

void testShortPressGesture() noexcept {
    ButtonGesture gesture(500, 40);
    gesture.reset(false, 0);

    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, 100));
    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, 598));
    EXPECT_EQ(ButtonGesture::Event::short_press, gesture.update(false, 599));

    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, 700));
    EXPECT_EQ(ButtonGesture::Event::short_press, gesture.update(false, 750));
}

void testLongPressGesture() noexcept {
    ButtonGesture gesture(500, 40);
    gesture.reset(false, 0);

    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, 100));
    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, 599));
    EXPECT_EQ(ButtonGesture::Event::long_press_started, gesture.update(true, 600));
    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, 639));
    EXPECT_EQ(ButtonGesture::Event::long_press_step, gesture.update(true, 640));
    EXPECT_EQ(ButtonGesture::Event::long_press_step, gesture.update(true, 680));
    EXPECT_EQ(ButtonGesture::Event::long_press_ended, gesture.update(false, 700));
    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(false, 701));
}

void testGestureHeldAtStartup() noexcept {
    ButtonGesture gesture(500, 40);
    gesture.reset(true, 100);

    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, 1'000));
    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(false, 1'001));
    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, 1'100));
    EXPECT_EQ(ButtonGesture::Event::short_press, gesture.update(false, 1'200));
}

void testGestureTimeOverflow() noexcept {
    constexpr std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max();
    ButtonGesture gesture(500, 40);
    gesture.reset(false, maximum - 300U);

    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, maximum - 200U));
    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, 298));
    EXPECT_EQ(ButtonGesture::Event::long_press_started, gesture.update(true, 299));
    EXPECT_EQ(ButtonGesture::Event::long_press_step, gesture.update(true, 339));
}

void testDimmingFromOff() noexcept {
    ChannelState state(100);

    EXPECT_TRUE(state.beginDimming());
    EXPECT_TRUE(state.isOn());
    EXPECT_TRUE(state.isDimming());
    EXPECT_EQ(1U, state.brightness());
    EXPECT_EQ(ChannelState::DimmingDirection::brighter, state.dimmingDirection());

    EXPECT_TRUE(state.stepDimming());
    EXPECT_EQ(2U, state.brightness());
    state.endDimming();
    EXPECT_TRUE(!state.isDimming());
}

void testDimmingAlternatesAtLimits() noexcept {
    ChannelState state(100);
    state.turnOn();

    EXPECT_TRUE(state.beginDimming());
    EXPECT_EQ(ChannelState::DimmingDirection::dimmer, state.dimmingDirection());
    EXPECT_EQ(99U, state.brightness());

    for (unsigned step = 0; step < 98U; ++step) {
        EXPECT_TRUE(state.stepDimming());
    }
    EXPECT_EQ(1U, state.brightness());
    EXPECT_TRUE(!state.stepDimming());
    state.endDimming();

    EXPECT_TRUE(state.beginDimming());
    EXPECT_EQ(ChannelState::DimmingDirection::brighter, state.dimmingDirection());
    EXPECT_EQ(2U, state.brightness());
}

void testLinearDutyTransition() noexcept {
    DutyTransition transition;
    transition.reset(0, 100);

    EXPECT_TRUE(!transition.retarget(1'000, 100, 100));
    EXPECT_TRUE(transition.isActive());
    EXPECT_TRUE(transition.update(149));
    EXPECT_EQ(490U, transition.currentDuty());
    EXPECT_TRUE(transition.update(150));
    EXPECT_EQ(500U, transition.currentDuty());
    EXPECT_TRUE(transition.update(200));
    EXPECT_EQ(1'000U, transition.currentDuty());
    EXPECT_TRUE(!transition.isActive());
}

void testDutyTransitionRetarget() noexcept {
    DutyTransition transition;
    transition.reset(0, 0);

    EXPECT_TRUE(!transition.retarget(1'000, 100, 0));
    EXPECT_TRUE(transition.retarget(0, 50, 50));
    EXPECT_EQ(500U, transition.currentDuty());
    EXPECT_EQ(0U, transition.targetDuty());
    EXPECT_TRUE(transition.update(75));
    EXPECT_EQ(250U, transition.currentDuty());
    EXPECT_TRUE(transition.update(100));
    EXPECT_EQ(0U, transition.currentDuty());
}

void testDutyTransitionTimeOverflow() noexcept {
    constexpr std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max();
    DutyTransition transition;
    transition.reset(0, maximum - 10U);

    EXPECT_TRUE(!transition.retarget(100, 20, maximum - 10U));
    EXPECT_TRUE(transition.update(maximum));
    EXPECT_EQ(50U, transition.currentDuty());
    EXPECT_TRUE(transition.update(9));
    EXPECT_EQ(100U, transition.currentDuty());
    EXPECT_TRUE(!transition.isActive());
}

void testStatusIndicatorHeartbeat() noexcept {
    StatusIndicator indicator(500);
    indicator.reset(false, 100);

    EXPECT_TRUE(indicator.isIlluminated());
    EXPECT_TRUE(!indicator.isFaultLatched());
    EXPECT_TRUE(!indicator.update(599));
    EXPECT_TRUE(indicator.update(600));
    EXPECT_TRUE(!indicator.isIlluminated());
    EXPECT_TRUE(!indicator.update(1'099));
    EXPECT_TRUE(indicator.update(1'100));
    EXPECT_TRUE(indicator.isIlluminated());
}

void testStatusIndicatorFaultLifecycle() noexcept {
    StatusIndicator indicator(500);
    indicator.reset(false, 0);

    EXPECT_TRUE(indicator.update(500));
    EXPECT_TRUE(!indicator.isIlluminated());

    indicator.latchFault();
    EXPECT_TRUE(indicator.isFaultLatched());
    EXPECT_TRUE(indicator.isIlluminated());
    EXPECT_TRUE(!indicator.update(10'000));
    EXPECT_TRUE(indicator.isIlluminated());

    indicator.clearFault(10'000);
    EXPECT_TRUE(!indicator.isFaultLatched());
    EXPECT_TRUE(indicator.isIlluminated());
    EXPECT_TRUE(!indicator.update(10'499));
    EXPECT_TRUE(indicator.update(10'500));
    EXPECT_TRUE(!indicator.isIlluminated());
}

void testLcdDashboardLayout() noexcept {
    const std::array<LcdDashboard::ChannelSnapshot, LcdDashboard::kChannelCount> channels{{
        {false, 100},
        {true, 61},
        {true, 1},
        {false, 42},
        {true, 100},
    }};

    const LcdDashboard::NetworkSnapshot network{};
    const LcdDashboard::MqttSnapshot mqtt{};
    const LcdDashboard::Lines lines =
        LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network, mqtt,
                             LcdDashboard::InformationPage::status);
    EXPECT_LINE("1:OFF100%  2:ON  61%", lines[0]);
    EXPECT_LINE("3:ON   1%  4:OFF 42%", lines[1]);
    EXPECT_LINE("5:ON 100%", lines[2]);
    EXPECT_LINE("LOKALNIE: OK", lines[3]);
}

void testLcdDashboardMeasurementField() noexcept {
    const std::array<LcdDashboard::ChannelSnapshot, LcdDashboard::kChannelCount> channels{{
        {false, 100},
        {false, 100},
        {false, 100},
        {false, 100},
        {true, 100},
    }};
    const LcdDashboard::NetworkSnapshot network{};
    const LcdDashboard::MqttSnapshot mqtt{};
    LcdDashboard::MeasurementSnapshot measurement{};
    measurement.current_supported = true;
    measurement.current_available = true;
    measurement.current_milliamps = 9'375;

    LcdDashboard::Lines lines = LcdDashboard::render(
        channels, LcdDashboard::Status::ok, 0, network, mqtt, LcdDashboard::InformationPage::status,
        LcdDashboard::NoticeSnapshot{}, measurement, LcdDashboard::MeasurementPage::current);
    EXPECT_LINE("5:ON 100% I: 9375mA", lines[2]);

    measurement.radiator_configured = true;
    measurement.radiator_temperature_available = true;
    measurement.radiator_temperature_millicelsius = 25'000;
    lines =
        LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network, mqtt,
                             LcdDashboard::InformationPage::status, LcdDashboard::NoticeSnapshot{},
                             measurement, LcdDashboard::MeasurementPage::radiator_temperature);
    EXPECT_LINE("5:ON 100% R: 25.0\xDF"
                "C",
                lines[2]);

    measurement.radiator_temperature_millicelsius = -500;
    lines =
        LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network, mqtt,
                             LcdDashboard::InformationPage::status, LcdDashboard::NoticeSnapshot{},
                             measurement, LcdDashboard::MeasurementPage::radiator_temperature);
    EXPECT_LINE("5:ON 100% R:-0.5\xDF"
                "C",
                lines[2]);

    measurement.enclosure_configured = true;
    lines =
        LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network, mqtt,
                             LcdDashboard::InformationPage::status, LcdDashboard::NoticeSnapshot{},
                             measurement, LcdDashboard::MeasurementPage::enclosure_temperature);
    EXPECT_LINE("5:ON 100% O: BRAK", lines[2]);
    EXPECT_EQ(
        LcdDashboard::MeasurementPage::enclosure_temperature,
        LcdDashboard::nextMeasurementPage(LcdDashboard::MeasurementPage::radiator_temperature));
    EXPECT_EQ(
        LcdDashboard::MeasurementPage::current,
        LcdDashboard::nextMeasurementPage(LcdDashboard::MeasurementPage::enclosure_temperature));
}

void testLcdDashboardMeasurementRotationSkipsUnusedPages() noexcept {
    LcdDashboard::MeasurementSnapshot measurement{};
    measurement.radiator_configured = true;
    measurement.current_supported = true;
    EXPECT_EQ(LcdDashboard::MeasurementPage::current,
              LcdDashboard::nextMeasurementPage(LcdDashboard::MeasurementPage::radiator_temperature,
                                                measurement));
    EXPECT_EQ(
        LcdDashboard::MeasurementPage::radiator_temperature,
        LcdDashboard::nextMeasurementPage(LcdDashboard::MeasurementPage::current, measurement));

    measurement.current_supported = false;
    EXPECT_EQ(LcdDashboard::MeasurementPage::radiator_temperature,
              LcdDashboard::nextMeasurementPage(LcdDashboard::MeasurementPage::radiator_temperature,
                                                measurement));
}

void testTemperatureModelCrcRomAndScratchpad() noexcept {
    const std::uint64_t rom_id = makeDs18b20Rom(0x10U);
    EXPECT_TRUE(temperature_model::romIsValid(rom_id));
    EXPECT_TRUE(!temperature_model::romIsValid(rom_id ^ (std::uint64_t{1} << 16U)));

    std::array<std::uint8_t, temperature_model::kScratchpadSize> scratchpad{{
        0x91U,
        0x01U,
        0x4BU,
        0x46U,
        0x7FU,
        0xFFU,
        0x0CU,
        0x10U,
        0U,
    }};
    scratchpad.back() = temperature_model::dallasCrc8(scratchpad.data(), scratchpad.size() - 1U);
    std::int32_t temperature = 0;
    EXPECT_TRUE(
        temperature_model::parseScratchpad(scratchpad.data(), scratchpad.size(), temperature));
    EXPECT_EQ(25'063, temperature);

    scratchpad[0] = 0x5EU;
    scratchpad[1] = 0xFFU;
    scratchpad.back() = temperature_model::dallasCrc8(scratchpad.data(), scratchpad.size() - 1U);
    EXPECT_TRUE(
        temperature_model::parseScratchpad(scratchpad.data(), scratchpad.size(), temperature));
    EXPECT_EQ(-10'125, temperature);
    scratchpad.back() ^= 0x01U;
    EXPECT_TRUE(
        !temperature_model::parseScratchpad(scratchpad.data(), scratchpad.size(), temperature));
}

void testIna228ScalingForTenMilliohmShunt() noexcept {
    EXPECT_EQ(3'277U, ina228_scaling::kNominalShuntCalibration);
    EXPECT_EQ(375'000, ina228_scaling::decodeSigned20(375'000U << 4U));
    EXPECT_EQ(-1, ina228_scaling::decodeSigned20(0x00FF'FFF0U));
    EXPECT_EQ(9'375, ina228_scaling::currentMilliamps(375'000));
    EXPECT_EQ(93'750, ina228_scaling::shuntMicrovolts(300'000));
    EXPECT_EQ(24'000U, ina228_scaling::busVoltageMillivolts(122'880));
    EXPECT_EQ(225'000U, ina228_scaling::powerMilliwatts(2'812'500));
    EXPECT_EQ(1'000U, ina228_scaling::energyMilliwattHours(2'812'500));
    EXPECT_EQ(25'000, ina228_scaling::dieTemperatureMillicelsius(3'200));
}

void testLcdDashboardStatus() noexcept {
    const std::array<LcdDashboard::ChannelSnapshot, LcdDashboard::kChannelCount> channels{};
    const LcdDashboard::NetworkSnapshot network{};
    const LcdDashboard::MqttSnapshot mqtt{};

    LcdDashboard::Lines lines =
        LcdDashboard::render(channels, LcdDashboard::Status::watchdog_reset, 90'061, network, mqtt,
                             LcdDashboard::InformationPage::uptime);
    EXPECT_LINE("RESET: WATCHDOG", lines[3]);

    lines = LcdDashboard::render(channels, LcdDashboard::Status::fault, 90'061, network, mqtt,
                                 LcdDashboard::InformationPage::uptime);
    EXPECT_LINE("LOKALNIE: BLAD", lines[3]);
}

void testLcdDashboardInformationRotation() noexcept {
    const std::array<LcdDashboard::ChannelSnapshot, LcdDashboard::kChannelCount> channels{};
    const LcdDashboard::NetworkSnapshot network{};
    const LcdDashboard::MqttSnapshot mqtt{};

    const LcdDashboard::Lines lines =
        LcdDashboard::render(channels, LcdDashboard::Status::ok, 90'061, network, mqtt,
                             LcdDashboard::InformationPage::uptime);
    EXPECT_LINE("UP 1d 01:01:01", lines[3]);
    EXPECT_EQ(LcdDashboard::InformationPage::uptime,
              LcdDashboard::nextInformationPage(LcdDashboard::InformationPage::status));
    EXPECT_EQ(LcdDashboard::InformationPage::wifi,
              LcdDashboard::nextInformationPage(LcdDashboard::InformationPage::uptime));
    EXPECT_EQ(LcdDashboard::InformationPage::ip,
              LcdDashboard::nextInformationPage(LcdDashboard::InformationPage::wifi));
    EXPECT_EQ(LcdDashboard::InformationPage::mqtt,
              LcdDashboard::nextInformationPage(LcdDashboard::InformationPage::ip));
    EXPECT_EQ(LcdDashboard::InformationPage::date_time,
              LcdDashboard::nextInformationPage(LcdDashboard::InformationPage::mqtt));
    EXPECT_EQ(LcdDashboard::InformationPage::status,
              LcdDashboard::nextInformationPage(LcdDashboard::InformationPage::date_time));
}

void testLcdDashboardNetworkPages() noexcept {
    const std::array<LcdDashboard::ChannelSnapshot, LcdDashboard::kChannelCount> channels{};
    LcdDashboard::NetworkSnapshot network{};
    const LcdDashboard::MqttSnapshot mqtt{};

    LcdDashboard::Lines lines = LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network,
                                                     mqtt, LcdDashboard::InformationPage::wifi);
    EXPECT_LINE("WIFI: NIEDOSTEPNE", lines[3]);

    network.state = LcdDashboard::NetworkState::connected;
    network.rssi_dbm = -61;
    static_cast<void>(std::snprintf(network.ssid.data(), network.ssid.size(), "Dom_2G"));
    static_cast<void>(std::snprintf(network.ip.data(), network.ip.size(), "192.168.1.84"));
    static_cast<void>(std::snprintf(network.local_time.data(), network.local_time.size(),
                                    "27.07.2026 21:37:45 CEST"));
    network.time_synchronized = true;

    lines = LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network, mqtt,
                                 LcdDashboard::InformationPage::wifi);
    EXPECT_LINE("-61dBm Dom_2G", lines[3]);

    static_cast<void>(
        std::snprintf(network.ssid.data(), network.ssid.size(), "Bardzo-Dluga-Nazwa"));
    lines = LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network, mqtt,
                                 LcdDashboard::InformationPage::wifi);
    EXPECT_LINE("-61dBm Bardzo-Dluga-", lines[3]);

    lines = LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network, mqtt,
                                 LcdDashboard::InformationPage::ip);
    EXPECT_LINE("IP 192.168.1.84", lines[3]);
    lines = LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network, mqtt,
                                 LcdDashboard::InformationPage::date_time);
    EXPECT_LINE("27.07.2026 21:37:45", lines[3]);
}

void testLcdDashboardMqttPage() noexcept {
    const std::array<LcdDashboard::ChannelSnapshot, LcdDashboard::kChannelCount> channels{};
    const LcdDashboard::NetworkSnapshot network{};
    LcdDashboard::MqttSnapshot mqtt{};

    LcdDashboard::Lines lines = LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network,
                                                     mqtt, LcdDashboard::InformationPage::mqtt);
    EXPECT_LINE("MQTT: NIEDOSTEPNE", lines[3]);

    mqtt.state = LcdDashboard::MqttState::unconfigured;
    lines = LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network, mqtt,
                                 LcdDashboard::InformationPage::mqtt);
    EXPECT_LINE("MQTT: BRAK KONFIG", lines[3]);

    mqtt.state = LcdDashboard::MqttState::connected;
    lines = LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network, mqtt,
                                 LcdDashboard::InformationPage::mqtt);
    EXPECT_LINE("MQTT: POLACZONE", lines[3]);

    mqtt.publish_backlog = true;
    lines = LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network, mqtt,
                                 LcdDashboard::InformationPage::mqtt);
    EXPECT_LINE("MQTT: KOLEJKA PELNA", lines[3]);
    mqtt.publish_backlog = false;

    mqtt.state = LcdDashboard::MqttState::authentication_error;
    lines = LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network, mqtt,
                                 LcdDashboard::InformationPage::mqtt);
    EXPECT_LINE("MQTT: BLAD LOGOWANIA", lines[3]);
}

void testLcdDashboardNoticeAndFaultPriority() noexcept {
    const std::array<LcdDashboard::ChannelSnapshot, LcdDashboard::kChannelCount> channels{};
    const LcdDashboard::NetworkSnapshot network{};
    const LcdDashboard::MqttSnapshot mqtt{};
    LcdDashboard::NoticeSnapshot notice{};
    notice.active = true;
    static_cast<void>(std::snprintf(notice.text.data(), notice.text.size(), "IR: KANAL 3"));

    LcdDashboard::Lines lines =
        LcdDashboard::render(channels, LcdDashboard::Status::ok, 0, network, mqtt,
                             LcdDashboard::InformationPage::wifi, notice);
    EXPECT_LINE("IR: KANAL 3", lines[3]);

    lines = LcdDashboard::render(channels, LcdDashboard::Status::fault, 0, network, mqtt,
                                 LcdDashboard::InformationPage::wifi, notice);
    EXPECT_LINE("LOKALNIE: BLAD", lines[3]);
}

void testDeviceIdentityUsesNeutralChannelNames() noexcept {
    EXPECT_TRUE(std::strcmp(device_identity::kDisplayName, "Sterownik LED") == 0);
    EXPECT_TRUE(std::strcmp(device_identity::kManufacturer, "Daniel Parzniewski") == 0);
    EXPECT_TRUE(std::strcmp(device_identity::kModel, "ESP32 5-channel PWM") == 0);
    EXPECT_TRUE(std::strcmp(device_identity::kTechnicalSlug, "sterownik-led") == 0);
    EXPECT_TRUE(std::strcmp(device_identity::kDeviceIdPrefix, "sterownik_led") == 0);

    constexpr std::array<const char*, 5> expected_names{{
        "Kanał 1",
        "Kanał 2",
        "Kanał 3",
        "Kanał 4",
        "Kanał 5",
    }};
    for (std::size_t index = 0; index < expected_names.size(); ++index) {
        EXPECT_TRUE(std::strcmp(device_identity::kHomeAssistantChannelNames[index],
                                expected_names[index]) == 0);
    }

    constexpr std::array<const char*, 2> expected_pir_names{{
        "Wejście PIR 1",
        "Wejście PIR 2",
    }};
    for (std::size_t index = 0; index < expected_pir_names.size(); ++index) {
        EXPECT_TRUE(std::strcmp(device_identity::kHomeAssistantPirInputNames[index],
                                expected_pir_names[index]) == 0);
    }
}

void testMqttCommandParser() noexcept {
    ControlCommand command{};
    constexpr char payload[] = R"({"state":"ON","brightness":128,"transition":1.25})";
    EXPECT_EQ(mqtt_protocol::ParseResult::ok,
              mqtt_protocol::parseCommand(payload, sizeof(payload) - 1U, command));
    EXPECT_EQ(ControlCommand::StateAction::turn_on, command.state);
    EXPECT_TRUE(command.brightness_present);
    EXPECT_EQ(128U, command.brightness);
    EXPECT_TRUE(command.transition_present);
    EXPECT_EQ(1'250U, command.transition_ms);

    constexpr char brightness_only[] = R"({"brightness":0})";
    EXPECT_EQ(mqtt_protocol::ParseResult::ok,
              mqtt_protocol::parseCommand(brightness_only, sizeof(brightness_only) - 1U, command));
    EXPECT_EQ(ControlCommand::StateAction::unchanged, command.state);
    EXPECT_TRUE(command.brightness_present);
    EXPECT_EQ(0U, command.brightness);
    EXPECT_TRUE(!command.transition_present);

    EXPECT_EQ(1U, mqtt_protocol::brightnessToPercent(0));
    EXPECT_EQ(1U, mqtt_protocol::brightnessToPercent(1));
    EXPECT_EQ(50U, mqtt_protocol::brightnessToPercent(128));
    EXPECT_EQ(100U, mqtt_protocol::brightnessToPercent(255));
    EXPECT_EQ(3U, mqtt_protocol::percentToBrightness(1));
    EXPECT_EQ(128U, mqtt_protocol::percentToBrightness(50));
    EXPECT_EQ(255U, mqtt_protocol::percentToBrightness(100));
}

void testMqttCommandParserRejectsInvalidPayloads() noexcept {
    ControlCommand command{};
    constexpr char fractional_brightness[] = R"({"brightness":10.5})";
    EXPECT_EQ(mqtt_protocol::ParseResult::invalid_brightness,
              mqtt_protocol::parseCommand(fractional_brightness, sizeof(fractional_brightness) - 1U,
                                          command));

    constexpr char excessive_brightness[] = R"({"brightness":256})";
    EXPECT_EQ(mqtt_protocol::ParseResult::invalid_brightness,
              mqtt_protocol::parseCommand(excessive_brightness, sizeof(excessive_brightness) - 1U,
                                          command));

    constexpr char bad_state[] = R"({"state":"on"})";
    EXPECT_EQ(mqtt_protocol::ParseResult::invalid_state,
              mqtt_protocol::parseCommand(bad_state, sizeof(bad_state) - 1U, command));

    constexpr char transition_only[] = R"({"transition":2})";
    EXPECT_EQ(mqtt_protocol::ParseResult::no_action,
              mqtt_protocol::parseCommand(transition_only, sizeof(transition_only) - 1U, command));

    constexpr char unknown_field[] = R"({"state":"OFF","effect":"blink"})";
    EXPECT_EQ(mqtt_protocol::ParseResult::unsupported_field,
              mqtt_protocol::parseCommand(unknown_field, sizeof(unknown_field) - 1U, command));

    constexpr char duplicate_field[] = R"({"state":"ON","state":"OFF"})";
    EXPECT_EQ(mqtt_protocol::ParseResult::duplicate_field,
              mqtt_protocol::parseCommand(duplicate_field, sizeof(duplicate_field) - 1U, command));

    constexpr char trailing_text[] = R"({"state":"ON"}x)";
    EXPECT_EQ(mqtt_protocol::ParseResult::invalid_json,
              mqtt_protocol::parseCommand(trailing_text, sizeof(trailing_text) - 1U, command));

    constexpr char root_array[] = R"([{"state":"ON"}])";
    EXPECT_EQ(mqtt_protocol::ParseResult::root_not_object,
              mqtt_protocol::parseCommand(root_array, sizeof(root_array) - 1U, command));

    constexpr char empty_object[] = R"({})";
    EXPECT_EQ(mqtt_protocol::ParseResult::no_action,
              mqtt_protocol::parseCommand(empty_object, sizeof(empty_object) - 1U, command));

    constexpr char excessive_transition[] = R"({"state":"ON","transition":3600.001})";
    EXPECT_EQ(mqtt_protocol::ParseResult::invalid_transition,
              mqtt_protocol::parseCommand(excessive_transition, sizeof(excessive_transition) - 1U,
                                          command));

    std::array<char, mqtt_protocol::kMaxCommandPayloadLength + 1U> oversized_payload{};
    oversized_payload.fill('x');
    EXPECT_EQ(
        mqtt_protocol::ParseResult::payload_too_long,
        mqtt_protocol::parseCommand(oversized_payload.data(), oversized_payload.size(), command));
}

void testMqttPublishTrackerRetriesOnlyUnpublishedFields() noexcept {
    MqttPublishTracker tracker;
    const auto channel = MqttPublishTracker::Field::channel_1;
    const auto temperature = MqttPublishTracker::Field::enclosure_temperature;

    EXPECT_TRUE(tracker.needsPublish(channel, 100U));
    tracker.markPublished(channel, 100U);
    EXPECT_TRUE(!tracker.needsPublish(channel, 100U));
    EXPECT_TRUE(tracker.needsPublish(channel, 101U));
    EXPECT_TRUE(tracker.needsPublish(temperature, 26'625U));

    tracker.markPublished(temperature, 26'625U);
    tracker.invalidate(temperature);
    EXPECT_TRUE(tracker.needsPublish(temperature, 26'625U));

    tracker.markPublished(channel, 101U);
    tracker.markPublished(temperature, 26'625U);
    tracker.invalidateAll();
    EXPECT_TRUE(tracker.needsPublish(channel, 101U));
    EXPECT_TRUE(tracker.needsPublish(temperature, 26'625U));
}

void testOtaRequestValidation() noexcept {
    using ota_request::ValidationResult;

    constexpr char public_url[] = "https://firmware.example.org/sterownik-led.bin";
    EXPECT_EQ(ValidationResult::ok, ota_request::validate(public_url, sizeof(public_url) - 1U));

    constexpr char local_url[] = "https://192.168.1.40:8443/fw.bin?build=8";
    EXPECT_EQ(ValidationResult::ok, ota_request::validate(local_url, sizeof(local_url) - 1U));

    constexpr char path_with_at[] = "https://example.org/builds/fw@next.bin";
    EXPECT_EQ(ValidationResult::ok, ota_request::validate(path_with_at, sizeof(path_with_at) - 1U));

    constexpr char insecure_url[] = "http://example.org/fw.bin";
    EXPECT_EQ(ValidationResult::insecure_scheme,
              ota_request::validate(insecure_url, sizeof(insecure_url) - 1U));
    EXPECT_EQ(ValidationResult::empty, ota_request::validate(nullptr, 0U));

    constexpr char missing_host[] = "https:///fw.bin";
    EXPECT_EQ(ValidationResult::missing_host,
              ota_request::validate(missing_host, sizeof(missing_host) - 1U));

    constexpr char credentials[] = "https://user@example.org/fw.bin";
    EXPECT_EQ(ValidationResult::credentials_not_allowed,
              ota_request::validate(credentials, sizeof(credentials) - 1U));

    constexpr char whitespace[] = "https://example.org/fw bad.bin";
    EXPECT_EQ(ValidationResult::invalid_character,
              ota_request::validate(whitespace, sizeof(whitespace) - 1U));

    constexpr char fragment[] = "https://example.org/fw.bin#old";
    EXPECT_EQ(ValidationResult::invalid_character,
              ota_request::validate(fragment, sizeof(fragment) - 1U));
    EXPECT_EQ(ValidationResult::too_long,
              ota_request::validate(public_url, ota_request::kMaxUrlLength + 1U));
}

void testSecureSettingsRecordLayout() noexcept {
    SecureSettingsRecord::Slot slot{};
    SecureSettingsRecord::Nonce nonce{};
    for (std::size_t index = 0; index < nonce.size(); ++index) {
        nonce[index] = static_cast<std::uint8_t>(index + 1U);
    }

    EXPECT_TRUE(SecureSettingsRecord::prepareStaged(slot, 7, 3, 123, nonce));

    SecureSettingsRecord::Metadata metadata{};
    EXPECT_TRUE(SecureSettingsRecord::inspect(slot, false, metadata));
    EXPECT_TRUE(!SecureSettingsRecord::inspect(slot, true, metadata));

    SecureSettingsRecord::markCommitted(slot);
    EXPECT_TRUE(SecureSettingsRecord::inspect(slot, true, metadata));
    EXPECT_EQ(7U, metadata.sequence);
    EXPECT_EQ(3U, metadata.key_generation);
    EXPECT_EQ(123U, metadata.payload_size);
    EXPECT_EQ(nonce[0], metadata.nonce[0]);
    EXPECT_EQ(nonce[11], metadata.nonce[11]);
    EXPECT_EQ(31U, SecureSettingsRecord::aadSize());
}

void testSecureSettingsRecordRejectsInvalidData() noexcept {
    SecureSettingsRecord::Slot slot{};
    slot.fill(0xFF);
    SecureSettingsRecord::Metadata metadata{};
    EXPECT_TRUE(!SecureSettingsRecord::inspect(slot, false, metadata));

    SecureSettingsRecord::Nonce nonce{};
    EXPECT_TRUE(!SecureSettingsRecord::prepareStaged(
        slot, 1, 1, SecureSettingsRecord::kMaxPayloadSize + 1U, nonce));

    EXPECT_TRUE(SecureSettingsRecord::prepareStaged(slot, 1, 1,
                                                    SecureSettingsRecord::kMaxPayloadSize, nonce));
    SecureSettingsRecord::markCommitted(slot);
    slot[1] = static_cast<std::uint8_t>(slot[1] ^ 0x01U);
    EXPECT_TRUE(!SecureSettingsRecord::inspect(slot, true, metadata));
}

void testSecureSettingsSequenceOrdering() noexcept {
    constexpr std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max();
    EXPECT_TRUE(SecureSettingsRecord::sequenceIsNewer(2, 1));
    EXPECT_TRUE(!SecureSettingsRecord::sequenceIsNewer(1, 2));
    EXPECT_TRUE(!SecureSettingsRecord::sequenceIsNewer(5, 5));
    EXPECT_TRUE(SecureSettingsRecord::sequenceIsNewer(0, maximum));

    EXPECT_EQ(SecureSettingsRecord::SlotSelection::none,
              SecureSettingsRecord::chooseNewest(false, 0, false, 0));
    EXPECT_EQ(SecureSettingsRecord::SlotSelection::first,
              SecureSettingsRecord::chooseNewest(true, 10, false, 0));
    EXPECT_EQ(SecureSettingsRecord::SlotSelection::second,
              SecureSettingsRecord::chooseNewest(true, 10, true, 11));
    EXPECT_EQ(SecureSettingsRecord::SlotSelection::first,
              SecureSettingsRecord::chooseNewest(true, maximum, true, maximum - 1U));
}

template <std::size_t Size>
void setText(std::array<char, Size>& destination, const char* const text) noexcept {
    destination.fill('\0');
    const std::size_t length = std::strlen(text);
    EXPECT_TRUE(length < destination.size());
    if (length < destination.size()) {
        std::memcpy(destination.data(), text, length);
    }
}

void testPersistentSettingsRoundTrip() noexcept {
    persistent_settings::Data source{};
    source.wifi.configured = true;
    source.wifi.dhcp = false;
    setText(source.wifi.ssid, "Siec testowa");
    setText(source.wifi.password, "bezpieczne-haslo");
    source.wifi.ip = 0x0101A8C0U;
    source.wifi.netmask = 0x00FFFFFFU;
    source.wifi.gateway = 0x0101A8C0U;
    source.wifi.dns_primary = 0x08080808U;
    source.mqtt.configured = true;
    setText(source.mqtt.uri, "mqtt://192.168.1.10:1883");
    setText(source.mqtt.username, "daniel");
    setText(source.mqtt.password, "mqtt-secret");
    setText(source.mqtt.topic_prefix, "sterownik-led-test");
    setText(source.mqtt.discovery_prefix, "ha-test");
    source.ina_current_correction_ppm = -12'500;
    source.temperature_assignments.rom_ids[0] = makeDs18b20Rom(0x20U);
    source.temperature_assignments.rom_ids[1] = makeDs18b20Rom(0x40U);

    SecureSettingsRecord::Payload payload{};
    std::size_t payload_size = 0;
    EXPECT_EQ(persistent_settings::CodecResult::ok,
              persistent_settings::encode(source, payload.data(), payload.size(), payload_size));
    EXPECT_TRUE(payload_size < payload.size());

    persistent_settings::Data decoded{};
    EXPECT_EQ(persistent_settings::CodecResult::ok,
              persistent_settings::decode(payload.data(), payload_size, decoded));
    EXPECT_TRUE(decoded.wifi.configured);
    EXPECT_TRUE(!decoded.wifi.dhcp);
    EXPECT_TRUE(decoded.wifi.ssid == source.wifi.ssid);
    EXPECT_TRUE(decoded.wifi.password == source.wifi.password);
    EXPECT_EQ(source.wifi.ip, decoded.wifi.ip);
    EXPECT_EQ(source.wifi.netmask, decoded.wifi.netmask);
    EXPECT_EQ(source.wifi.gateway, decoded.wifi.gateway);
    EXPECT_EQ(source.wifi.dns_primary, decoded.wifi.dns_primary);
    EXPECT_TRUE(decoded.mqtt.configured);
    EXPECT_TRUE(decoded.mqtt.uri == source.mqtt.uri);
    EXPECT_TRUE(decoded.mqtt.username == source.mqtt.username);
    EXPECT_TRUE(decoded.mqtt.password == source.mqtt.password);
    EXPECT_TRUE(decoded.mqtt.topic_prefix == source.mqtt.topic_prefix);
    EXPECT_TRUE(decoded.mqtt.discovery_prefix == source.mqtt.discovery_prefix);
    EXPECT_EQ(source.ina_current_correction_ppm, decoded.ina_current_correction_ppm);
    EXPECT_TRUE(source.temperature_assignments.rom_ids == decoded.temperature_assignments.rom_ids);
}

void testPersistentSettingsEmptyRecordUsesDefaults() noexcept {
    persistent_settings::Data source{};
    SecureSettingsRecord::Payload payload{};
    std::size_t payload_size = 0;
    EXPECT_EQ(persistent_settings::CodecResult::ok,
              persistent_settings::encode(source, payload.data(), payload.size(), payload_size));
    EXPECT_EQ(8U, payload_size);

    persistent_settings::Data decoded{};
    EXPECT_EQ(persistent_settings::CodecResult::ok,
              persistent_settings::decode(payload.data(), payload_size, decoded));
    EXPECT_TRUE(!decoded.wifi.configured);
    EXPECT_TRUE(!decoded.mqtt.configured);
    EXPECT_EQ(0, decoded.ina_current_correction_ppm);
    EXPECT_EQ(0U, decoded.temperature_assignments.rom_ids[0]);
    EXPECT_EQ(0U, decoded.temperature_assignments.rom_ids[1]);
    EXPECT_TRUE(std::strcmp(decoded.mqtt.topic_prefix.data(),
                            persistent_settings::kDefaultTopicPrefix) == 0);
    EXPECT_TRUE(std::strcmp(decoded.mqtt.discovery_prefix.data(),
                            persistent_settings::kDefaultDiscoveryPrefix) == 0);
}

void testPersistentSettingsRejectsInvalidTemperatureAssignments() noexcept {
    persistent_settings::Data source{};
    SecureSettingsRecord::Payload payload{};
    std::size_t payload_size = 0;

    source.temperature_assignments.rom_ids[0] = makeDs18b20Rom(0x20U);
    source.temperature_assignments.rom_ids[1] = source.temperature_assignments.rom_ids[0];
    EXPECT_EQ(persistent_settings::CodecResult::invalid_data,
              persistent_settings::encode(source, payload.data(), payload.size(), payload_size));

    source.temperature_assignments.rom_ids[1] = makeDs18b20Rom(0x40U) ^ (std::uint64_t{1} << 8U);
    EXPECT_EQ(persistent_settings::CodecResult::invalid_data,
              persistent_settings::encode(source, payload.data(), payload.size(), payload_size));
}

void testPersistentSettingsRejectsMalformedAndOversizedData() noexcept {
    persistent_settings::Data source{};
    source.wifi.configured = true;
    source.wifi.dhcp = true;
    source.wifi.ssid.fill('s');
    source.wifi.ssid.back() = '\0';
    source.wifi.password.fill('w');
    source.wifi.password.back() = '\0';
    source.mqtt.configured = true;
    source.mqtt.uri.fill('u');
    std::memcpy(source.mqtt.uri.data(), "mqtt://", 7);
    source.mqtt.uri.back() = '\0';
    source.mqtt.username.fill('n');
    source.mqtt.username.back() = '\0';
    source.mqtt.password.fill('p');
    source.mqtt.password.back() = '\0';
    source.mqtt.topic_prefix.fill('t');
    source.mqtt.topic_prefix.back() = '\0';
    source.mqtt.discovery_prefix.fill('d');
    source.mqtt.discovery_prefix.back() = '\0';

    SecureSettingsRecord::Payload payload{};
    std::size_t payload_size = 0;
    EXPECT_EQ(persistent_settings::CodecResult::buffer_too_small,
              persistent_settings::encode(source, payload.data(), payload.size(), payload_size));
    EXPECT_EQ(0U, payload_size);

    persistent_settings::Data empty{};
    EXPECT_EQ(persistent_settings::CodecResult::ok,
              persistent_settings::encode(empty, payload.data(), payload.size(), payload_size));
    payload[6] = static_cast<std::uint8_t>(payload[6] + 1U);
    persistent_settings::Data decoded{};
    EXPECT_EQ(persistent_settings::CodecResult::invalid_data,
              persistent_settings::decode(payload.data(), payload_size, decoded));
}
} // namespace

void testLocalControlMappingAndParsing() noexcept {
    local_control::Config config{};
    EXPECT_TRUE(local_control::valid(config));
    EXPECT_EQ(1U, config.button_masks[0]);
    EXPECT_EQ(2U, config.button_masks[1]);
    EXPECT_EQ(0U, config.off_delay_seconds[0]);
    for (const auto mask : local_control::kButtonMasks) {
        std::uint8_t parsed = 255;
        const auto* option = local_control::optionForMask(mask);
        EXPECT_TRUE(local_control::parseMask(option, std::strlen(option), parsed));
        EXPECT_EQ(mask, parsed);
    }
    std::uint8_t mask = 0;
    EXPECT_TRUE(!local_control::parseMask("Kanal 1x", 8, mask));
    EXPECT_TRUE(!local_control::parseMask("Kanal 1\0", 8, mask));
    std::uint16_t seconds = 123;
    EXPECT_TRUE(local_control::parseDelay("3600", 4, seconds));
    EXPECT_EQ(3600U, seconds);
    EXPECT_TRUE(!local_control::parseDelay("3601", 4, seconds));
    EXPECT_TRUE(!local_control::parseDelay("-1", 2, seconds));
    EXPECT_TRUE(!local_control::parseDelay("1.5", 3, seconds));
    EXPECT_TRUE(!local_control::parseDelay("10x", 3, seconds));
    EXPECT_TRUE(!local_control::parseDelay(nullptr, 0, seconds));
    EXPECT_TRUE(!local_control::parseDelay("65536", 5, seconds));
    EXPECT_TRUE(local_control::parseDelay("0", 1, seconds));
    EXPECT_EQ(0U, seconds);
    config.button_masks[1] = 32;
    EXPECT_TRUE(!local_control::valid(config));
}

void testLocalControlGroupAndDimming() noexcept {
    std::array<bool, 5> on{};
    EXPECT_TRUE(!local_control::groupShouldTurnOn(0, on));
    EXPECT_TRUE(local_control::groupShouldTurnOn(3, on));
    on[0] = true;
    EXPECT_TRUE(local_control::groupShouldTurnOn(3, on));
    EXPECT_TRUE(!local_control::groupShouldTurnOn(1, on));
    on[1] = true;
    EXPECT_TRUE(!local_control::groupShouldTurnOn(3, on));
    EXPECT_TRUE(local_control::groupShouldTurnOn(31, on));
    EXPECT_EQ(30U, local_control::scaledBrightness(40, 80, 60));
    EXPECT_EQ(50U, local_control::scaledBrightness(40, 80, 100));
    EXPECT_EQ(1U, local_control::scaledBrightness(1, 100, 1));
}

void testPirAutomationTimersAndManualPriority() noexcept {
    using Action = PirAutomation::Action;
    PirAutomation first, second;
    EXPECT_EQ(Action::none, first.update(true, false, 0, 0));
    // An already HIGH input at startup is handled without requiring a new edge.
    EXPECT_EQ(Action::turn_on, first.update(true, false, 10, 100));
    EXPECT_EQ(Action::none, second.update(false, false, 10, 100));
    EXPECT_EQ(Action::none, first.update(true, true, 1000, 100));
    EXPECT_EQ(Action::none, first.update(false, true, 1099, 100));
    EXPECT_EQ(Action::none, first.update(true, true, 1099, 100));
    EXPECT_EQ(Action::none, first.update(false, true, 1198, 100));
    EXPECT_EQ(Action::turn_off, first.update(false, true, 1199, 100));
    EXPECT_EQ(Action::none, first.update(false, false, 1200, 100));
    EXPECT_EQ(Action::turn_on, first.update(true, false, 1300, 100));
    first.manualOverride(true);
    EXPECT_EQ(Action::none, first.update(true, false, 1301, 100));
    EXPECT_EQ(Action::none, first.update(false, false, 1302, 100));
    EXPECT_EQ(Action::turn_on, first.update(true, false, 1303, 100));
    first.manualOverride(true); // Manual ON is never owned by the PIR timer.
    EXPECT_EQ(Action::none, first.update(false, true, 5000, 100));
    EXPECT_EQ(Action::none, first.update(true, true, 6000, 100));
    EXPECT_EQ(Action::none, first.update(false, true, 7000, 100));
    EXPECT_EQ(Action::none, first.update(false, true, 8000, 0));
}

void testPirAutomationOverflowAndDisable() noexcept {
    using Action = PirAutomation::Action;
    PirAutomation pir;
    EXPECT_EQ(Action::turn_on, pir.update(true, false, 0xFFFFFFF0U, 100));
    EXPECT_EQ(Action::none, pir.update(false, true, 83, 100));
    EXPECT_EQ(Action::turn_off, pir.update(false, true, 84, 100));
    EXPECT_EQ(Action::turn_on, pir.update(true, false, 100, 100));
    EXPECT_EQ(Action::turn_off, pir.update(true, true, 101, 0));
    EXPECT_EQ(Action::none, pir.update(true, false, 102, 0));
}

void testLocalConfigNvramRecovery() noexcept {
    local_control::Config config{};
    config.button_masks[0] = 3;
    config.button_masks[1] = 0;
    config.off_delay_seconds = {{60, 120}};
    auto record = local_control::encodeRecord(config, 0xFFFFFFFFU);
    local_control::Config decoded{};
    std::uint32_t sequence = 0;
    EXPECT_TRUE(local_control::decodeRecord(record, decoded, sequence));
    EXPECT_TRUE(local_control::equal(config, decoded));
    EXPECT_EQ(0xFFFFFFFFU, sequence);
    for (std::size_t index = 0; index < record.size(); ++index) {
        auto corrupt = record;
        corrupt[index] ^= 1;
        EXPECT_TRUE(!local_control::decodeRecord(corrupt, decoded, sequence));
    }
    auto next = config;
    next.off_delay_seconds[0] = 3600;
    std::array<local_control::Record, 2> slots{{record, local_control::encodeRecord(next, 0)}};
    EXPECT_EQ(1, local_control::newestRecord(slots, decoded, sequence));
    EXPECT_TRUE(local_control::equal(next, decoded));
    // Simulate interrupted writes at every byte before committing the second slot.
    for (std::size_t size = 0; size < record.size(); ++size) {
        slots[1].fill(0);
        const auto pending = local_control::encodeRecord(next, 0);
        for (std::size_t index = 0; index < size; ++index) {
            slots[1][index] = pending[index];
        }
        EXPECT_EQ(0, local_control::newestRecord(slots, decoded, sequence));
        EXPECT_TRUE(local_control::equal(config, decoded));
    }
    slots = {};
    EXPECT_EQ(-1, local_control::newestRecord(slots, decoded, sequence));
}

void testLocalControlDiscovery() noexcept {
    cJSON* components = cJSON_CreateObject();
    EXPECT_TRUE(components != nullptr);
    EXPECT_TRUE(local_control::appendDiscovery(components, "led/device", "device"));
    EXPECT_EQ(20, cJSON_GetArraySize(components));
    cJSON* key = cJSON_GetObjectItemCaseSensitive(components, "key_1");
    EXPECT_TRUE(key != nullptr);
    EXPECT_TRUE(std::strcmp(cJSON_GetObjectItemCaseSensitive(key, "entity_category")->valuestring,
                            "config") == 0);
    EXPECT_TRUE(std::strcmp(cJSON_GetObjectItemCaseSensitive(key, "command_topic")->valuestring,
                            "led/device/config/key/1/set") == 0);
    EXPECT_EQ(8, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(key, "options")));
    EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(key, "retain")));
    cJSON* delay = cJSON_GetObjectItemCaseSensitive(components, "pir_delay_2");
    EXPECT_EQ(3600, cJSON_GetObjectItemCaseSensitive(delay, "max")->valueint);
    EXPECT_EQ(0, cJSON_GetObjectItemCaseSensitive(delay, "min")->valueint);
    EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(delay, "optimistic")));
    cJSON_Delete(components);
}

void testPowerFadeDoesNotPublishIntermediateLevels() noexcept {
    ChannelState state(65);
    DutyTransition fade;
    MqttPublishTracker tracker;
    const auto field = MqttPublishTracker::Field::channel_1;
    state.turnOn();
    fade.reset(0, 0);
    static_cast<void>(fade.retarget(state.targetDuty(4095), 250, 0));
    unsigned messages = 0;
    for (std::uint32_t tick = 0; tick <= 250; tick += 5) {
        static_cast<void>(fade.update(tick));
        const std::uint64_t fingerprint = state.brightness() | (state.isOn() ? 256U : 0U);
        if (tracker.needsPublish(field, fingerprint)) {
            ++messages;
            tracker.markPublished(field, fingerprint);
        }
    }
    EXPECT_EQ(1U, messages);
    state.turnOff();
    static_cast<void>(fade.retarget(0, 250, 250));
    for (std::uint32_t tick = 250; tick <= 500; tick += 5) {
        static_cast<void>(fade.update(tick));
        const auto fingerprint = state.brightness();
        if (tracker.needsPublish(field, fingerprint)) {
            ++messages;
            tracker.markPublished(field, fingerprint);
        }
    }
    EXPECT_EQ(2U, messages);
    state.setBrightness(40);
    EXPECT_TRUE(tracker.needsPublish(field, state.brightness()));
}

void testBacklightNightAndMotion() noexcept {
    BacklightPolicy policy;
    const auto mode = local_control::BacklightMode::automatic;
    EXPECT_TRUE(policy.update(mode, false, false, 0, 100));
    EXPECT_TRUE(!policy.update(mode, true, false, 1, 100));
    EXPECT_TRUE(policy.update(mode, true, true, 2, 100));
    EXPECT_TRUE(policy.update(mode, true, true, 200, 100));
    EXPECT_TRUE(policy.update(mode, true, false, 299, 100));
    EXPECT_TRUE(!policy.update(mode, true, false, 300, 100));
    EXPECT_TRUE(policy.update(mode, false, false, 301, 100));
    EXPECT_TRUE(!policy.update(mode, true, true, 302, 0));
}

void testBacklightTemporaryButtonAndPermanentOff() noexcept {
    BacklightPolicy policy;
    const auto off = local_control::BacklightMode::always_off;
    EXPECT_TRUE(!policy.update(off, false, true, 0, 100));
    policy.toggle(10, 120);
    EXPECT_TRUE(policy.update(off, true, true, 129, 100));
    EXPECT_TRUE(!policy.update(off, false, true, 130, 100));
    const auto automatic = local_control::BacklightMode::automatic;
    EXPECT_TRUE(policy.update(automatic, false, false, 200, 100));
    policy.toggle(201, 120);
    EXPECT_TRUE(!policy.update(automatic, false, true, 300, 100));
    EXPECT_TRUE(policy.update(automatic, false, true, 321, 100));
    policy.toggle(322, 120);
    policy.cancelOverride();
    EXPECT_TRUE(policy.update(local_control::BacklightMode::always_on, true, false, 323, 100));
}

void testBacklightOverflowAndConfigRecord() noexcept {
    BacklightPolicy policy;
    const auto off = local_control::BacklightMode::always_off;
    EXPECT_TRUE(!policy.update(off, true, false, 0xFFFFFFF0U, 100));
    policy.toggle(0xFFFFFFF0U, 32);
    EXPECT_TRUE(policy.update(off, true, false, 15, 100));
    EXPECT_TRUE(!policy.update(off, true, false, 16, 100));
    local_control::Config config;
    config.backlight_mode = off;
    config.lcd_override_seconds = 3600;
    local_control::Config decoded;
    std::uint32_t sequence = 0;
    EXPECT_TRUE(
        local_control::decodeRecord(local_control::encodeRecord(config, 5), decoded, sequence));
    EXPECT_TRUE(local_control::equal(config, decoded));
    config.lcd_override_seconds = 0;
    EXPECT_TRUE(!local_control::valid(config));
    config.lcd_override_seconds = 120;
    config.backlight_mode = static_cast<local_control::BacklightMode>(3);
    EXPECT_TRUE(!local_control::valid(config));
}

void testDebugSession() noexcept {
    DebugBuffer buffer;
    DebugBuffer::Message message{};
    EXPECT_TRUE(!buffer.active(0));
    EXPECT_TRUE(!buffer.push("off", 0));
    buffer.setEnabled(true, 10);
    EXPECT_TRUE(buffer.push("first", 11));
    EXPECT_TRUE(buffer.pop(message, 12));
    EXPECT_TRUE(std::strcmp(message.data(), "first") == 0);
    EXPECT_TRUE(buffer.active(609));
    EXPECT_TRUE(!buffer.active(610));
    EXPECT_TRUE(!buffer.pop(message, 610));
    buffer.setEnabled(true, 700);
    EXPECT_TRUE(buffer.push("secret old session", 701));
    buffer.setEnabled(false, 702);
    buffer.setEnabled(true, 703);
    EXPECT_TRUE(!buffer.pop(message, 704));
}
void testDebugOverflowAndRenewal() noexcept {
    DebugBuffer buffer;
    buffer.setEnabled(true, 0xFFFFFF00U);
    EXPECT_TRUE(buffer.active(0xFFFFFF00U + 599U));
    EXPECT_TRUE(!buffer.active(0xFFFFFF00U + 600U));
    buffer.setEnabled(true, 0);
    buffer.setEnabled(true, 599);
    EXPECT_TRUE(buffer.active(1198));
    EXPECT_TRUE(!buffer.active(1199));
    buffer.setEnabled(true, 1200);
    for (unsigned i = 0; i < 40; ++i) {
        char text[16]{};
        std::snprintf(text, sizeof(text), "%u", i);
        EXPECT_TRUE(buffer.push(text, 1201));
    }
    EXPECT_EQ(8U, buffer.dropped());
    DebugBuffer::Message message{};
    EXPECT_TRUE(buffer.pop(message, 1202));
    EXPECT_TRUE(std::strcmp(message.data(), "8") == 0);
    buffer.setEnabled(false, 1203);
    EXPECT_TRUE(!buffer.pop(message, 1204));
    buffer.setEnabled(true, 1205);
    EXPECT_EQ(0U, buffer.dropped());
    std::array<char, 512> long_line{};
    long_line.fill('a');
    long_line.back() = 0;
    EXPECT_TRUE(buffer.push(long_line.data(), 1206));
    EXPECT_TRUE(buffer.pop(message, 1207));
    EXPECT_EQ(255U, std::strlen(message.data()));
}
void testDebugDiscovery() noexcept {
    cJSON* root = cJSON_CreateObject();
    EXPECT_TRUE(debug_discovery::append(root, "led/device", "device"));
    EXPECT_EQ(4, cJSON_GetArraySize(root));
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, "debug");
    EXPECT_TRUE(std::strcmp(cJSON_GetObjectItemCaseSensitive(item, "command_topic")->valuestring,
                            "led/device/debug/set") == 0);
    EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(item, "retain")));
    EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(item, "optimistic")));
    item = cJSON_GetObjectItemCaseSensitive(root, "uptime");
    EXPECT_TRUE(std::strcmp(cJSON_GetObjectItemCaseSensitive(item, "value_template")->valuestring,
                            "{{ value_json.uptime_s }}") == 0);
    cJSON_Delete(root);
    EXPECT_TRUE(!debug_discovery::append(nullptr, "x", "y"));
}

void testPwmConstraints() noexcept {
    pwm_config::Config c{};
    EXPECT_TRUE(pwm_config::valid(c));
    for (unsigned duty = 0; duty < 4096; ++duty)
        EXPECT_EQ(duty, pwm_config::constrain(duty, c));
    c.frequency_hz = 1000;
    c.minimum_percent = 20;
    c.on_index = 7;
    c.off_index = 7;
    EXPECT_TRUE(pwm_config::valid(c));
    EXPECT_EQ(0U, pwm_config::constrain(0, c));
    EXPECT_EQ(819U, pwm_config::constrain(1, c));
    EXPECT_EQ(3686U, pwm_config::constrain(4095, c));
    for (unsigned duty = 1; duty < 4096; ++duty) {
        auto actual = pwm_config::constrain(duty, c);
        EXPECT_TRUE(actual >= 819 && actual <= 3686);
    }
    c.minimum_percent = 100;
    EXPECT_TRUE(!pwm_config::valid(c));
    c.minimum_percent = 0;
    c.on_index = 15;
    EXPECT_TRUE(!pwm_config::valid(c));
    c.on_index = 0;
    c.off_index = 0;
    c.frequency_hz = 99;
    EXPECT_TRUE(!pwm_config::valid(c));
    c.frequency_hz = 19532;
    EXPECT_TRUE(!pwm_config::valid(c));
    c.frequency_hz = 100;
    c.on_off = true;
    EXPECT_EQ(4096U, pwm_config::constrain(1, c));
    EXPECT_EQ(0U, pwm_config::constrain(0, c));
    std::uint16_t v = 0;
    EXPECT_TRUE(pwm_config::parse(0, "19531", 5, v));
    EXPECT_EQ(19531U, v);
    EXPECT_TRUE(!pwm_config::parse(0, "99999", 5, v));
    EXPECT_TRUE(!pwm_config::parse(1, "-1", 2, v));
    EXPECT_TRUE(!pwm_config::parse(1, "1.0", 3, v));
    EXPECT_TRUE(!pwm_config::parse(2, "3", 1, v));
    EXPECT_TRUE(pwm_config::parse(2, "100", 3, v));
    EXPECT_EQ(7U, v);
    EXPECT_TRUE(pwm_config::parse(4, "ON/OFF", 6, v));
    EXPECT_EQ(1U, v);
}
void testPwmNvramMigration() noexcept {
    // Known v1 layout: mapping, delays and backlight must survive decoding with defaults for PWM.
    local_control::Record old{};
    old[0] = 'L';
    old[1] = 'C';
    old[2] = 'F';
    old[3] = 1;
    old[4] = 42;
    old[8] = 3;
    old[9] = 0;
    old[10] = 4;
    old[11] = 8;
    old[12] = 16;
    old[13] = 60;
    old[15] = 120;
    old[17] = 120;
    old[19] = 1;
    std::uint32_t crc = 0xFFFFFFFFU;
    for (unsigned i = 0; i < 23; ++i) {
        crc ^= old[i];
        for (unsigned j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320U : 0U);
    }
    crc = ~crc;
    for (unsigned i = 0; i < 4; ++i)
        old[23 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
    old[27] = local_control::kCommit;
    local_control::Config config{};
    std::uint32_t seq = 0;
    EXPECT_TRUE(local_control::decodeRecord(old, config, seq));
    EXPECT_EQ(42U, seq);
    EXPECT_EQ(3U, config.button_masks[0]);
    EXPECT_EQ(120U, config.off_delay_seconds[1]);
    EXPECT_EQ(19531U, config.pwm[0].frequency_hz);
    config.pwm[0] = {100, 25, 10, 10, false};
    config.pwm[1] = {1000, 10, 3, 4, true};
    const auto record = local_control::encodeRecord(config, 43);
    local_control::Config decoded{};
    EXPECT_TRUE(local_control::decodeRecord(record, decoded, seq));
    EXPECT_TRUE(local_control::equal(config, decoded));
    for (unsigned i = 0; i < 28; ++i) {
        auto broken = record;
        broken[i] ^= 1;
        EXPECT_TRUE(!local_control::decodeRecord(broken, decoded, seq));
    }
    std::array<local_control::Record, 2> slots{{old, record}};
    for (unsigned bytes = 0; bytes < 28; ++bytes) {
        slots[1].fill(0);
        for (unsigned i = 0; i < bytes; ++i)
            slots[1][i] = record[i];
        EXPECT_EQ(0, local_control::newestRecord(slots, decoded, seq));
    }
}
void testLocalOtaImageHeader() noexcept {
    std::array<char, 288> image{};
    image[0] = static_cast<char>(0xE9);
    image[32] = 0x32;
    image[33] = 0x54;
    image[34] = static_cast<char>(0xCD);
    image[35] = static_cast<char>(0xAB);
    std::strcpy(image.data() + 48, "0.9.1");
    std::strcpy(image.data() + 80, "led_controller");
    EXPECT_TRUE(
        local_ota_validation::header(image.data(), image.size(), "led_controller", "0.9.0"));
    EXPECT_TRUE(!local_ota_validation::header(image.data(), 287, "led_controller", "0.9.0"));
    EXPECT_TRUE(
        !local_ota_validation::header(image.data(), image.size(), "another_project", "0.9.0"));
    EXPECT_TRUE(
        !local_ota_validation::header(image.data(), image.size(), "led_controller", "0.9.1"));
    image[32] = 0;
    EXPECT_TRUE(
        !local_ota_validation::header(image.data(), image.size(), "led_controller", "0.9.0"));
    image[32] = 0x32;
    std::memset(image.data() + 48, 'x', 32);
    EXPECT_TRUE(
        !local_ota_validation::header(image.data(), image.size(), "led_controller", "0.9.0"));
}
void testLocalOtaButtonGesture() noexcept {
    ButtonGesture gesture(10000, 40);
    gesture.reset(false, 0);
    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, 100));
    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, 600));
    EXPECT_EQ(ButtonGesture::Event::short_press, gesture.update(false, 900));
    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, 1000));
    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, 10999));
    EXPECT_EQ(ButtonGesture::Event::long_press_started, gesture.update(true, 11000));
    EXPECT_EQ(ButtonGesture::Event::long_press_ended, gesture.update(false, 11001));
    gesture.reset(true, 12000);
    EXPECT_EQ(ButtonGesture::Event::none, gesture.update(true, 30000));
}

int main() {
    testPwmConstraints();
    testPwmNvramMigration();
    testLocalOtaImageHeader();
    testLocalOtaButtonGesture();
    testDebugSession();
    testDebugOverflowAndRenewal();
    testDebugDiscovery();
    testBacklightNightAndMotion();
    testBacklightTemporaryButtonAndPermanentOff();
    testBacklightOverflowAndConfigRecord();
    testLocalControlMappingAndParsing();
    testLocalControlGroupAndDimming();
    testPirAutomationTimersAndManualPriority();
    testPirAutomationOverflowAndDisable();
    testLocalConfigNvramRecovery();
    testLocalControlDiscovery();
    testPowerFadeDoesNotPublishIntermediateLevels();
    testNecDecoderCommand();
    testNecDecoderRepeatAndInvalidTiming();
    testIrRemoteRoleSelection();
    testIrRemoteTargetAndTransitionSelection();
    testIrRemoteRepeatPolicy();
    testSinglePressAndRelease();
    testBounceShorterThanThreshold();
    testPirActiveHighContactDebounce();
    testHeldButtonAtStartup();
    testTimeCounterOverflow();
    testChannelToggle();
    testBrightnessWhileOff();
    testShortPressGesture();
    testLongPressGesture();
    testGestureHeldAtStartup();
    testGestureTimeOverflow();
    testDimmingFromOff();
    testDimmingAlternatesAtLimits();
    testLinearDutyTransition();
    testDutyTransitionRetarget();
    testDutyTransitionTimeOverflow();
    testStatusIndicatorHeartbeat();
    testStatusIndicatorFaultLifecycle();
    testLcdDashboardLayout();
    testLcdDashboardMeasurementField();
    testLcdDashboardMeasurementRotationSkipsUnusedPages();
    testTemperatureModelCrcRomAndScratchpad();
    testIna228ScalingForTenMilliohmShunt();
    testLcdDashboardStatus();
    testLcdDashboardInformationRotation();
    testLcdDashboardNetworkPages();
    testLcdDashboardMqttPage();
    testLcdDashboardNoticeAndFaultPriority();
    testDeviceIdentityUsesNeutralChannelNames();
    testMqttCommandParser();
    testMqttCommandParserRejectsInvalidPayloads();
    testMqttPublishTrackerRetriesOnlyUnpublishedFields();
    testOtaRequestValidation();
    testSecureSettingsRecordLayout();
    testSecureSettingsRecordRejectsInvalidData();
    testSecureSettingsSequenceOrdering();
    testPersistentSettingsRoundTrip();
    testPersistentSettingsEmptyRecordUsesDefaults();
    testPersistentSettingsRejectsInvalidTemperatureAssignments();
    testPersistentSettingsRejectsMalformedAndOversizedData();

    if (failure_count != 0U) {
        std::printf("Host tests failed: %u\n", failure_count);
        return 1;
    }

    std::printf("Host tests passed: 62\n");
    return 0;
}
