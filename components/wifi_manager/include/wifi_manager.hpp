#pragma once

#include <cstdint>

#include "esp_err.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

class WifiManager {
public:
    static constexpr EventBits_t kConnectedBit = BIT0;

    esp_err_t start();
    EventGroupHandle_t events() const noexcept { return events_; }

private:
    static void event_handler(void* arg, esp_event_base_t event_base, std::int32_t event_id, void* event_data);

    EventGroupHandle_t events_{};
};
