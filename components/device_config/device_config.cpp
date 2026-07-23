#include "device_config.hpp"

#include "nvs.h"

namespace {
constexpr char kNamespace[] = "akp03e_cfg";
constexpr char kWifiSsidKey[] = "wifi_ssid";
constexpr char kWifiPasswordKey[] = "wifi_pass";
constexpr char kCompanionHostKey[] = "comp_host";
constexpr char kCompanionPortKey[] = "comp_port";

esp_err_t read_string(nvs_handle_t handle, const char* key, std::string& value) {
    std::size_t size = 0;
    esp_err_t err = nvs_get_str(handle, key, nullptr, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        value.clear();
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    std::string buffer(size, '\0');
    err = nvs_get_str(handle, key, buffer.data(), &size);
    if (err != ESP_OK) {
        return err;
    }
    buffer.resize(size > 0 ? size - 1 : 0);
    value = std::move(buffer);
    return ESP_OK;
}
}

bool DeviceConfig::wifi_configured() const noexcept {
    return !wifi_ssid.empty();
}

bool DeviceConfig::companion_configured() const noexcept {
    return !companion_host.empty() && companion_port != 0;
}

bool DeviceConfig::complete() const noexcept {
    return wifi_configured() && companion_configured();
}

esp_err_t DeviceConfigStore::load(DeviceConfig& config) const {
    config = {};

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = read_string(handle, kWifiSsidKey, config.wifi_ssid);
    if (err == ESP_OK) {
        err = read_string(handle, kWifiPasswordKey, config.wifi_password);
    }
    if (err == ESP_OK) {
        err = read_string(handle, kCompanionHostKey, config.companion_host);
    }
    if (err == ESP_OK) {
        std::uint16_t port = 0;
        const esp_err_t port_err = nvs_get_u16(handle, kCompanionPortKey, &port);
        if (port_err == ESP_OK) {
            config.companion_port = port;
        } else if (port_err != ESP_ERR_NVS_NOT_FOUND) {
            err = port_err;
        }
    }
    nvs_close(handle);
    return err;
}

esp_err_t DeviceConfigStore::set_wifi(
    const std::string& ssid, const std::string& password) const {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, kWifiSsidKey, ssid.c_str());
    if (err == ESP_OK) {
        err = nvs_set_str(handle, kWifiPasswordKey, password.c_str());
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t DeviceConfigStore::set_companion(
    const std::string& host, std::uint16_t port) const {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(handle, kCompanionHostKey, host.c_str());
    if (err == ESP_OK) {
        err = nvs_set_u16(handle, kCompanionPortKey, port);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t DeviceConfigStore::reset() const {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
