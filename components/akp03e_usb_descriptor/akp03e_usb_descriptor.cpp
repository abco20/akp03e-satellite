#include "akp03e_usb_descriptor.hpp"

#include <algorithm>

namespace akp03e::usb_descriptor {
namespace {
constexpr std::uint8_t kInterfaceDescriptorType = 0x04;
constexpr std::uint8_t kEndpointDescriptorType = 0x05;
constexpr std::uint8_t kHidClass = 0x03;
constexpr std::uint8_t kInterruptTransferType = 0x03;
constexpr std::uint8_t kEndpointDirectionIn = 0x80;
constexpr std::uint16_t kFullSpeedInterruptMps = 64;
constexpr std::uint8_t kMinimumInputPollIntervalMs = 10;
constexpr std::size_t kInterfaceDescriptorSize = 9;
constexpr std::size_t kEndpointDescriptorSize = 7;

std::uint16_t read_u16(const std::uint8_t* data) noexcept {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(data[1] << 8);
}

void write_u16(std::uint8_t* data, std::uint16_t value) noexcept {
    data[0] = static_cast<std::uint8_t>(value & 0xff);
    data[1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
}

bool is_interrupt_endpoint(const std::uint8_t* descriptor) noexcept {
    return (descriptor[3] & 0x03) == kInterruptTransferType;
}

bool is_in_endpoint(const std::uint8_t* descriptor) noexcept {
    return (descriptor[2] & kEndpointDirectionIn) != 0;
}
}  // namespace

ScanResult scan_hid_interfaces(
    std::span<const std::uint8_t> descriptor,
    int forced_interface) noexcept {
    ScanResult result;
    std::optional<InterfaceInfo> current;

    auto finish_current = [&]() {
        if (!current) {
            return;
        }
        const bool usable = current->in_endpoint != 0 && current->in_mps != 0 &&
                            current->out_endpoint != 0 && current->out_mps != 0;
        if (usable && result.candidate_count < result.candidates.size()) {
            result.candidates[result.candidate_count++] = *current;
        }
        current.reset();
    };

    for (std::size_t offset = 0; offset + 2 <= descriptor.size();) {
        const auto length = descriptor[offset];
        const auto type = descriptor[offset + 1];
        if (length < 2 || offset + length > descriptor.size()) {
            result.malformed = true;
            break;
        }

        const auto* item = descriptor.data() + offset;
        if (type == kInterfaceDescriptorType && length >= kInterfaceDescriptorSize) {
            finish_current();
            const auto interface_number = item[2];
            const auto alternate = item[3];
            const auto interface_class = item[5];
            if (interface_class == kHidClass && alternate == 0) {
                current = InterfaceInfo{
                    .number = interface_number,
                    .alternate = alternate,
                };
            }
        } else if (type == kEndpointDescriptorType &&
                   length >= kEndpointDescriptorSize && current &&
                   is_interrupt_endpoint(item)) {
            const auto endpoint = item[2];
            const auto mps = read_u16(item + 4) & 0x07ff;
            if (is_in_endpoint(item)) {
                current->in_endpoint = endpoint;
                current->in_mps = mps;
            } else {
                current->out_endpoint = endpoint;
                current->out_mps = mps;
            }
        }
        offset += length;
    }
    finish_current();

    for (std::size_t index = 0; index < result.candidate_count; ++index) {
        const auto& candidate = result.candidates[index];
        if (forced_interface >= 0 &&
            candidate.number != static_cast<std::uint8_t>(forced_interface)) {
            continue;
        }
        if (!result.selected || candidate.in_mps > result.selected->in_mps) {
            result.selected = candidate;
        }
    }
    return result;
}

PatchResult patch_akp03e_hid_endpoints(
    std::span<std::uint8_t> descriptor) noexcept {
    PatchResult result;
    bool hid_interface = false;

    for (std::size_t offset = 0; offset + 2 <= descriptor.size();) {
        const auto length = descriptor[offset];
        const auto type = descriptor[offset + 1];
        if (length < 2 || offset + length > descriptor.size()) {
            result.malformed = true;
            break;
        }

        auto* item = descriptor.data() + offset;
        if (type == kInterfaceDescriptorType && length >= kInterfaceDescriptorSize) {
            hid_interface = item[5] == kHidClass;
        } else if (hid_interface && type == kEndpointDescriptorType &&
                   length >= kEndpointDescriptorSize && is_interrupt_endpoint(item)) {
            const auto endpoint = item[2];
            const auto original_mps = static_cast<std::uint16_t>(
                read_u16(item + 4) & 0x07ff);
            const auto original_interval = item[6];
            auto patched_mps = original_mps;
            auto patched_interval = original_interval;

            if (!is_in_endpoint(item) && original_mps > kFullSpeedInterruptMps) {
                patched_mps = kFullSpeedInterruptMps;
                const auto preserved_flags = read_u16(item + 4) & 0xf800;
                write_u16(item + 4, preserved_flags | patched_mps);
            }
            if (is_in_endpoint(item) && original_interval < kMinimumInputPollIntervalMs) {
                patched_interval = kMinimumInputPollIntervalMs;
                item[6] = patched_interval;
            }

            if ((patched_mps != original_mps || patched_interval != original_interval) &&
                result.patch_count < result.patches.size()) {
                result.patches[result.patch_count++] = EndpointPatch{
                    .endpoint = endpoint,
                    .original_mps = original_mps,
                    .patched_mps = patched_mps,
                    .original_interval = original_interval,
                    .patched_interval = patched_interval,
                };
            }
        }
        offset += length;
    }
    return result;
}

}  // namespace akp03e::usb_descriptor
