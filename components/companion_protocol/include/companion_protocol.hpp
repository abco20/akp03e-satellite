#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "akp03e_protocol.hpp"

namespace companion {

struct Field {
    std::string name;
    std::string value;
};

struct Message {
    std::string command;
    std::vector<Field> fields;
    std::vector<std::string> positional;

    [[nodiscard]] std::optional<std::string_view> get(std::string_view name) const noexcept;
};

std::optional<Message> parse_line(std::string_view line);
std::string escape_quoted(std::string_view value);
std::string add_device(std::string_view device_id, std::string_view product_name, std::string_view serial);
std::string input_event(std::string_view device_id, const akp03e::InputEvent& event);
std::string ping(std::string_view payload);
std::string pong(std::string_view payload);
std::optional<std::uint8_t> display_key_from_control_id(std::string_view control_id) noexcept;

}  // namespace companion
