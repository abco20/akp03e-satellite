#include "image_transform.hpp"

#include <algorithm>

namespace image_transform {

bool rotate_rgb888(
    std::span<const std::uint8_t> source,
    std::span<std::uint8_t> destination,
    std::size_t source_width,
    std::size_t source_height,
    Rotation rotation) noexcept {
    constexpr std::size_t kChannels = 3;
    const auto expected_size = source_width * source_height * kChannels;
    if (source.size() != expected_size || destination.size() < expected_size ||
        source.data() == destination.data()) {
        return false;
    }

    if (rotation == Rotation::Deg0) {
        std::copy(source.begin(), source.end(), destination.begin());
        return true;
    }

    const bool swaps_dimensions =
        rotation == Rotation::Clockwise90 || rotation == Rotation::Clockwise270;
    const auto destination_width = swaps_dimensions ? source_height : source_width;

    for (std::size_t source_y = 0; source_y < source_height; ++source_y) {
        for (std::size_t source_x = 0; source_x < source_width; ++source_x) {
            std::size_t destination_x = 0;
            std::size_t destination_y = 0;
            switch (rotation) {
            case Rotation::Deg0:
                break;
            case Rotation::Clockwise90:
                destination_x = source_height - 1 - source_y;
                destination_y = source_x;
                break;
            case Rotation::Deg180:
                destination_x = source_width - 1 - source_x;
                destination_y = source_height - 1 - source_y;
                break;
            case Rotation::Clockwise270:
                destination_x = source_y;
                destination_y = source_width - 1 - source_x;
                break;
            }

            const auto source_offset = (source_y * source_width + source_x) * kChannels;
            const auto destination_offset =
                (destination_y * destination_width + destination_x) * kChannels;
            destination[destination_offset + 0] = source[source_offset + 0];
            destination[destination_offset + 1] = source[source_offset + 1];
            destination[destination_offset + 2] = source[source_offset + 2];
        }
    }
    return true;
}

}  // namespace image_transform
