#include <memory>
#include <utility>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "akp03e_usb.hpp"
#include "companion_client.hpp"
#include "device_config.hpp"
#include "serial_config_console.hpp"
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

    static DeviceConfigStore config_store;
    static SerialConfigConsole config_console(config_store);
    ESP_ERROR_CHECK(config_console.start());

    DeviceConfig config;
    ESP_ERROR_CHECK(config_store.load(config));
    if (!config.complete()) {
        ESP_LOGW(kTag, "configuration required; use the serial console");
        return;
    }

    static SurfaceState surface_state;
    static WifiManager wifi;
    static Akp03eUsb usb(surface_state);
    static CompanionClient companion(
        wifi,
        surface_state,
        usb.input_queue(),
        std::move(config.companion_host),
        config.companion_port);

    ESP_LOGI(kTag, "starting Wi-Fi");
    ESP_ERROR_CHECK(wifi.start(config.wifi_ssid, config.wifi_password));

    ESP_LOGI(kTag, "starting USB host and AKP03E client");
    ESP_ERROR_CHECK(usb.start());

    ESP_LOGI(kTag, "starting Companion Satellite client");
    ESP_ERROR_CHECK(companion.start());
}
