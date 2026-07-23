#include "akp03e_usb.hpp"

#include <cstring>
#include <span>

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "akp03e_usb_quirks.hpp"

namespace {
constexpr char kTag[] = "akp03e_usb";
constexpr std::int64_t kReopenDelayUs = 500'000;
}

Akp03eUsb::Akp03eUsb(SurfaceState& surface) : surface_(surface) {
    host_events_ = xEventGroupCreate();
    input_queue_ = xQueueCreate(32, sizeof(akp03e::InputEvent));
    configASSERT(host_events_ != nullptr);
    configASSERT(input_queue_ != nullptr);
}

esp_err_t Akp03eUsb::start() {
    configure_vbus();
    if (xTaskCreatePinnedToCore(
            &Akp03eUsb::daemon_task_entry, "usb_daemon", 4096, this, 8, nullptr, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreatePinnedToCore(
            &Akp03eUsb::client_task_entry, "akp03e_usb", 8192, this, 7, nullptr, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void Akp03eUsb::daemon_task_entry(void* arg) {
    static_cast<Akp03eUsb*>(arg)->daemon_task();
}

void Akp03eUsb::client_task_entry(void* arg) {
    static_cast<Akp03eUsb*>(arg)->client_task();
}

void Akp03eUsb::daemon_task() {
    auto config = akp03e::usb_quirks::host_configuration();
    ESP_ERROR_CHECK(usb_host_install(&config));
    xEventGroupSetBits(host_events_, kHostReadyBit);
    ESP_LOGI(kTag, "USB Host installed");

    while (true) {
        std::uint32_t flags = 0;
        const auto result = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (result != ESP_OK && result != ESP_ERR_TIMEOUT) {
            ESP_LOGE(kTag, "usb_host_lib_handle_events failed: %s", esp_err_to_name(result));
        }
    }
}

void Akp03eUsb::client_task() {
    xEventGroupWaitBits(host_events_, kHostReadyBit, pdFALSE, pdTRUE, portMAX_DELAY);

    usb_host_client_config_t config{};
    config.is_synchronous = false;
    config.max_num_event_msg = 8;
    config.async.client_event_callback = &Akp03eUsb::client_event_callback;
    config.async.callback_arg = this;
    ESP_ERROR_CHECK(usb_host_client_register(&config, &client_handle_));
    surface_.set_consumer_task(xTaskGetCurrentTaskHandle());

    ESP_LOGI(kTag, "USB client registered; waiting for AKP03E VID=%04x PID=%04x",
             akp03e::kVendorId, akp03e::kProductId);

    while (true) {
        const auto result = usb_host_client_handle_events(client_handle_, pdMS_TO_TICKS(20));
        if (result != ESP_OK && result != ESP_ERR_TIMEOUT) {
            ESP_LOGW(kTag, "client event handler: %s", esp_err_to_name(result));
        }
        tick();
        ulTaskNotifyTake(pdTRUE, 0);

        // Some firmware completes empty IN requests immediately. Yield so the
        // network/JPEG task cannot be starved by a high-priority USB loop.
        vTaskDelay(1);
    }
}

void Akp03eUsb::client_event_callback(
    const usb_host_client_event_msg_t* message,
    void* arg) {
    auto* self = static_cast<Akp03eUsb*>(arg);
    if (message->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        self->pending_new_address_ = message->new_dev.address;
    } else if (message->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        self->on_device_gone(message->dev_gone.dev_hdl);
    }
}

void Akp03eUsb::input_transfer_callback(usb_transfer_t* transfer) {
    auto* self = static_cast<Akp03eUsb*>(transfer->context);
    self->input_in_flight_ = false;
    self->input_completed_ = true;
    self->input_status_ = transfer->status;
}

void Akp03eUsb::output_transfer_callback(usb_transfer_t* transfer) {
    auto* self = static_cast<Akp03eUsb*>(transfer->context);
    self->output_in_flight_ = false;
    self->output_completed_ = true;
    self->output_status_ = transfer->status;
}

void Akp03eUsb::tick() {
    if (pending_device_gone_ && state_ != State::Recovering) {
        pending_device_gone_ = false;
        begin_recovery("device disconnected");
    }

    if (pending_new_address_ != 0 && state_ == State::Waiting) {
        const auto address = pending_new_address_;
        pending_new_address_ = 0;
        on_new_device(address);
    }

    if (input_completed_) {
        input_completed_ = false;
        if (input_status_ == USB_TRANSFER_STATUS_COMPLETED && state_ == State::Ready) {
            const auto report_size = static_cast<std::size_t>(input_transfer_->actual_num_bytes);
            const auto report = std::span<const std::uint8_t>(
                input_transfer_->data_buffer, report_size);
            if (const auto event = akp03e::parse_input_report(report)) {
                if (xQueueSend(input_queue_, &*event, 0) != pdTRUE) {
                    ESP_LOGW(kTag, "dropping input event: Companion queue full");
                }
            }
        } else if (input_status_ != USB_TRANSFER_STATUS_CANCELED) {
            begin_recovery("input transfer failed");
        }
    }

    if (output_completed_) {
        output_completed_ = false;
        if (output_status_ == USB_TRANSFER_STATUS_COMPLETED) {
            if (const auto completion = tx_sequence_.advance()) {
                complete_job(*completion);
            }
        } else if (output_status_ != USB_TRANSFER_STATUS_CANCELED) {
            begin_recovery("output transfer failed");
        }
    }

    if (state_ == State::Recovering) {
        finish_recovery_if_possible();
        return;
    }

    if (state_ == State::ReopenDelay && esp_timer_get_time() >= reopen_at_us_) {
        if (!open_device(device_address_)) {
            state_ = State::Waiting;
            device_address_ = 0;
            return;
        }
        state_ = State::SecondInit;
        tx_sequence_.start_initialization(CONFIG_AKP03E_INITIAL_BRIGHTNESS);
    }

    if (state_ == State::Ready) {
        if (!input_in_flight_) {
            submit_input();
        }
        if (tx_sequence_.idle() && !output_in_flight_) {
            start_next_output_job();
        }
    }

    if (!tx_sequence_.idle() && !output_in_flight_) {
        submit_current_report();
    }
}

void Akp03eUsb::on_new_device(std::uint8_t address) {
    if (!open_device(address)) {
        return;
    }
    device_address_ = address;
    state_ = State::FirstInit;
    tx_sequence_.start_initialization(CONFIG_AKP03E_INITIAL_BRIGHTNESS);
}

void Akp03eUsb::on_device_gone(usb_device_handle_t handle) {
    if (device_handle_ == handle) {
        // Endpoint cleanup can block; defer it until the event handler returns.
        pending_device_gone_ = true;
    }
}

bool Akp03eUsb::open_device(std::uint8_t address) {
    if (usb_host_device_open(client_handle_, address, &device_handle_) != ESP_OK) {
        return false;
    }

    const usb_device_desc_t* device_descriptor = nullptr;
    if (usb_host_get_device_descriptor(device_handle_, &device_descriptor) != ESP_OK ||
        device_descriptor->idVendor != akp03e::kVendorId ||
        device_descriptor->idProduct != akp03e::kProductId) {
        usb_host_device_close(client_handle_, device_handle_);
        device_handle_ = nullptr;
        return false;
    }

    const usb_config_desc_t* config = nullptr;
    if (usb_host_get_active_config_descriptor(device_handle_, &config) != ESP_OK) {
        close_device_cleanly();
        return false;
    }
    akp03e::usb_quirks::patch_configuration_descriptor(config);

    const auto scan = akp03e::usb_descriptor::scan_hid_interfaces(
        std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(config), config->wTotalLength),
        CONFIG_AKP03E_USB_INTERFACE);
    for (std::size_t index = 0; index < scan.candidate_count; ++index) {
        const auto& candidate = scan.candidates[index];
        ESP_LOGI(kTag, "HID candidate interface=%u alt=%u IN=0x%02x/%u OUT=0x%02x/%u",
                 candidate.number, candidate.alternate,
                 candidate.in_endpoint, candidate.in_mps,
                 candidate.out_endpoint, candidate.out_mps);
    }
    if (scan.malformed) {
        ESP_LOGW(kTag, "malformed USB configuration descriptor");
    }
    if (!scan.selected) {
        ESP_LOGE(kTag, "AKP03E found, but no usable HID interface was found");
        close_device_cleanly();
        return false;
    }
    interface_ = *scan.selected;

    const auto claim_result = usb_host_interface_claim(
        client_handle_, device_handle_, interface_.number, interface_.alternate);
    if (claim_result != ESP_OK) {
        ESP_LOGE(kTag, "interface claim failed: %s", esp_err_to_name(claim_result));
        close_device_cleanly();
        return false;
    }
    interface_claimed_ = true;

    if (!allocate_transfers()) {
        close_device_cleanly();
        return false;
    }

    ESP_LOGI(kTag, "AKP03E opened address=%u interface=%u IN=0x%02x/%u OUT=0x%02x/%u",
             address, interface_.number, interface_.in_endpoint, interface_.in_mps,
             interface_.out_endpoint, interface_.out_mps);
    return true;
}

bool Akp03eUsb::allocate_transfers() {
    if (usb_host_transfer_alloc(akp03e::kInputReportSize, 0, &input_transfer_) != ESP_OK) {
        return false;
    }
    if (usb_host_transfer_alloc(akp03e::kUsbPayloadSize, 0, &output_transfer_) != ESP_OK) {
        free_transfers();
        return false;
    }

    input_transfer_->device_handle = device_handle_;
    input_transfer_->bEndpointAddress = interface_.in_endpoint;
    input_transfer_->num_bytes = akp03e::kInputReportSize;
    input_transfer_->callback = &Akp03eUsb::input_transfer_callback;
    input_transfer_->context = this;

    output_transfer_->device_handle = device_handle_;
    output_transfer_->bEndpointAddress = interface_.out_endpoint;
    output_transfer_->callback = &Akp03eUsb::output_transfer_callback;
    output_transfer_->context = this;
    return true;
}

void Akp03eUsb::free_transfers() {
    if (input_transfer_ != nullptr) {
        usb_host_transfer_free(input_transfer_);
        input_transfer_ = nullptr;
    }
    if (output_transfer_ != nullptr) {
        usb_host_transfer_free(output_transfer_);
        output_transfer_ = nullptr;
    }
    input_in_flight_ = false;
    output_in_flight_ = false;
    input_completed_ = false;
    output_completed_ = false;
}

void Akp03eUsb::close_device_cleanly() {
    free_transfers();
    if (interface_claimed_ && device_handle_ != nullptr) {
        usb_host_interface_release(client_handle_, device_handle_, interface_.number);
        interface_claimed_ = false;
    }
    if (device_handle_ != nullptr) {
        usb_host_device_close(client_handle_, device_handle_);
        device_handle_ = nullptr;
    }
}

void Akp03eUsb::begin_recovery(const char* reason) {
    if (state_ == State::Recovering || device_handle_ == nullptr) {
        return;
    }
    ESP_LOGW(kTag, "recovering: %s", reason);
    state_ = State::Recovering;
    tx_sequence_.reset();
    if (input_in_flight_) {
        usb_host_endpoint_halt(device_handle_, interface_.in_endpoint);
        usb_host_endpoint_flush(device_handle_, interface_.in_endpoint);
    }
    if (output_in_flight_) {
        usb_host_endpoint_halt(device_handle_, interface_.out_endpoint);
        usb_host_endpoint_flush(device_handle_, interface_.out_endpoint);
    }
}

void Akp03eUsb::finish_recovery_if_possible() {
    if (input_in_flight_ || output_in_flight_) {
        return;
    }
    close_device_cleanly();
    state_ = State::Waiting;
    device_address_ = 0;
    pending_device_gone_ = false;
    surface_.invalidate_applied_state();
    ESP_LOGI(kTag, "recovery complete; waiting for device");
}

void Akp03eUsb::start_next_output_job() {
    std::uint8_t brightness = 0;
    std::uint32_t generation = 0;
    if (surface_.next_brightness_update(brightness, generation)) {
        tx_sequence_.start_brightness(brightness, generation);
        return;
    }

    if (!surface_.next_image_update(image_buffer_)) {
        return;
    }
    const auto image = std::span<const std::uint8_t>(
        image_buffer_.data.data(), image_buffer_.size);
    if (tx_sequence_.start_image(
            image_buffer_.key, image, image_buffer_.generation)) {
        ESP_LOGD(kTag, "sending key %u JPEG bytes=%u",
                 static_cast<unsigned>(image_buffer_.key),
                 static_cast<unsigned>(image_buffer_.size));
    }
}

void Akp03eUsb::complete_job(const akp03e::TxCompletion& completion) {
    switch (completion.kind) {
    case akp03e::TxKind::Initialization:
        if (state_ == State::FirstInit) {
            close_device_cleanly();
            state_ = State::ReopenDelay;
            reopen_at_us_ = esp_timer_get_time() + kReopenDelayUs;
        } else if (state_ == State::SecondInit) {
            enter_ready();
        }
        break;
    case akp03e::TxKind::Brightness:
        surface_.mark_brightness_applied(completion.generation);
        break;
    case akp03e::TxKind::Image:
        surface_.mark_image_applied(completion.key, completion.generation);
        ESP_LOGD(kTag, "sent key %u", static_cast<unsigned>(completion.key));
        break;
    }
}

void Akp03eUsb::enter_ready() {
    state_ = State::Ready;
    // Initialization already applied brightness; replay only cached images.
    surface_.invalidate_images();
    ESP_LOGI(kTag, "AKP03E ready");
}

bool Akp03eUsb::submit_current_report() {
    if (tx_sequence_.idle() || output_in_flight_) {
        return true;
    }
    return submit_report(tx_sequence_.current_report());
}

bool Akp03eUsb::submit_report(const akp03e::HidApiReport& report) {
    if (output_transfer_ == nullptr || device_handle_ == nullptr) {
        return false;
    }

    std::memcpy(
        output_transfer_->data_buffer,
        report.data() + 1,
        akp03e::kUsbPayloadSize);
    output_transfer_->device_handle = device_handle_;
    output_transfer_->bEndpointAddress = interface_.out_endpoint;
    output_transfer_->num_bytes = akp03e::kUsbPayloadSize;
    output_transfer_->flags = 0;

    const auto result = usb_host_transfer_submit(output_transfer_);
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "submit output failed: %s", esp_err_to_name(result));
        begin_recovery("output submit failed");
        return false;
    }
    output_in_flight_ = true;
    return true;
}

bool Akp03eUsb::submit_input() {
    if (input_transfer_ == nullptr || input_in_flight_ || state_ != State::Ready) {
        return false;
    }
    input_transfer_->device_handle = device_handle_;
    input_transfer_->bEndpointAddress = interface_.in_endpoint;
    input_transfer_->num_bytes = akp03e::kInputReportSize;
    input_transfer_->flags = 0;

    const auto result = usb_host_transfer_submit(input_transfer_);
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "submit input failed: %s", esp_err_to_name(result));
        begin_recovery("input submit failed");
        return false;
    }
    input_in_flight_ = true;
    return true;
}

void Akp03eUsb::configure_vbus() {
#if CONFIG_AKP03E_VBUS_ENABLE_GPIO >= 0
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << CONFIG_AKP03E_VBUS_ENABLE_GPIO;
    config.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_set_level(
        static_cast<gpio_num_t>(CONFIG_AKP03E_VBUS_ENABLE_GPIO), 1));
    vTaskDelay(pdMS_TO_TICKS(100));
#endif
}
