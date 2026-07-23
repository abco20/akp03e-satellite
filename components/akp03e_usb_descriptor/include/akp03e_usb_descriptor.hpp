#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace akp03e::usb_descriptor {

constexpr std::size_t kMaxHidInterfaces = 8;
constexpr std::size_t kMaxEndpointPatches = 8;

struct InterfaceInfo {
    std::uint8_t number{};
    std::uint8_t alternate{};
    std::uint8_t in_endpoint{};
    std::uint16_t in_mps{};
    std::uint8_t out_endpoint{};
    std::uint16_t out_mps{};

    friend bool operator==(const InterfaceInfo&, const InterfaceInfo&) = default;
};

struct ScanResult {
    std::array<InterfaceInfo, kMaxHidInterfaces> candidates{};
    std::size_t candidate_count{};
    std::optional<InterfaceInfo> selected;
    bool malformed{};
};

struct EndpointPatch {
    std::uint8_t endpoint{};
    std::uint16_t original_mps{};
    std::uint16_t patched_mps{};
    std::uint8_t original_interval{};
    std::uint8_t patched_interval{};
};

struct PatchResult {
    std::array<EndpointPatch, kMaxEndpointPatches> patches{};
    std::size_t patch_count{};
    bool malformed{};
};

ScanResult scan_hid_interfaces(
    std::span<const std::uint8_t> descriptor,
    int forced_interface) noexcept;

PatchResult patch_akp03e_hid_endpoints(
    std::span<std::uint8_t> descriptor) noexcept;

}  // namespace akp03e::usb_descriptor
