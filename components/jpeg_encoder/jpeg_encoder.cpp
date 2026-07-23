#include "jpeg_encoder.hpp"

#include <algorithm>
#include <cstring>

#include "sdkconfig.h"
#include "esp_log.h"
#include "mbedtls/base64.h"

#include "akp03e_protocol.hpp"
#include "image_transform.hpp"

namespace {
constexpr char kTag[] = "jpeg_encoder";
constexpr std::size_t kRgbSize = akp03e::kDisplayWidth * akp03e::kDisplayHeight * 3;
}

JpegEncoder::JpegEncoder() {
    decoded_ = static_cast<std::uint8_t*>(jpeg_calloc_align(kRgbSize, 16));
    rotated_ = static_cast<std::uint8_t*>(jpeg_calloc_align(kRgbSize, 16));
    if (decoded_ == nullptr || rotated_ == nullptr) {
        ESP_LOGE(kTag, "failed to allocate RGB buffers");
        return;
    }

    jpeg_enc_config_t config = DEFAULT_JPEG_ENC_CONFIG();
    config.width = akp03e::kDisplayWidth;
    config.height = akp03e::kDisplayHeight;
    config.src_type = JPEG_PIXEL_FORMAT_RGB888;
    config.subsampling = JPEG_SUBSAMPLE_420;
    config.quality = CONFIG_AKP03E_JPEG_QUALITY;
    config.rotate = JPEG_ROTATE_0D;
    config.task_enable = false;

    const auto result = jpeg_enc_open(&config, &encoder_);
    if (result != JPEG_ERR_OK) {
        ESP_LOGE(kTag, "jpeg_enc_open failed: %d", static_cast<int>(result));
        encoder_ = nullptr;
    }
}

JpegEncoder::~JpegEncoder() {
    if (encoder_ != nullptr) {
        jpeg_enc_close(encoder_);
    }
    if (decoded_ != nullptr) {
        jpeg_free_align(decoded_);
    }
    if (rotated_ != nullptr) {
        jpeg_free_align(rotated_);
    }
}

bool JpegEncoder::encode_companion_rgb_base64(std::string_view encoded, SurfaceState::ImageUpdate& output) {
    if (!ready()) {
        return false;
    }
    std::size_t decoded_size = 0;
    const auto result = mbedtls_base64_decode(
        decoded_, kRgbSize, &decoded_size,
        reinterpret_cast<const unsigned char*>(encoded.data()), encoded.size());
    if (result != 0 || decoded_size != kRgbSize) {
        ESP_LOGW(kTag, "invalid RGB bitmap base64 result=%d size=%u expected=%u", result,
                 static_cast<unsigned>(decoded_size), static_cast<unsigned>(kRgbSize));
        return false;
    }
    return encode_rgb(std::span<const std::uint8_t>(decoded_, decoded_size), output);
}

bool JpegEncoder::encode_black(SurfaceState::ImageUpdate& output) {
    if (!ready()) {
        return false;
    }
    std::memset(decoded_, 0, kRgbSize);
    return encode_rgb(std::span<const std::uint8_t>(decoded_, kRgbSize), output);
}

bool JpegEncoder::encode_rgb(std::span<const std::uint8_t> rgb, SurfaceState::ImageUpdate& output) {
    if (rgb.size() != kRgbSize) {
        return false;
    }
    constexpr auto rotation = [] {
#if CONFIG_AKP03E_IMAGE_ROTATION_0
        return image_transform::Rotation::Deg0;
#elif CONFIG_AKP03E_IMAGE_ROTATION_180
        return image_transform::Rotation::Deg180;
#elif CONFIG_AKP03E_IMAGE_ROTATION_270_CW
        return image_transform::Rotation::Clockwise270;
#else
        return image_transform::Rotation::Clockwise90;
#endif
    }();
    if (!image_transform::rotate_rgb888(
            rgb, std::span<std::uint8_t>(rotated_, kRgbSize),
            akp03e::kDisplayWidth, akp03e::kDisplayHeight, rotation)) {
        return false;
    }

    int output_size = 0;
    auto result = jpeg_enc_process(
        encoder_, rotated_, static_cast<int>(kRgbSize), output.data.data(),
        static_cast<int>(output.data.size()), &output_size);
    if (result != JPEG_ERR_OK) {
        ESP_LOGW(kTag, "JPEG encode failed result=%d", static_cast<int>(result));
        return false;
    }
    if (output_size <= 0 || static_cast<std::size_t>(output_size) > output.data.size()) {
        ESP_LOGW(kTag, "JPEG encoder returned invalid size=%d", output_size);
        return false;
    }
    output.size = static_cast<std::size_t>(output_size);
    return true;
}
