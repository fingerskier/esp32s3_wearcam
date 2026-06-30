# Smoke verification — 2026-06-29

Device: XIAO ESP32S3 Sense, firmware on branch `feat/wearcam-fw-dashboard`.

## Build / host verification (DONE)

- [x] `idf.py set-target esp32s3` + `idf.py build` succeeds for the full app
      (camera + ring buffer + WiFi + NimBLE + HTTP/embedded dashboard + mDNS).
      Final image `wearcam.bin` ≈ 0x111210 bytes, ~64% of the 3 MB app
      partition free.
- [x] Dashboard embedded via `EMBED_FILES` (`_binary_index_html_*`,
      `_binary_app_js_*`, `_binary_style_css_*`) — links clean.
- [x] Ring-buffer host unit tests pass: **24 checks, 0 failures**
      (`firmware/test_host`, `mingw32-make CC=gcc`).
- [x] Dashboard JS syntax-checks clean (`node --check`).

## On-device verification (DONE — 2026-06-30, board on COM12, OV3660, LAN "Niflheim", device 192.168.1.52)

- [x] Boots: serial shows `camera ready (SVGA q12)`, `http server up`, `stream server up (port 81)`, `mdns: wearcam.local`.
- [x] No creds → `provisioning AP 'wearcam-setup'` + BLE `advertising as WearCam`.
- [x] BLE scan shows service UUID `0000fe40-cc7a-482a-984a-7f2ed5b3e58f` (verified with nRF Connect — the C1 fix; pre-fix it advertised `000040fe-…`).
- [x] BLE provisioning sets creds → `STA got IP <ip>`; STATUS notifies `connected ip=<ip>` (pushed on GOT_IP, the I3 fix). Note: STATUS briefly reads the SoftAP `192.168.4.1` until DHCP completes, then updates to the LAN IP.
- [x] `GET http://<ip>/api/status` → 200, `"cam":true`, `buf_frames` ~240.
- [x] `GET http://<ip>/snapshot` → valid JPEG (SOI `ffd8` … EOI `ffd9`, ~20 KB).
- [x] `GET http://<ip>:81/stream` → live MJPEG (stream is on port **81** — the I1 fix). Single-viewer: `:81` is a single-task httpd, so one stream client at a time.
- [x] While streaming, `/api/status` (on `:80`) still responds — status poll not blocked by the stream (confirms I1).
- [x] `GET http://<ip>/clip` → multipart MJPEG of buffered frames (~1.8 MB; capture keeps running during the download — confirms I2).
- [x] `POST http://<ip>/api/config {"res":"VGA","fps":15,"quality":15}` → status reflects `640×480/15/15`; restored to SVGA/20/12.
- [x] `http://wearcam.local/` resolves on the LAN.

Results / notes:
- Camera/BLE/WiFi/mDNS APIs compile against ESP-IDF v5.3.2 + esp32-camera
  (managed component). One symbol clash resolved: public camera init renamed
  `cam_init` → `wearcam_cam_init` (esp32-camera exports an internal `cam_init`).
- **Runtime fix during smoke — WiFi modem-sleep:** the firmware originally left
  the default `WIFI_PS_MIN_MODEM`, so the radio slept between DTIM beacons and
  the device intermittently dropped out of clients' ARP caches — bursts of
  ~100% ping loss and `000` on both `:80` and `:81`, while the gateway held 0%
  loss. Fixed by `esp_wifi_set_ps(WIFI_PS_NONE)` in `wifi_start()`; after the
  fix the device pings 0% loss and both ports respond consistently. (Battery
  tradeoff noted in code; a future enhancement could drop to MIN_MODEM only
  while no client is streaming.)
- **Observed live `/stream` framerate ≈ 4–8 fps** (avg ~20 KB SVGA frames over
  2.4 GHz), below the configured 20 fps — camera PCLK/throughput tuning is a
  separate follow-up, not a connectivity defect.
- **Dashboard `/clip` UX gap:** the endpoint works, but the dashboard plays the
  clip inline with no save/scrub/replay control (tracked separately).
- **Network note:** "Niflheim" showed wireless client-isolation-like behavior at
  times (PC→gateway OK, PC→device ARP intermittently failing); the PS fix is the
  device-side mitigation.
