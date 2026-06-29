# esp32s3_wearcam — Firmware + Dashboard Design

Date: 2026-06-29
Status: Approved

## Goal

Wearable camera on the **Seeed Studio XIAO ESP32S3 Sense** that:

1. Captures live video from the onboard OV2640 camera.
2. Keeps a rolling ~1-minute buffer of recent video in PSRAM (RAM ring, no SD).
3. Broadcasts live video wirelessly over WiFi (STA mode, on the local network).
4. Is controllable over a Bluetooth (BLE) connection.
5. Hosts a configuration + preview dashboard (deployable to GitHub Pages, and also
   embedded in firmware and served from the device).

## Hardware / platform

- Board: XIAO ESP32S3 Sense (ESP32-S3, 8 MB PSRAM OPI, 8 MB flash, OV2640).
- Framework: **ESP-IDF** (C, CMake).
- Flash/monitor port: **COM11** (native USB-CDC).

## Connectivity model (decided)

- Device runs in **WiFi STA** mode, joining the user's network and getting a LAN IP.
- Clients reach it on the LAN via IP or mDNS `wearcam.local`.
- **Mixed-content caveat:** an HTTPS GitHub-Pages page cannot fetch/stream from the
  HTTP device. Resolution:
  - **Provisioning + control** from the GitHub-Pages dashboard happen over **BLE**
    (Web Bluetooth) — no HTTP needed, works from an HTTPS origin.
  - **Live video + full control UI** are served by the **device's own HTTP server**
    (same files, embedded in firmware) at `http://wearcam.local`.
- WiFi credentials are provisioned over **BLE with a SoftAP fallback**; stored in NVS.
  No recompile needed to change networks.

## Firmware architecture (`./firmware`, ESP-IDF C)

Modules (each a focused unit with a clear interface):

| Module | Responsibility | Key interface |
|---|---|---|
| `app_main` | Boot, NVS init, mode select (STA vs provisioning) | — |
| `camera` | OV2640 init + frame grab, runtime res/fps/quality | `cam_init()`, `cam_get_frame()`, `cam_set_config()` |
| `ring_buffer` | PSRAM ring of last ~60s JPEG frames (time + byte bounded), drop-oldest, mutex-guarded | `rb_init()`, `rb_push()`, `rb_dump()`, `rb_stats()` |
| `capture_task` | Pull frames at target fps → push to ring + feed live streamers | FreeRTOS task |
| `wifi` | STA connect from NVS, retry w/ backoff, fall back to provisioning | `wifi_start()`, events |
| `ble_prov` | BLE GATT: WiFi provisioning (SSID/pass/apply) + control (start/stop/status) | GATT service |
| `http_server` | REST + streaming + embedded dashboard | esp_http_server |
| `mdns` | Advertise `wearcam.local` | `mdns_start()` |

### HTTP endpoints

- `GET /` — embedded gzip dashboard (`index.html`/`app.js`/`style.css`).
- `GET /stream` — `multipart/x-mixed-replace` MJPEG live broadcast.
- `GET /snapshot` — single JPEG.
- `GET /clip` — dump current ring buffer (last ~60s) as MJPEG/AVI download.
- `GET /api/status` — JSON: `{ip, rssi, fps, res, quality, heap, psram_free, clients, recording}`.
- `POST /api/config` — set `{res, fps, quality}`.
- `GET /api/wifi` — scan APs / read current SSID.
- `POST /api/wifi` — set SSID/pass (also available via BLE).

### BLE GATT service

- Provisioning characteristics: `ssid` (write), `pass` (write), `apply` (write), `status` (read/notify: provisioning state, device IP after join).
- Control characteristics: `command` (write: start/stop/snapshot), `status` (read/notify: recording, fps, heap).

### Camera defaults

- Pixel format JPEG, **SVGA 800×600 @ ~20 fps, quality 12**. Runtime-tunable via
  `/api/config` and BLE. PSRAM frame buffers (≥2).

### Ring buffer

- Stores JPEG frames with capture timestamps in PSRAM.
- Bounded by **both** wall-clock (~60 s) and a byte budget (~4–6 MB) — whichever hits
  first triggers drop-oldest.
- Thread-safe (FreeRTOS mutex). `rb_dump()` serializes frames for `/clip`.
- **Volatile:** contents lost on power-off (accepted trade-off, no SD).

### Build config

- Custom `partitions.csv` (factory app + space for embedded www if not in flash rodata).
- `sdkconfig.defaults`: PSRAM OPI enabled, USB-CDC console, camera + BLE enabled.
- Dashboard embedded via CMake `EMBED_FILES` (gzipped) so `/` works offline.

## Dashboard architecture (`./dashboard`, vanilla JS, no build step)

Single-page app: `index.html` + `app.js` + `style.css`. No framework, no bundler — so
the exact same files deploy to GitHub Pages **and** embed in firmware.

**Origin-aware progressive UI** (one codebase, two roles):

- **Loaded over HTTPS (GitHub Pages):**
  - Web Bluetooth provisioning panel: scan/connect device, push WiFi SSID+pass, read
    back assigned IP, BLE control (start/stop/snapshot/status).
  - "Open device" link → `http://wearcam.local` once provisioned.
- **Loaded over HTTP (served from device):**
  - Live video `<img src="/stream">`, snapshot button, "download last 60 s" (`/clip`).
  - Config form (res/fps/quality → `POST /api/config`).
  - Status panel polling `/api/status`.
  - WiFi management (`/api/wifi`).

## Data flow

```
OV2640 -> capture_task -> ring_buffer (PSRAM, last 60s)
                       \-> per-client MJPEG (/stream)
control: HTTP REST (LAN)  OR  BLE (provisioning + commands)
```

## Error handling

- WiFi: connect retry with exponential backoff; after N failures → provisioning mode
  (SoftAP + BLE advertising).
- Camera init failure → report in `/api/status` + periodic retry; device still boots.
- Ring overflow → drop oldest frame(s) until within bounds.
- HTTP: per-client send buffers; drop slow/disconnected stream clients cleanly.

## Testing strategy

- **Host unit tests (real red/green TDD):** `ring_buffer` logic compiled on host —
  bounds enforcement (time + byte), drop-oldest ordering, wraparound, concurrent
  push/dump. This is the unit with pure logic and no hardware dependency.
- **On-device smoke verification:** build → flash COM11 → confirm via serial monitor +
  endpoints: boot OK, camera frames flowing, STA connect + IP, `GET /api/status` 200,
  `GET /stream` serves MJPEG, `GET /snapshot` valid JPEG, BLE service advertises, ring
  `/clip` downloads.

## Build / flash plan

1. Install ESP-IDF toolchain (Windows).
2. `idf.py set-target esp32s3`
3. `idf.py build`
4. `idf.py -p COM11 flash monitor`
5. Verify smoke checklist above.

## Out of scope (YAGNI)

- microSD recording / persistent segments (RAM ring only).
- Audio capture.
- Cloud upload / remote (off-LAN) access.
- Multi-device management.
