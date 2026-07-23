#include "surface_state.hpp"

#include <algorithm>

#include "esp_log.h"

namespace {
constexpr char kTag[] = "surface_state";
class LockGuard {
public:
    explicit LockGuard(SemaphoreHandle_t mutex) : mutex_(mutex) {
        xSemaphoreTake(mutex_, portMAX_DELAY);
    }
    ~LockGuard() { xSemaphoreGive(mutex_); }
private:
    SemaphoreHandle_t mutex_;
};
}  // namespace

SurfaceState::SurfaceState() : mutex_(xSemaphoreCreateMutex()) {
    configASSERT(mutex_ != nullptr);
}

SurfaceState::~SurfaceState() {
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

void SurfaceState::set_consumer_task(TaskHandle_t task) {
    LockGuard lock(mutex_);
    consumer_task_ = task;
}

bool SurfaceState::set_image(std::uint8_t key, std::span<const std::uint8_t> jpeg) {
    if (key >= slots_.size() || jpeg.empty() || jpeg.size() > kMaxJpegSize) {
        ESP_LOGW(kTag, "rejecting image key=%u size=%u max=%u", key,
                 static_cast<unsigned>(jpeg.size()), static_cast<unsigned>(kMaxJpegSize));
        return false;
    }
    {
        LockGuard lock(mutex_);
        auto& slot = slots_[key];
        std::copy(jpeg.begin(), jpeg.end(), slot.data.begin());
        slot.size = jpeg.size();
        slot.valid = true;
        ++slot.generation;
        if (slot.generation == 0) {
            slot.generation = 1;
        }
    }
    notify_consumer();
    return true;
}

void SurfaceState::set_brightness(std::uint8_t brightness) {
    brightness = std::min<std::uint8_t>(brightness, 100);
    {
        LockGuard lock(mutex_);
        if (brightness_ == brightness) {
            return;
        }
        brightness_ = brightness;
        ++brightness_generation_;
        if (brightness_generation_ == 0) {
            brightness_generation_ = 1;
        }
    }
    notify_consumer();
}

bool SurfaceState::next_image_update(ImageUpdate& output) {
    LockGuard lock(mutex_);
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        const auto& slot = slots_[index];
        if (!slot.valid || slot.generation == slot.applied_generation) {
            continue;
        }
        output.key = static_cast<std::uint8_t>(index);
        output.generation = slot.generation;
        output.size = slot.size;
        std::copy_n(slot.data.begin(), slot.size, output.data.begin());
        return true;
    }
    return false;
}

bool SurfaceState::next_brightness_update(std::uint8_t& brightness, std::uint32_t& generation) {
    LockGuard lock(mutex_);
    if (brightness_generation_ == brightness_applied_generation_) {
        return false;
    }
    brightness = brightness_;
    generation = brightness_generation_;
    return true;
}

void SurfaceState::mark_image_applied(std::uint8_t key, std::uint32_t generation) {
    if (key >= slots_.size()) {
        return;
    }
    LockGuard lock(mutex_);
    auto& slot = slots_[key];
    if (slot.generation == generation) {
        slot.applied_generation = generation;
    }
}

void SurfaceState::mark_brightness_applied(std::uint32_t generation) {
    LockGuard lock(mutex_);
    if (brightness_generation_ == generation) {
        brightness_applied_generation_ = generation;
    }
}

void SurfaceState::invalidate_images() {
    {
        LockGuard lock(mutex_);
        for (auto& slot : slots_) {
            slot.applied_generation = 0;
        }
    }
    notify_consumer();
}

void SurfaceState::invalidate_applied_state() {
    {
        LockGuard lock(mutex_);
        for (auto& slot : slots_) {
            slot.applied_generation = 0;
        }
        brightness_applied_generation_ = 0;
    }
    notify_consumer();
}

void SurfaceState::notify_consumer() {
    TaskHandle_t task = nullptr;
    {
        LockGuard lock(mutex_);
        task = consumer_task_;
    }
    if (task != nullptr) {
        xTaskNotifyGive(task);
    }
}
