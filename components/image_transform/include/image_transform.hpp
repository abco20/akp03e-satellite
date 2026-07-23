#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace image_transform {

enum class Rotation : std::uint8_t {
    Deg0,
    Clockwise90,
    Deg180,
    Clockwise270,
};

// Rotates packed RGB888. For 90/270 degrees, destination dimensions are
// source_height x source_width. Source and destination must not overlap.
bool rotate_rgb888(
    std::span<const std::uint8_t> source,
    std::span<std::uint8_t> destination,
    std::size_t source_width,
    std::size_t source_height,
    Rotation rotation) noexcept;

}  // namespace image_transform
