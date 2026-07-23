#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "akp03e_protocol.hpp"
#include "akp03e_tx_sequence.hpp"
#include "akp03e_usb_descriptor.hpp"
#include "companion_protocol.hpp"
#include "image_transform.hpp"

namespace {
int failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

void test_input_parser() {
    std::array<std::uint8_t, 512> report{};
    report[0] = 1;
    report[9] = 0x91;
    report[10] = 1;
    check(akp03e::parse_input_report(report) ==
              akp03e::InputEvent{akp03e::InputEventType::EncoderTurn, 0, 1},
          "encoder clockwise");

    report[9] = 0x25;
    report[10] = 0;
    check(akp03e::parse_input_report(report) ==
              akp03e::InputEvent{akp03e::InputEventType::Button, 6, 0},
          "side key release");

    report[9] = 0xff;
    check(!akp03e::parse_input_report(report), "unknown input ignored");
}

void test_packets() {
    const auto brightness = akp03e::build_brightness_report(75);
    check(brightness.size() == 1025, "packet size");
    check(brightness[1] == 'C' && brightness[2] == 'R' && brightness[3] == 'T', "CRT header");
    check(brightness[6] == 'L' && brightness[11] == 75, "brightness command");

    const auto announce = akp03e::build_image_announce_report(2, 0x1234);
    check(announce[11] == 0x12 && announce[12] == 0x34 && announce[13] == 3,
          "image announcement");
}

void test_companion_parser() {
    const auto message = companion::parse_line(
        "KEY-STATE DEVICEID=abc CONTROLID=\"key/3\" BITMAP=YWJj COLOR=\"#001122\"");
    check(message.has_value(), "parse KEY-STATE");
    check(message->command == "KEY-STATE", "command parsed");
    check(message->get("CONTROLID") == std::optional<std::string_view>("key/3"), "quoted field");
    check(message->get("BITMAP") == std::optional<std::string_view>("YWJj"), "base64 field");
    check(companion::display_key_from_control_id("key/5") == 5, "control id mapping");
    check(!companion::display_key_from_control_id("side/0"), "non-display control ignored");

    const auto ping = companion::parse_line("PING 12345");
    check(ping && ping->positional.size() == 1 && ping->positional[0] == "12345", "PING payload");
}


void test_image_rotation() {
    // 2x2 image, using the red component as the label.
    const std::array<std::uint8_t, 12> source{
        1, 0, 0, 2, 0, 0,
        3, 0, 0, 4, 0, 0,
    };
    std::array<std::uint8_t, 12> destination{};

    check(image_transform::rotate_rgb888(
              source, destination, 2, 2, image_transform::Rotation::Clockwise90),
          "rotate RGB image clockwise 90");
    check(destination == std::array<std::uint8_t, 12>{
                             3, 0, 0, 1, 0, 0,
                             4, 0, 0, 2, 0, 0,
                         },
          "90 CW matches Pillow rotate(270)");

    check(image_transform::rotate_rgb888(
              source, destination, 2, 2, image_transform::Rotation::Deg180),
          "rotate RGB image 180");
    check(destination == std::array<std::uint8_t, 12>{
                             4, 0, 0, 3, 0, 0,
                             2, 0, 0, 1, 0, 0,
                         },
          "180 rotation");

    check(image_transform::rotate_rgb888(
              source, destination, 2, 2, image_transform::Rotation::Clockwise270),
          "rotate RGB image clockwise 270");
    check(destination == std::array<std::uint8_t, 12>{
                             2, 0, 0, 4, 0, 0,
                             1, 0, 0, 3, 0, 0,
                         },
          "270 CW rotation");
}


void test_tx_sequence() {
    akp03e::TxSequence sequence;
    sequence.start_initialization(60);
    check(sequence.current_report()[6] == 'D', "init sequence starts with DIS");
    check(!sequence.advance(), "init step 1 incomplete");
    check(sequence.current_report()[6] == 'L', "init sequence sends brightness");
    check(!sequence.advance(), "init step 2 incomplete");
    check(sequence.current_report()[6] == 'C', "init sequence clears displays");
    check(!sequence.advance(), "init step 3 incomplete");
    check(sequence.current_report()[6] == 'S', "init sequence flushes");
    const auto init_done = sequence.advance();
    check(init_done && init_done->kind == akp03e::TxKind::Initialization,
          "init sequence completes after four reports");
    check(sequence.idle(), "sequence becomes idle");

    std::vector<std::uint8_t> jpeg(1500);
    for (std::size_t index = 0; index < jpeg.size(); ++index) {
        jpeg[index] = static_cast<std::uint8_t>(index & 0xff);
    }
    check(sequence.start_image(2, jpeg, 42), "image sequence starts");
    check(sequence.current_report()[6] == 'B', "image sequence announces image");
    check(!sequence.advance(), "image announce incomplete");
    const auto first_chunk = sequence.current_report();
    check(first_chunk[1] == jpeg[0] && first_chunk[1024] == jpeg[1023],
          "first image chunk contains 1024 bytes");
    check(!sequence.advance(), "first image chunk incomplete");
    const auto second_chunk = sequence.current_report();
    check(second_chunk[1] == jpeg[1024] && second_chunk[476] == jpeg[1499],
          "second image chunk contains remainder");
    check(!sequence.advance(), "second image chunk incomplete");
    check(sequence.current_report()[6] == 'S', "image sequence flushes");
    const auto image_done = sequence.advance();
    check(image_done && image_done->kind == akp03e::TxKind::Image &&
              image_done->key == 2 && image_done->generation == 42,
          "image sequence reports completion metadata");
}

void test_usb_descriptor() {
    // Configuration + HID interface + HID descriptor + IN endpoint + OUT endpoint.
    std::array<std::uint8_t, 41> descriptor{
        9, 2, 41, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 2, 3, 0, 0, 0,
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, 0, 0,
        7, 5, 0x81, 3, 0x00, 0x02, 1,
        7, 5, 0x01, 3, 0x00, 0x04, 1,
    };

    const auto patch = akp03e::usb_descriptor::patch_akp03e_hid_endpoints(descriptor);
    check(!patch.malformed && patch.patch_count == 2, "descriptor quirks are patched");
    check(descriptor[33] == 10, "IN polling interval is raised");
    check(descriptor[38] == 64 && descriptor[39] == 0, "OUT MPS is clamped to 64");

    const auto scan = akp03e::usb_descriptor::scan_hid_interfaces(descriptor, 0);
    check(!scan.malformed && scan.candidate_count == 1, "HID interface is discovered");
    check(scan.selected == akp03e::usb_descriptor::InterfaceInfo{
                               .number = 0,
                               .alternate = 0,
                               .in_endpoint = 0x81,
                               .in_mps = 512,
                               .out_endpoint = 0x01,
                               .out_mps = 64,
                           },
          "patched HID interface is selected");
    check(!akp03e::usb_descriptor::scan_hid_interfaces(descriptor, 1).selected,
          "forced interface filter is honored");
}

void test_companion_output() {
    const auto press = companion::input_event(
        "dev", akp03e::InputEvent{akp03e::InputEventType::Button, 1, 1});
    check(press == "KEY-PRESS DEVICEID=\"dev\" CONTROLID=\"key/1\" PRESSED=1\n",
          "button output");

    const auto rotate = companion::input_event(
        "dev", akp03e::InputEvent{akp03e::InputEventType::EncoderTurn, 2, -1});
    check(rotate == "KEY-ROTATE DEVICEID=\"dev\" CONTROLID=\"enc/2\" DIRECTION=-1\n",
          "encoder output");

    const auto add = companion::add_device("dev", "AKP03E", "akp03e:test");
    check(add.find("LAYOUT_MANIFEST=") != std::string::npos, "advanced manifest included");
    check(add.find("BITMAP_FORMAT=rgb") != std::string::npos, "RGB bitmap requested");
}
}

int main() {
    test_input_parser();
    test_packets();
    test_companion_parser();
    test_companion_output();
    test_image_rotation();
    test_tx_sequence();
    test_usb_descriptor();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "all host tests passed\n";
    return EXIT_SUCCESS;
}
