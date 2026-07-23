#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace akp03e {

constexpr std::uint16_t kVendorId = 0x0300;
constexpr std::uint16_t kProductId = 0x3002;
constexpr std::size_t kHidApiReportSize = 1025;
constexpr std::size_t kUsbPayloadSize = 1024;
constexpr std::size_t kInputReportSize = 512;
constexpr std::size_t kDisplayKeyCount = 6;
constexpr std::size_t kKeyCount = 9;
constexpr std::size_t kEncoderCount = 3;
constexpr std::size_t kDisplayWidth = 60;
constexpr std::size_t kDisplayHeight = 60;

using HidApiReport = std::array<std::uint8_t, kHidApiReportSize>;

enum class InputEventType : std::uint8_t {
    Button,
    EncoderButton,
    EncoderTurn,
};

struct InputEvent {
    InputEventType type{};
    std::uint8_t index{};
    std::int8_t value{};  // pressed=1/released=0, or encoder direction -1/+1

    friend bool operator==(const InputEvent&, const InputEvent&) = default;
};

std::optional<InputEvent> parse_input_report(std::span<const std::uint8_t> report) noexcept;

HidApiReport build_init_report() noexcept;
HidApiReport build_brightness_report(std::uint8_t percent) noexcept;
HidApiReport build_clear_report() noexcept;
HidApiReport build_flush_report() noexcept;
HidApiReport build_image_announce_report(std::uint8_t key, std::size_t jpeg_size) noexcept;
HidApiReport build_image_chunk_report(std::span<const std::uint8_t> chunk) noexcept;

}  // namespace akp03e
