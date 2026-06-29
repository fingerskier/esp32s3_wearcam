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

## On-device verification (PENDING HARDWARE)

> **Blocked:** at verification time no Espressif device (USB VID `303A`) was
> enumerated on the host. The only USB serial devices present were a
> Raspberry Pi Pico (`2E8A:0005`, COM11) and an unidentified `16D0:08C7`
> (COM4) — neither is the XIAO ESP32S3. A board in download mode would still
> appear as `303A`, so this is almost certainly a power-only USB-C cable, a
> loose connector, or an unplugged board rather than firmware. Flash with
> `idf.py -p <PORT> flash monitor` once the board enumerates as `303A`, then
> tick the items below.

- [ ] Boots: serial shows `camera ready`, `http server up`, `stream server up (port 81)`, `mdns: wearcam.local`.
- [ ] No creds → `provisioning AP 'wearcam-setup'` + BLE `advertising as WearCam`.
- [ ] BLE scan shows service UUID `0000fe40-cc7a-482a-984a-7f2ed5b3e58f` (verify with nRF Connect — this is the C1 fix; pre-fix it advertised `000040fe-…`).
- [ ] BLE provisioning sets creds → `STA got IP <ip>`; STATUS notifies `connected ip=<ip>` (pushed on GOT_IP, the I3 fix — dashboard "Open device" link appears).
- [ ] `GET http://<ip>/api/status` → 200, `"cam":true`, `buf_frames>0`.
- [ ] `GET http://<ip>/snapshot` → valid JPEG.
- [ ] `GET http://<ip>:81/stream` → live MJPEG in browser (stream is on port **81** — the I1 fix).
- [ ] While streaming, `/api/status` still responds (status poll not blocked by the stream — confirms I1).
- [ ] `GET http://<ip>/clip` → downloads multipart MJPEG of buffered frames (capture keeps running during a slow download — confirms I2).
- [ ] `POST http://<ip>/api/config {"res":"VGA","fps":15,"quality":15}` → stream resolution changes.
- [ ] `http://wearcam.local/` resolves on the LAN.

Results / notes:
- Camera/BLE/WiFi/mDNS APIs compile against ESP-IDF v5.3.2 + esp32-camera
  (managed component). One symbol clash resolved: public camera init renamed
  `cam_init` → `wearcam_cam_init` (esp32-camera exports an internal `cam_init`).
