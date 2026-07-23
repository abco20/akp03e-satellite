#pragma once

#include <cstdint>
#include <string>

#include "esp_err.h"

struct DeviceConfig {
    std::string wifi_ssid;
    std::string wifi_password;
    std::string companion_host;
    std::uint16_t companion_port{};

    bool wifi_configured() const noexcept;
    bool companion_configured() const noexcept;
    bool complete() const noexcept;
};

class DeviceConfigStore {
public:
    esp_err_t load(DeviceConfig& config) const;
    esp_err_t set_wifi(const std::string& ssid, const std::string& password) const;
    esp_err_t set_companion(const std::string& host, std::uint16_t port) const;
    esp_err_t reset() const;
};
