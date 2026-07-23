#include "akp03e_usb_quirks.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

#include "esp_log.h"

#include "akp03e_usb_descriptor.hpp"

namespace akp03e::usb_quirks {
namespace {
constexpr char kTag[] = "akp03e_usb";
}

usb_host_config_t host_configuration() noexcept {
    usb_host_config_t config{};
    config.skip_phy_setup = false;
    config.root_port_unpowered = false;
    config.intr_flags = ESP_INTR_FLAG_LEVEL1;

    // AKP03E declares a 512-byte interrupt IN endpoint. Allocate most of the
    // ESP32-S3 FIFO RAM to RX while retaining small periodic/non-periodic TX FIFOs.
    config.fifo_settings_custom.nptx_fifo_lines = 20;
    config.fifo_settings_custom.ptx_fifo_lines = 20;
    config.fifo_settings_custom.rx_fifo_lines = 160;
    return config;
}

void patch_configuration_descriptor(const usb_config_desc_t* descriptor) noexcept {
    if (descriptor == nullptr || descriptor->wTotalLength == 0) {
        return;
    }

    // ESP-IDF exposes the active descriptor as const, but the host stack later
    // consumes the same in-memory descriptor when allocating endpoint pipes.
    // AKP03E advertises an invalid 1024-byte Full-Speed interrupt OUT MPS, so
    // patch that device quirk in one isolated platform-specific location.
    auto bytes = std::span<std::uint8_t>(
        const_cast<std::uint8_t*>(reinterpret_cast<const std::uint8_t*>(descriptor)),
        descriptor->wTotalLength);
    const auto result = usb_descriptor::patch_akp03e_hid_endpoints(bytes);
    if (result.malformed) {
        ESP_LOGW(kTag, "malformed USB configuration descriptor while applying quirks");
    }
    for (std::size_t index = 0; index < result.patch_count; ++index) {
        const auto& patch = result.patches[index];
        if (patch.original_mps != patch.patched_mps) {
            ESP_LOGW(kTag, "clamping endpoint 0x%02x MPS %u to %u",
                     patch.endpoint, patch.original_mps, patch.patched_mps);
        }
        if (patch.original_interval != patch.patched_interval) {
            ESP_LOGW(kTag, "raising endpoint 0x%02x poll interval %u ms to %u ms",
                     patch.endpoint, patch.original_interval, patch.patched_interval);
        }
    }
}

}  // namespace akp03e::usb_quirks
