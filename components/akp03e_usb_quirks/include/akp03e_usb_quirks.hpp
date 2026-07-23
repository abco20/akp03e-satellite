#pragma once

#include "usb/usb_host.h"

namespace akp03e::usb_quirks {

usb_host_config_t host_configuration() noexcept;
void patch_configuration_descriptor(const usb_config_desc_t* descriptor) noexcept;

}  // namespace akp03e::usb_quirks
