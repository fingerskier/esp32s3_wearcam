# WearCam Dashboard

Single-page, no build step. Two roles from one codebase:

- **Hosted (GitHub Pages, https):** Web Bluetooth WiFi provisioning. Connect the
  device, send SSID/password, then follow the "Open device" link.
- **On-device (http, served by firmware):** live MJPEG stream, snapshot,
  download-last-60s, and resolution/fps/quality config.

These files are also copied into `firmware/main/www/` and embedded into the
firmware, so the device serves an identical UI offline.

Deploy: push to `main`, enable GitHub Pages on `/dashboard` (or copy to `/docs`).
