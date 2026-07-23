#include "companion_protocol.hpp"

#include <charconv>
#include <cctype>

namespace companion {
namespace {
constexpr std::string_view kLayoutManifestBase64 = "eyJzdHlsZVByZXNldHMiOnsiZGVmYXVsdCI6e30sImRpc3BsYXkiOnsiYml0bWFwIjp7InciOjYwLCJoIjo2MH19fSwiY29udHJvbHMiOnsiZW5jLzAiOnsicm93IjowLCJjb2x1bW4iOjB9LCJlbmMvMSI6eyJyb3ciOjAsImNvbHVtbiI6MX0sImVuYy8yIjp7InJvdyI6MCwiY29sdW1uIjoyfSwia2V5LzAiOnsicm93IjoxLCJjb2x1bW4iOjAsInN0eWxlUHJlc2V0IjoiZGlzcGxheSJ9LCJrZXkvMSI6eyJyb3ciOjEsImNvbHVtbiI6MSwic3R5bGVQcmVzZXQiOiJkaXNwbGF5In0sImtleS8yIjp7InJvdyI6MSwiY29sdW1uIjoyLCJzdHlsZVByZXNldCI6ImRpc3BsYXkifSwia2V5LzMiOnsicm93IjoyLCJjb2x1bW4iOjAsInN0eWxlUHJlc2V0IjoiZGlzcGxheSJ9LCJrZXkvNCI6eyJyb3ciOjIsImNvbHVtbiI6MSwic3R5bGVQcmVzZXQiOiJkaXNwbGF5In0sImtleS81Ijp7InJvdyI6MiwiY29sdW1uIjoyLCJzdHlsZVByZXNldCI6ImRpc3BsYXkifSwic2lkZS8wIjp7InJvdyI6MywiY29sdW1uIjowfSwic2lkZS8xIjp7InJvdyI6MywiY29sdW1uIjoxfSwic2lkZS8yIjp7InJvdyI6MywiY29sdW1uIjoyfX19";

void skip_spaces(std::string_view input, std::size_t& offset) {
    while (offset < input.size() && std::isspace(static_cast<unsigned char>(input[offset]))) {
        ++offset;
    }
}

std::optional<std::string> parse_value(std::string_view input, std::size_t& offset) {
    if (offset >= input.size()) {
        return std::string{};
    }
    if (input[offset] != '"') {
        const auto begin = offset;
        while (offset < input.size() && !std::isspace(static_cast<unsigned char>(input[offset]))) {
            ++offset;
        }
        return std::string(input.substr(begin, offset - begin));
    }

    ++offset;
    std::string value;
    while (offset < input.size()) {
        const char ch = input[offset++];
        if (ch == '"') {
            return value;
        }
        if (ch == '\\' && offset < input.size()) {
            value.push_back(input[offset++]);
        } else {
            value.push_back(ch);
        }
    }
    return std::nullopt;
}

std::string control_id(const akp03e::InputEvent& event) {
    if (event.type == akp03e::InputEventType::EncoderButton || event.type == akp03e::InputEventType::EncoderTurn) {
        return "enc/" + std::to_string(event.index);
    }
    if (event.index < akp03e::kDisplayKeyCount) {
        return "key/" + std::to_string(event.index);
    }
    return "side/" + std::to_string(event.index - akp03e::kDisplayKeyCount);
}
}  // namespace

std::optional<std::string_view> Message::get(std::string_view name) const noexcept {
    for (const auto& field : fields) {
        if (field.name == name) {
            return field.value;
        }
    }
    return std::nullopt;
}

std::optional<Message> parse_line(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }
    std::size_t offset = 0;
    skip_spaces(line, offset);
    if (offset == line.size()) {
        return std::nullopt;
    }

    const auto command_begin = offset;
    while (offset < line.size() && !std::isspace(static_cast<unsigned char>(line[offset]))) {
        ++offset;
    }

    Message message;
    message.command = std::string(line.substr(command_begin, offset - command_begin));

    while (offset < line.size()) {
        skip_spaces(line, offset);
        if (offset >= line.size()) {
            break;
        }

        const auto token_begin = offset;
        while (offset < line.size() && line[offset] != '=' && !std::isspace(static_cast<unsigned char>(line[offset]))) {
            ++offset;
        }

        if (offset < line.size() && line[offset] == '=') {
            const std::string name(line.substr(token_begin, offset - token_begin));
            ++offset;
            auto value = parse_value(line, offset);
            if (!value) {
                return std::nullopt;
            }
            message.fields.push_back(Field{name, std::move(*value)});
        } else {
            message.positional.emplace_back(line.substr(token_begin, offset - token_begin));
        }
    }

    return message;
}

std::string escape_quoted(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    for (const char ch : value) {
        if (ch == '"' || ch == '\\') {
            result.push_back('\\');
        }
        result.push_back(ch);
    }
    return result;
}

std::string add_device(std::string_view device_id, std::string_view product_name, std::string_view serial) {
    return "ADD-DEVICE DEVICEID=\"" + escape_quoted(device_id) +
           "\" PRODUCT_NAME=\"" + escape_quoted(product_name) +
           "\" SERIAL=\"" + escape_quoted(serial) +
           "\" SERIAL_IS_UNIQUE=0 BRIGHTNESS=1 BITMAP_FORMAT=rgb LAYOUT_MANIFEST=" +
           std::string(kLayoutManifestBase64) + "\n";
}

std::string input_event(std::string_view device_id, const akp03e::InputEvent& event) {
    const auto id = control_id(event);
    if (event.type == akp03e::InputEventType::EncoderTurn) {
        return "KEY-ROTATE DEVICEID=\"" + escape_quoted(device_id) +
               "\" CONTROLID=\"" + id + "\" DIRECTION=" +
               std::to_string(event.value < 0 ? -1 : 1) + "\n";
    }
    return "KEY-PRESS DEVICEID=\"" + escape_quoted(device_id) +
           "\" CONTROLID=\"" + id + "\" PRESSED=" +
           std::to_string(event.value != 0 ? 1 : 0) + "\n";
}

std::string ping(std::string_view payload) {
    return "PING " + std::string(payload) + "\n";
}

std::string pong(std::string_view payload) {
    return "PONG " + std::string(payload) + "\n";
}

std::optional<std::uint8_t> display_key_from_control_id(std::string_view control_id) noexcept {
    constexpr std::string_view prefix = "key/";
    if (!control_id.starts_with(prefix)) {
        return std::nullopt;
    }
    unsigned value = 0;
    const auto number = control_id.substr(prefix.size());
    const auto result = std::from_chars(number.data(), number.data() + number.size(), value);
    if (result.ec != std::errc{} || result.ptr != number.data() + number.size() || value >= akp03e::kDisplayKeyCount) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(value);
}

}  // namespace companion
