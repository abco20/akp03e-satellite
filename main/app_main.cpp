#include <memory>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "akp03e_usb.hpp"
#include "companion_client.hpp"
#include "surface_state.hpp"
#include "wifi_manager.hpp"

namespace {
constexpr char kTag[] = "app";
}

extern "C" void app_main() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    static SurfaceState surface_state;
    static WifiManager wifi;
    static Akp03eUsb usb(surface_state);
    static CompanionClient companion(wifi, surface_state, usb.input_queue());

    ESP_LOGI(kTag, "starting Wi-Fi");
    ESP_ERROR_CHECK(wifi.start());

    ESP_LOGI(kTag, "starting USB host and AKP03E client");
    ESP_ERROR_CHECK(usb.start());

    ESP_LOGI(kTag, "starting Companion Satellite client");
    ESP_ERROR_CHECK(companion.start());
}
