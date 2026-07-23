#include "companion_client.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <string_view>
#include <utility>

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "companion_protocol.hpp"

namespace {
constexpr char kTag[] = "companion";
constexpr std::size_t kMaxLineSize = 20 * 1024;
constexpr std::int64_t kPingIntervalUs = 2'000'000;

bool supports_required_api(std::string_view version) {
    const auto first_dot = version.find('.');
    if (first_dot == std::string_view::npos) {
        return false;
    }
    const auto second_dot = version.find('.', first_dot + 1);
    const auto major_text = version.substr(0, first_dot);
    const auto minor_text = version.substr(
        first_dot + 1,
        second_dot == std::string_view::npos ? std::string_view::npos
                                             : second_dot - first_dot - 1);
    unsigned major = 0;
    unsigned minor = 0;
    const auto major_result = std::from_chars(
        major_text.data(), major_text.data() + major_text.size(), major);
    const auto minor_result = std::from_chars(
        minor_text.data(), minor_text.data() + minor_text.size(), minor);
    return major_result.ec == std::errc{} &&
           major_result.ptr == major_text.data() + major_text.size() &&
           minor_result.ec == std::errc{} &&
           minor_result.ptr == minor_text.data() + minor_text.size() &&
           (major > 1 || (major == 1 && minor >= 12));
}
}

CompanionClient::CompanionClient(
    WifiManager& wifi,
    SurfaceState& surface,
    QueueHandle_t input_queue,
    std::string host,
    std::uint16_t port)
    : wifi_(wifi),
      surface_(surface),
      input_queue_(input_queue),
      host_(std::move(host)),
      port_(port) {}

