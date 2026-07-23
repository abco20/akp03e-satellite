#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "akp03e_protocol.hpp"

namespace akp03e {

enum class TxKind : std::uint8_t {
    Initialization,
    Brightness,
    Image,
};

struct TxCompletion {
    TxKind kind{};
    std::uint8_t key{};
    std::uint32_t generation{};
};

class TxSequence {
public:
    [[nodiscard]] bool idle() const noexcept { return !active_; }
    void reset() noexcept;

    void start_initialization(std::uint8_t brightness) noexcept;
    void start_brightness(std::uint8_t brightness, std::uint32_t generation) noexcept;
    bool start_image(
        std::uint8_t key,
        std::span<const std::uint8_t> jpeg,
        std::uint32_t generation) noexcept;

    [[nodiscard]] HidApiReport current_report() const noexcept;
    std::optional<TxCompletion> advance() noexcept;

private:
    [[nodiscard]] std::size_t image_chunk_count() const noexcept;
    [[nodiscard]] bool finished() const noexcept;

    bool active_{};
    TxKind kind_{TxKind::Initialization};
    std::size_t step_{};
    std::uint8_t brightness_{};
    std::uint8_t key_{};
    std::uint32_t generation_{};
    std::span<const std::uint8_t> image_{};
};

}  // namespace akp03e
