#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "esp_jpeg_enc.h"

#include "surface_state.hpp"

class JpegEncoder {
public:
    JpegEncoder();
    ~JpegEncoder();
    JpegEncoder(const JpegEncoder&) = delete;
    JpegEncoder& operator=(const JpegEncoder&) = delete;

    bool ready() const noexcept { return encoder_ != nullptr; }
    bool encode_companion_rgb_base64(std::string_view encoded, SurfaceState::ImageUpdate& output);
    bool encode_black(SurfaceState::ImageUpdate& output);

private:
    bool encode_rgb(std::span<const std::uint8_t> rgb, SurfaceState::ImageUpdate& output);

    jpeg_enc_handle_t encoder_{};
    std::uint8_t* decoded_{};
    std::uint8_t* rotated_{};
};