esp_err_t CompanionClient::start() {
    if (!encoder_.ready()) {
        ESP_LOGE(kTag, "JPEG encoder is unavailable");
        return ESP_FAIL;
    }
    if (xTaskCreatePinnedToCore(
            &CompanionClient::task_entry, "companion", 12288, this, 6, &task_, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void CompanionClient::task_entry(void* arg) {
    static_cast<CompanionClient*>(arg)->run();
}

void CompanionClient::run() {
    std::uint32_t backoff_ms = 1000;
    while (true) {
        xEventGroupWaitBits(wifi_.events(), WifiManager::kConnectedBit, pdFALSE, pdTRUE, portMAX_DELAY);
        const int fd = connect_socket();
        if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            backoff_ms = std::min<std::uint32_t>(backoff_ms * 2, 30000);
            continue;
        }

        ESP_LOGI(kTag, "connected to %s:%u", host_.c_str(), port_);
        backoff_ms = 1000;
        session(fd);
        shutdown(fd, SHUT_RDWR);
        close(fd);
        ESP_LOGW(kTag, "session ended");
    }
}

int CompanionClient::connect_socket() {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* addresses = nullptr;
    const auto port = std::to_string(port_);
    const int result = getaddrinfo(host_.c_str(), port.c_str(), &hints, &addresses);
    if (result != 0) {
        ESP_LOGW(kTag, "getaddrinfo failed: %d", result);
        return -1;
    }

    int fd = -1;
    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    return fd;
}

bool CompanionClient::session(int socket_fd) {
    const int flags = fcntl(socket_fd, F_GETFL, 0);
    fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);

    std::string receive_buffer;
    receive_buffer.reserve(kMaxLineSize);
    bool registered = false;
    std::uint64_t ping_counter = 0;
    std::int64_t next_ping = esp_timer_get_time() + kPingIntervalUs;

    while ((xEventGroupGetBits(wifi_.events()) & WifiManager::kConnectedBit) != 0) {
        akp03e::InputEvent event;
        while (xQueueReceive(input_queue_, &event, 0) == pdTRUE) {
            if (registered && !send_all(socket_fd, companion::input_event(CONFIG_AKP03E_DEVICE_ID, event))) {
                return false;
            }
        }

        const auto now = esp_timer_get_time();
        if (now >= next_ping) {
            if (!send_all(socket_fd, companion::ping(std::to_string(++ping_counter)))) {
                return false;
            }
            next_ping = now + kPingIntervalUs;
        }

        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(socket_fd, &read_set);
        timeval timeout{.tv_sec = 0, .tv_usec = 50'000};
        const int selected = select(socket_fd + 1, &read_set, nullptr, nullptr, &timeout);
        if (selected < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (selected == 0) {
            continue;
        }

        char buffer[1024];
        const int count = recv(socket_fd, buffer, sizeof(buffer), 0);
        if (count == 0) {
            return false;
        }
        if (count < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                continue;
            }
            return false;
        }

        receive_buffer.append(buffer, static_cast<std::size_t>(count));
        if (receive_buffer.size() > kMaxLineSize) {
            ESP_LOGW(kTag, "incoming line exceeded limit");
            return false;
        }

        while (true) {
            const auto newline = receive_buffer.find('\n');
            if (newline == std::string::npos) {
                break;
            }
            std::string line = receive_buffer.substr(0, newline);
            receive_buffer.erase(0, newline + 1);
            if (!process_line(socket_fd, line, registered)) {
                return false;
            }
        }
    }
    return false;
}

bool CompanionClient::send_all(int socket_fd, std::string_view data) {
    std::size_t offset = 0;
    const auto deadline = esp_timer_get_time() + 2'000'000;
    while (offset < data.size()) {
        const int sent = send(socket_fd, data.data() + offset, data.size() - offset, 0);
        if (sent > 0) {
            offset += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            if (esp_timer_get_time() >= deadline) {
                ESP_LOGW(kTag, "socket send timed out");
                return false;
            }
            fd_set write_set;
            FD_ZERO(&write_set);
            FD_SET(socket_fd, &write_set);
            timeval timeout{.tv_sec = 0, .tv_usec = 100'000};
            const int selected = select(socket_fd + 1, nullptr, &write_set, nullptr, &timeout);
            if (selected < 0 && errno != EINTR) {
                return false;
            }
            continue;
        }
        return false;
    }
    return true;
}

bool CompanionClient::process_line(int socket_fd, std::string_view line, bool& registered) {
    const auto message = companion::parse_line(line);
    if (!message) {
        ESP_LOGW(kTag, "could not parse line");
        return true;
    }

    if (message->command == "BEGIN") {
        const auto api_version = message->get("ApiVersion");
        if (!api_version || !supports_required_api(*api_version)) {
            ESP_LOGE(
                kTag,
                "Companion Satellite API 1.12 or newer is required; received %.*s",
                api_version ? static_cast<int>(api_version->size()) : 7,
                api_version ? api_version->data() : "missing");
            return false;
        }
        const auto registration = companion::add_device(
            CONFIG_AKP03E_DEVICE_ID, CONFIG_AKP03E_PRODUCT_NAME, CONFIG_AKP03E_DEVICE_SERIAL);
        if (!send_all(socket_fd, registration)) {
            return false;
        }
        return true;
    }
    if (message->command == "ADD-DEVICE" && line.find(" OK") != std::string_view::npos) {
        registered = true;
        ESP_LOGI(kTag, "surface registered");
        return true;
    }
    if (message->command == "PING") {
        // Echo the payload exactly. It is arbitrary protocol data and may contain spaces.
        const auto separator = line.find_first_of(" \t");
        const auto payload = separator == std::string_view::npos
                                 ? std::string_view{}
                                 : line.substr(separator + 1);
        return send_all(socket_fd, companion::pong(payload));
    }
    if (message->command == "KEY-STATE") {
        process_key_state(*message);
        return true;
    }
    if (message->command == "KEYS-CLEAR") {
        clear_keys();
        return true;
    }
    if (message->command == "BRIGHTNESS") {
        if (const auto value = message->get("VALUE")) {
            unsigned brightness = 0;
            const auto parsed = std::from_chars(value->data(), value->data() + value->size(), brightness);
            if (parsed.ec == std::errc{} && brightness <= 100) {
                surface_.set_brightness(static_cast<std::uint8_t>(brightness));
            }
        }
        return true;
    }
    if (message->command == "ERROR" || line.find(" ERROR ") != std::string_view::npos) {
        ESP_LOGW(kTag, "Companion error: %.*s", static_cast<int>(line.size()), line.data());
    }
    return true;
}

void CompanionClient::process_key_state(const companion::Message& message) {
    const auto control = message.get("CONTROLID");
    const auto bitmap = message.get("BITMAP");
    if (!control || !bitmap) {
        return;
    }
    const auto key = companion::display_key_from_control_id(*control);
    if (!key) {
        return;
    }
    encode_buffer_.key = *key;
    if (!encoder_.encode_companion_rgb_base64(*bitmap, encode_buffer_)) {
        return;
    }
    surface_.set_image(
        *key,
        std::span<const std::uint8_t>(encode_buffer_.data.data(), encode_buffer_.size));
}

void CompanionClient::clear_keys() {
    if (!encoder_.encode_black(encode_buffer_)) {
        return;
    }
    for (std::uint8_t key = 0; key < akp03e::kDisplayKeyCount; ++key) {
        surface_.set_image(key, std::span<const std::uint8_t>(encode_buffer_.data.data(), encode_buffer_.size));
    }
}
