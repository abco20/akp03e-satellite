#include "serial_config_console.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "esp_log.h"
#include "esp_system.h"

namespace {
constexpr char kTag[] = "config";
constexpr std::size_t kMaxCommandSize = 384;

bool split_arguments(std::string_view line, std::vector<std::string>& arguments) {
    std::size_t pos = 0;
    while (pos < line.size()) {
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
            ++pos;
        }
        if (pos == line.size()) {
            break;
        }

        std::string argument;
        char quote = '\0';
        while (pos < line.size()) {
            const char ch = line[pos++];
            if (quote == '\0' && std::isspace(static_cast<unsigned char>(ch))) {
                break;
            }
            if (ch == '\\' && pos < line.size()) {
                argument.push_back(line[pos++]);
                continue;
            }
            if (ch == '\'' || ch == '"') {
                if (quote == '\0') {
                    quote = ch;
                    continue;
                }
                if (quote == ch) {
                    quote = '\0';
                    continue;
                }
            }
            argument.push_back(ch);
        }
        if (quote != '\0') {
            return false;
        }
        arguments.push_back(std::move(argument));
    }
    return true;
}

bool valid_wifi_password(std::string_view password) {
    if (password.empty() || (password.size() >= 8 && password.size() <= 63)) {
        return true;
    }
    if (password.size() != 64) {
        return false;
    }
    for (const char ch : password) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

void print_help() {
    std::puts(
        "\nConfiguration commands:\n"
        "  wifi set \"SSID\" \"PASSWORD\"\n"
        "  companion set HOST PORT\n"
        "  config show\n"
        "  config reset\n"
        "  help\n"
        "Quote values containing spaces. Use an empty password for an open network.");
}
}

esp_err_t SerialConfigConsole::start() {
    if (xTaskCreate(
            &SerialConfigConsole::task_entry, "config_console", 6144, this, 4, &task_) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void SerialConfigConsole::task_entry(void* arg) {
    static_cast<SerialConfigConsole*>(arg)->run();
}

void SerialConfigConsole::run() {
    print_help();
    std::array<char, kMaxCommandSize> line{};
    bool prompt_visible = false;
    while (true) {
        if (!prompt_visible) {
            std::fputs("\nakp03e> ", stdout);
            std::fflush(stdout);
            prompt_visible = true;
        }
        if (std::fgets(line.data(), line.size(), stdin) == nullptr) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        prompt_visible = false;
        if (std::strchr(line.data(), '\n') == nullptr && !std::feof(stdin)) {
            int ch = 0;
            while ((ch = std::fgetc(stdin)) != '\n' && ch != EOF) {
            }
            std::puts("ERROR: command is too long");
            continue;
        }
        process_line(line.data());
    }
}

void SerialConfigConsole::process_line(char* line) {
    std::vector<std::string> args;
    if (!split_arguments(line, args)) {
        std::puts("ERROR: unmatched quote");
        return;
    }
    if (args.empty()) {
        return;
    }
    if (args[0] == "help") {
        print_help();
        return;
    }
    if (args.size() == 2 && args[0] == "config" && args[1] == "show") {
        show_config();
        return;
    }
    if (args.size() == 2 && args[0] == "config" && args[1] == "reset") {
        const esp_err_t err = store_.reset();
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "could not reset configuration: %s", esp_err_to_name(err));
            return;
        }
        std::puts("Configuration erased. Restarting...");
        std::fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    if (args.size() == 4 && args[0] == "wifi" && args[1] == "set") {
        if (args[2].empty() || args[2].size() > 32) {
            std::puts("ERROR: SSID must be 1 to 32 bytes");
            return;
        }
        if (!valid_wifi_password(args[3])) {
            std::puts("ERROR: password must be empty, 8-63 bytes, or a 64-digit hex PSK");
            return;
        }
        const esp_err_t err = store_.set_wifi(args[2], args[3]);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "could not save Wi-Fi configuration: %s", esp_err_to_name(err));
            return;
        }
        std::puts("Wi-Fi configuration saved.");
        restart_if_complete();
        return;
    }
    if (args.size() == 4 && args[0] == "companion" && args[1] == "set") {
        if (args[2].empty() || args[2].size() > 253) {
            std::puts("ERROR: Companion host must be 1 to 253 bytes");
            return;
        }
        unsigned port = 0;
        const auto parsed = std::from_chars(
            args[3].data(), args[3].data() + args[3].size(), port);
        if (parsed.ec != std::errc{} || parsed.ptr != args[3].data() + args[3].size() ||
            port == 0 || port > 65535) {
            std::puts("ERROR: port must be in the range 1-65535");
            return;
        }
        const esp_err_t err =
            store_.set_companion(args[2], static_cast<std::uint16_t>(port));
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "could not save Companion configuration: %s", esp_err_to_name(err));
            return;
        }
        std::puts("Companion configuration saved.");
        restart_if_complete();
        return;
    }
    std::puts("ERROR: unknown command; enter 'help'");
}

void SerialConfigConsole::show_config() const {
    DeviceConfig config;
    const esp_err_t err = store_.load(config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "could not load configuration: %s", esp_err_to_name(err));
        return;
    }
    std::printf(
        "Wi-Fi SSID: %s\n"
        "Wi-Fi password: %s\n",
        config.wifi_configured() ? config.wifi_ssid.c_str() : "<not set>",
        config.wifi_configured() ? "<set; hidden>" : "<not set>");
    if (config.companion_configured()) {
        std::printf("Companion: %s:%u\n", config.companion_host.c_str(), config.companion_port);
    } else {
        std::puts("Companion: <not set>");
    }
    std::printf("Status: %s\n", config.complete() ? "ready" : "configuration required");
}

void SerialConfigConsole::restart_if_complete() const {
    DeviceConfig config;
    const esp_err_t err = store_.load(config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "could not verify configuration: %s", esp_err_to_name(err));
        return;
    }
    if (!config.complete()) {
        std::puts("Configuration is incomplete; enter 'config show' for status.");
        return;
    }
    std::puts("Configuration complete. Restarting to apply...");
    std::fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}
