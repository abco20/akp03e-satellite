# Hardware test checklist

## Wiring

- ESP32-S3 USB D-: GPIO19
- ESP32-S3 USB D+: GPIO20
- Shared ground between ESP32-S3 and AKP03E
- Stable external 5 V supply to AKP03E VBUS
- External USB-UART recommended for flashing and logs

Do not power AKP03E from the ESP32-S3 3.3 V rail.

## Bring-up

1. Build and flash with ESP-IDF v5.5.4.
2. Open the external USB-UART monitor and configure the device:

```text
wifi set "YOUR SSID" "YOUR PASSWORD"
companion set 192.168.1.10 16622
```

3. After the automatic restart, run `config show` and verify that the status is
   `ready`. The saved password must not be displayed.
4. Start without AKP03E and verify Wi-Fi and Companion connectivity.
5. Connect AKP03E and check for:

```text
USB Host installed
HID candidate interface=0 ...
AKP03E opened ...
AKP03E ready
surface registered
```

6. Verify all six display keys, three side keys, three encoders, and three encoder presses.
7. Verify display updates using Companion or `python3 tools/mock_companion.py`.

## Recovery tests

- Unplug and reconnect AKP03E during an image transfer.
- Restart Companion.
- Restart the Wi-Fi access point.
- Change Wi-Fi and Companion settings through the console and confirm that
  each change is applied after the automatic restart.
- Run `config reset`, confirm that networking no longer starts, then provision
  the device again.
- Confirm that cached images are replayed after USB reconnection.
- Run for an extended period and check for watchdog, stack, heap, or brownout errors.

## Descriptor differences

The tested firmware uses HID interface 0. If another revision differs, set
`AKP03E HID interface number` to `-1` for automatic selection and inspect the
candidate interface logs.
