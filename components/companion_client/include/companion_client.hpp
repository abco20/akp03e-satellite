#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "akp03e_protocol.hpp"
#include "companion_protocol.hpp"
#include "jpeg_encoder.hpp"
#include "surface_state.hpp"
#include "wifi_manager.hpp"

class CompanionClient {
public:
    CompanionClient(
        WifiManager& wifi,
        SurfaceState& surface,
        QueueHandle_t input_queue,
        std::string host,
        std::uint16_t port);
    esp_err_t start();

private:
    static void task_entry(void* arg);
    void run();
    int connect_socket();
    bool session(int socket_fd);
    bool send_all(int socket_fd, std::string_view data);
    bool process_line(int socket_fd, std::string_view line, bool& registered);
    void process_key_state(const companion::Message& message);
    void clear_keys();

    WifiManager& wifi_;
    SurfaceState& surface_;
    QueueHandle_t input_queue_{};
    std::string host_;
    std::uint16_t port_{};
    TaskHandle_t task_{};
    JpegEncoder encoder_{};
    SurfaceState::ImageUpdate encode_buffer_{};
};
