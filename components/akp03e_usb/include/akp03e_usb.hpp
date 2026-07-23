#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "usb/usb_host.h"

#include "akp03e_tx_sequence.hpp"
#include "akp03e_usb_descriptor.hpp"
#include "surface_state.hpp"

class Akp03eUsb {
public:
    explicit Akp03eUsb(SurfaceState& surface);
    esp_err_t start();
    QueueHandle_t input_queue() const noexcept { return input_queue_; }

private:
    enum class State : std::uint8_t {
        Waiting,
        FirstInit,
        ReopenDelay,
        SecondInit,
        Ready,
        Recovering,
    };

    static constexpr EventBits_t kHostReadyBit = BIT0;

    static void daemon_task_entry(void* arg);
    static void client_task_entry(void* arg);
    static void client_event_callback(const usb_host_client_event_msg_t* message, void* arg);
    static void input_transfer_callback(usb_transfer_t* transfer);
    static void output_transfer_callback(usb_transfer_t* transfer);

    void daemon_task();
    void client_task();
    void tick();
    void on_new_device(std::uint8_t address);
    void on_device_gone(usb_device_handle_t handle);

    bool open_device(std::uint8_t address);
    bool allocate_transfers();
    void free_transfers();
    void close_device_cleanly();
    void begin_recovery(const char* reason);
    void finish_recovery_if_possible();

    void start_next_output_job();
    void complete_job(const akp03e::TxCompletion& completion);
    void enter_ready();
    bool submit_current_report();
    bool submit_report(const akp03e::HidApiReport& report);
    bool submit_input();

    void configure_vbus();

    SurfaceState& surface_;
    EventGroupHandle_t host_events_{};
    QueueHandle_t input_queue_{};

    usb_host_client_handle_t client_handle_{};
    usb_device_handle_t device_handle_{};
    std::uint8_t device_address_{};
    akp03e::usb_descriptor::InterfaceInfo interface_{};
    bool interface_claimed_{};

    usb_transfer_t* input_transfer_{};
    usb_transfer_t* output_transfer_{};
    bool input_in_flight_{};
    bool output_in_flight_{};
    bool input_completed_{};
    bool output_completed_{};
    usb_transfer_status_t input_status_{};
    usb_transfer_status_t output_status_{};

    State state_{State::Waiting};
    akp03e::TxSequence tx_sequence_{};
    SurfaceState::ImageUpdate image_buffer_{};
    std::int64_t reopen_at_us_{};
    bool pending_device_gone_{};
    std::uint8_t pending_new_address_{};
};
