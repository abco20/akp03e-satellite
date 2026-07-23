#include "akp03e_protocol.hpp"

#include <algorithm>
#include <array>
#include <initializer_list>

namespace akp03e {
namespace {
constexpr std::array<std::uint8_t, 5> kHeader{0x43, 0x52, 0x54, 0x00, 0x00};

HidApiReport build_command(std::initializer_list<std::uint8_t> command) noexcept {
    HidApiReport report{};
    std::copy(kHeader.begin(), kHeader.end(), report.begin() + 1);
    std::copy(command.begin(), command.end(), report.begin() + 6);
    return report;
}
}  // namespace

std::optional<InputEvent> parse_input_report(std::span<const std::uint8_t> report) noexcept {
    if (report.size() < 11 || report[0] == 0) {
        return std::nullopt;
    }

    const auto action = report[9];
    const auto state = report[10];

    if (action >= 0x01 && action <= 0x06) {
        return InputEvent{InputEventType::Button, static_cast<std::uint8_t>(action - 1), static_cast<std::int8_t>(state != 0)};
    }

    switch (action) {
    case 0x25:
        return InputEvent{InputEventType::Button, 6, static_cast<std::int8_t>(state != 0)};
    case 0x30:
        return InputEvent{InputEventType::Button, 7, static_cast<std::int8_t>(state != 0)};
    case 0x31:
        return InputEvent{InputEventType::Button, 8, static_cast<std::int8_t>(state != 0)};
    case 0x90:
        return InputEvent{InputEventType::EncoderTurn, 0, -1};
    case 0x91:
        return InputEvent{InputEventType::EncoderTurn, 0, 1};
    case 0x50:
        return InputEvent{InputEventType::EncoderTurn, 1, -1};
    case 0x51:
        return InputEvent{InputEventType::EncoderTurn, 1, 1};
    case 0x60:
        return InputEvent{InputEventType::EncoderTurn, 2, -1};
    case 0x61:
        return InputEvent{InputEventType::EncoderTurn, 2, 1};
    case 0x33:
        return InputEvent{InputEventType::EncoderButton, 0, static_cast<std::int8_t>(state != 0)};
    case 0x35:
        return InputEvent{InputEventType::EncoderButton, 1, static_cast<std::int8_t>(state != 0)};
    case 0x34:
        return InputEvent{InputEventType::EncoderButton, 2, static_cast<std::int8_t>(state != 0)};
    default:
        return std::nullopt;
    }
}

HidApiReport build_init_report() noexcept {
    return build_command({0x44, 0x49, 0x53, 0x00, 0x00});
}

HidApiReport build_brightness_report(std::uint8_t percent) noexcept {
    percent = std::min<std::uint8_t>(percent, 100);
    return build_command({0x4c, 0x49, 0x47, 0x00, 0x00, percent});
}

HidApiReport build_clear_report() noexcept {
    return build_command({0x43, 0x4c, 0x45, 0x00, 0x00, 0x00, 0xff});
}

HidApiReport build_flush_report() noexcept {
    return build_command({0x53, 0x54, 0x50});
}

HidApiReport build_image_announce_report(std::uint8_t key, std::size_t jpeg_size) noexcept {
    const auto high = static_cast<std::uint8_t>((jpeg_size >> 8) & 0xff);
    const auto low = static_cast<std::uint8_t>(jpeg_size & 0xff);
    return build_command({0x42, 0x41, 0x54, 0x00, 0x00, high, low, static_cast<std::uint8_t>(key + 1)});
}

HidApiReport build_image_chunk_report(std::span<const std::uint8_t> chunk) noexcept {
    HidApiReport report{};
    const auto count = std::min(chunk.size(), kUsbPayloadSize);
    std::copy_n(chunk.begin(), count, report.begin() + 1);
    return report;
}

}  // namespace akp03e
