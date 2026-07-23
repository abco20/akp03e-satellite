#pragma once

#include "device_config.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class SerialConfigConsole {
public:
    explicit SerialConfigConsole(const DeviceConfigStore& store) : store_(store) {}

    esp_err_t start();

private:
    static void task_entry(void* arg);
    void run();
    void process_line(char* line);
    void show_config() const;
    void restart_if_complete() const;

    const DeviceConfigStore& store_;
    TaskHandle_t task_{};
};
