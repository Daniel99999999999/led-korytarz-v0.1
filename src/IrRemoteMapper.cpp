#include "IrRemoteMapper.hpp"

IrRemoteAction IrRemoteMapper::process(const nec_remote::Frame& frame,
                                       const std::uint32_t now_ms) noexcept {
    if (frame.type == nec_remote::FrameType::repeat) {
        return processRepeat(now_ms);
    }

    repeatable_command_valid_ = false;
    if ((frame.type != nec_remote::FrameType::command) || (frame.address_first != kAddressFirst) ||
        (frame.address_second != kAddressSecond) ||
        (static_cast<std::uint8_t>(frame.command ^ frame.command_inverse) != 0xFFU)) {
        return {};
    }

    const Command command = static_cast<Command>(frame.command);
    if (command == Command::select_white_controller) {
        active_role_ = Role::white_controller;
        return makeAction(IrRemoteAction::Type::white_role_selected);
    }
    if (command == Command::select_rgb_controller) {
        active_role_ = Role::rgb_controller;
        return makeAction(IrRemoteAction::Type::rgb_role_selected);
    }
    if (active_role_ != Role::white_controller) {
        return {};
    }

    switch (command) {
    case Command::power: {
        IrRemoteAction action = makeAction(IrRemoteAction::Type::master_power);
        action.target = IrRemoteAction::kAllChannelsTarget;
        return action;
    }
    case Command::rgb_toggle:
        return makeAction(IrRemoteAction::Type::target_toggle);
    case Command::night_mode:
        return makeAction(IrRemoteAction::Type::night_toggle);
    case Command::brightness_up:
        repeatable_command_ = command;
        repeatable_command_valid_ = true;
        repeat_started_at_ms_ = now_ms;
        last_repeat_frame_at_ms_ = now_ms;
        return makeAction(IrRemoteAction::Type::brightness_up);
    case Command::target_previous:
        selected_target_ = (selected_target_ == 0U)
                               ? IrRemoteAction::kAllChannelsTarget
                               : static_cast<std::uint8_t>(selected_target_ - 1U);
        return makeAction(IrRemoteAction::Type::target_changed);
    case Command::timer_30_minutes:
        return makeAction(IrRemoteAction::Type::timer_toggle);
    case Command::target_next:
        selected_target_ = (selected_target_ == IrRemoteAction::kAllChannelsTarget)
                               ? 0U
                               : static_cast<std::uint8_t>(selected_target_ + 1U);
        return makeAction(IrRemoteAction::Type::target_changed);
    case Command::brightness_down:
        repeatable_command_ = command;
        repeatable_command_valid_ = true;
        repeat_started_at_ms_ = now_ms;
        last_repeat_frame_at_ms_ = now_ms;
        return makeAction(IrRemoteAction::Type::brightness_down);
    case Command::scene_previous:
        return makeAction(IrRemoteAction::Type::scene_previous);
    case Command::scene_next:
        return makeAction(IrRemoteAction::Type::scene_next);
    case Command::cycle_transition:
        transition_index_ = (transition_index_ + 1U) % kTransitionOptionsMs.size();
        return makeAction(IrRemoteAction::Type::transition_changed);
    case Command::select_rgb_controller:
    case Command::select_white_controller:
        break;
    }

    return {};
}

IrRemoteMapper::Role IrRemoteMapper::activeRole() const noexcept {
    return active_role_;
}

std::uint8_t IrRemoteMapper::selectedTarget() const noexcept {
    return selected_target_;
}

std::uint32_t IrRemoteMapper::transitionMs() const noexcept {
    return kTransitionOptionsMs[transition_index_];
}

IrRemoteAction IrRemoteMapper::makeAction(const IrRemoteAction::Type type,
                                          const bool repeated) const noexcept {
    return IrRemoteAction{type, selected_target_, transitionMs(), repeated};
}

IrRemoteAction IrRemoteMapper::processRepeat(const std::uint32_t now_ms) noexcept {
    if (!repeatable_command_valid_ || (active_role_ != Role::white_controller) ||
        ((now_ms - last_repeat_frame_at_ms_) > kRepeatTimeoutMs)) {
        repeatable_command_valid_ = false;
        return {};
    }

    last_repeat_frame_at_ms_ = now_ms;
    // Krotki klik pilota czesto zawiera pierwszy Repeat; nie moze dawac drugiego kroku.
    if ((now_ms - repeat_started_at_ms_) < kRepeatHoldDelayMs) {
        return {};
    }

    if (repeatable_command_ == Command::brightness_up) {
        return makeAction(IrRemoteAction::Type::brightness_up, true);
    }
    if (repeatable_command_ == Command::brightness_down) {
        return makeAction(IrRemoteAction::Type::brightness_down, true);
    }

    repeatable_command_valid_ = false;
    return {};
}
