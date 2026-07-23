# Architecture

## Data flow

```text
AKP03E
  │ USB interrupt IN/OUT
  ▼
akp03e_usb
  ├─ input events ───────────────► companion_client ─► Companion
  └─ display reports ◄──────────── surface_state ◄────┘
```

The ESP32-S3 acts only as a network-attached surface adapter.

## Component boundaries

- `akp03e_protocol`: Pure AKP03E report construction and input parsing.
- `akp03e_tx_sequence`: Pure initialization, brightness, and image transfer sequencing.
- `akp03e_usb_descriptor`: Pure USB descriptor scanning and AKP03E endpoint patching.
- `akp03e_usb_quirks`: ESP32-S3 FIFO configuration and the isolated descriptor mutation.
- `akp03e_usb`: ESP-IDF USB Host ownership, connection state, transfers, and recovery.
- `surface_state`: Latest-wins cache for six JPEG images and brightness.
- `companion_protocol`: Pure Companion Satellite line protocol.
- `companion_client`: Wi-Fi TCP session, bitmap decoding, and surface registration.
- `device_config`: NVS-backed Wi-Fi/Companion settings and the serial configuration console.
- `jpeg_encoder` / `image_transform`: RGB888 rotation and JPEG encoding.
- `wifi_manager`: Wi-Fi station lifecycle.

## Runtime configuration

The application contains no compiled-in network fallback. On first boot it
waits for Wi-Fi and Companion settings to be entered through the external
USB-UART console. Settings are stored in the `akp03e_cfg` NVS namespace. The
network and USB surface tasks start only after both settings are present.

## Ownership rules

- Only the `akp03e_usb` task calls ESP-IDF USB client/device/transfer APIs.
- Transfer callbacks record completion only; cleanup and state transitions run in the USB task.
- `TxSequence` borrows the USB task's image buffer for the duration of one serialized transfer.
- Offline input events are discarded instead of replayed after reconnection.
- `SurfaceState` retains only the newest image for each display key.

## USB state machine

```text
Waiting
  └─ device found → FirstInit
FirstInit
  └─ init complete → close → ReopenDelay
ReopenDelay
  └─ 500 ms → reopen → SecondInit
SecondInit
  └─ init complete → Ready
Ready
  └─ disconnect/transfer failure → Recovering
Recovering
  └─ transfers quiesced and handles released → Waiting
```

The two-stage wake sequence and Interrupt OUT transport match the tested AKP03E firmware.
The HID endpoint descriptor quirk is applied before ESP-IDF allocates endpoint pipes.
