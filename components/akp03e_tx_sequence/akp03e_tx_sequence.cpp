#include "akp03e_tx_sequence.hpp"

#include <algorithm>

namespace akp03e {

void TxSequence::reset() noexcept {
    active_ = false;
    step_ = 0;
    image_ = {};
}

void TxSequence::start_initialization(std::uint8_t brightness) noexcept {
    reset();
    active_ = true;
    kind_ = TxKind::Initialization;
    brightness_ = brightness;
}

void TxSequence::start_brightness(
    std::uint8_t brightness,
    std::uint32_t generation) noexcept {
    reset();
    active_ = true;
    kind_ = TxKind::Brightness;
    brightness_ = brightness;
    generation_ = generation;
}

bool TxSequence::start_image(
    std::uint8_t key,
    std::span<const std::uint8_t> jpeg,
    std::uint32_t generation) noexcept {
    reset();
    if (jpeg.empty() || key >= kDisplayKeyCount) {
        return false;
    }
    active_ = true;
    kind_ = TxKind::Image;
    key_ = key;
    generation_ = generation;
    image_ = jpeg;
    return true;
}

HidApiReport TxSequence::current_report() const noexcept {
    if (!active_) {
        return {};
    }

    switch (kind_) {
    case TxKind::Initialization:
        switch (step_) {
        case 0: return build_init_report();
        case 1: return build_brightness_report(brightness_);
        case 2: return build_clear_report();
        default: return build_flush_report();
        }
    case TxKind::Brightness:
        return build_brightness_report(brightness_);
    case TxKind::Image:
        if (step_ == 0) {
            return build_image_announce_report(key_, image_.size());
        }
        if (step_ <= image_chunk_count()) {
            const auto offset = (step_ - 1) * kUsbPayloadSize;
            const auto count = std::min(kUsbPayloadSize, image_.size() - offset);
            return build_image_chunk_report(image_.subspan(offset, count));
        }
        return build_flush_report();
    }
    return {};
}

std::optional<TxCompletion> TxSequence::advance() noexcept {
    if (!active_) {
        return std::nullopt;
    }
    ++step_;
    if (!finished()) {
        return std::nullopt;
    }

    const auto completion = TxCompletion{
        .kind = kind_,
        .key = key_,
        .generation = generation_,
    };
    reset();
    return completion;
}

std::size_t TxSequence::image_chunk_count() const noexcept {
    return (image_.size() + kUsbPayloadSize - 1) / kUsbPayloadSize;
}

bool TxSequence::finished() const noexcept {
    switch (kind_) {
    case TxKind::Initialization:
        return step_ >= 4;
    case TxKind::Brightness:
        return step_ >= 1;
    case TxKind::Image:
        return step_ > image_chunk_count() + 1;
    }
    return true;
}

}  // namespace akp03e
