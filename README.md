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

In `menuconfig`, set the Wi-Fi credentials and Companion host under
`AKP03E Satellite`. Connect the AKP03E to the ESP32-S3 USB host pins with a
shared ground and a stable external 5 V VBUS supply. The surface registers
automatically when the firmware connects to Companion.

## License

MIT
