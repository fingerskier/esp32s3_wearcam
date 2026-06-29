# esp32s3_wearcam
Firmware for a wearable camera

## Versions
1. Seeed Studio XIAO ESP32S3 Sense

## Features
* auto-record rolling 1min input/video segments
* wireless video broadcast
* controllable via BT conx
* host a web-dashboard here (GH Pages) for configuring the device and previewing input/camera

## Build & Flash (firmware)

Requires ESP-IDF v5.x.

```bash
cd firmware
idf.py set-target esp32s3      # first time only
idf.py build
idf.py -p COM11 flash monitor  # adjust port
```

First boot has no WiFi credentials, so the device starts a `wearcam-setup`
SoftAP and advertises a **WearCam** BLE service. Open the dashboard
(GitHub Pages) on a Web-Bluetooth browser, connect, and send your WiFi
SSID/password. The device reboots into STA mode and is reachable at
`http://wearcam.local` (or its DHCP IP).

## Dashboard

`dashboard/` is a no-build vanilla SPA. Hosted on GitHub Pages it provisions
WiFi over Bluetooth; served from the device (`http://wearcam.local`) it shows
the live stream, snapshots, the last-60s clip download, and camera config.
The same files are embedded into the firmware.
