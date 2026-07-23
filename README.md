# AKP03E ESP32-S3 Companion Satellite

Firmware that connects an Ajazz AKP03E to an ESP32-S3 over USB and exposes it
as a Bitfocus Companion Advanced Satellite Surface over Wi-Fi.

This is an unofficial community project and is not affiliated with or endorsed
by Ajazz or Bitfocus.

## Usage

Install [ESP-IDF v5.5.4](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/get-started/)
and [mise](https://mise.jdx.dev/), then run:

```sh
mise install
mise run menuconfig
mise run build
mise run flash-monitor -- -p /dev/ttyUSB0
```

`menuconfig` contains only build-time device and hardware options; network
credentials are not compiled into the firmware.

On first boot, configure the device through the external USB-UART serial
console used for flashing and logs:

```text
wifi set "YOUR SSID" "YOUR PASSWORD"
companion set 192.168.1.10 16622
```

The device stores the settings in NVS and restarts automatically after both
commands have been entered. The firmware contains no fallback Wi-Fi or
Companion connection settings. Use `config show` to inspect the saved settings
(the password is never displayed), or `config reset` to erase them.

Connect the AKP03E to the ESP32-S3 USB host pins with a shared ground and a
stable external 5 V VBUS supply. The surface registers automatically when the
firmware connects to Companion.

## License

MIT
