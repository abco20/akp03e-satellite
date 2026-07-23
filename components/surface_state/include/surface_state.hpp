#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "akp03e_protocol.hpp"

class SurfaceState {
public:
    static constexpr std::size_t kMaxJpegSize = CONFIG_AKP03E_MAX_JPEG_SIZE;

    struct ImageUpdate {
        std::uint8_t key{};
        std::uint32_t generation{};
        std::size_t size{};
        std::array<std::uint8_t, kMaxJpegSize> data{};
    };

    SurfaceState();
    ~SurfaceState();
    SurfaceState(const SurfaceState&) = delete;
    SurfaceState& operator=(const SurfaceState&) = delete;

    void set_consumer_task(TaskHandle_t task);
    bool set_image(std::uint8_t key, std::span<const std::uint8_t> jpeg);
    void set_brightness(std::uint8_t brightness);

    bool next_image_update(ImageUpdate& output);
    bool next_brightness_update(std::uint8_t& brightness, std::uint32_t& generation);
    void mark_image_applied(std::uint8_t key, std::uint32_t generation);
    void mark_brightness_applied(std::uint32_t generation);
    void invalidate_images();
    void invalidate_applied_state();

private:
    struct Slot {
        std::array<std::uint8_t, kMaxJpegSize> data{};
        std::size_t size{};
        std::uint32_t generation{};
        std::uint32_t applied_generation{};
        bool valid{};
    };

    void notify_consumer();

    SemaphoreHandle_t mutex_{};
    TaskHandle_t consumer_task_{};
    std::array<Slot, akp03e::kDisplayKeyCount> slots_{};
    std::uint8_t brightness_{CONFIG_AKP03E_INITIAL_BRIGHTNESS};
    std::uint32_t brightness_generation_{1};
    std::uint32_t brightness_applied_generation_{};
};
