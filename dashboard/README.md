# WearCam Dashboard

Single-page, no build step. One codebase, two tabs that degrade by context:

- **Live** — on the device (http) shows the MJPEG stream, snapshot,
  download-last-60s, and resolution/fps/quality config. On the hosted (https)
  page it shows a notice plus a "device IP → Open device" link (a hosted https
  page cannot stream from an http device — mixed content).
- **Setup (Bluetooth)** — on the hosted (https) page, Web Bluetooth WiFi
  provisioning: connect, send SSID/password, then follow "Open device". On the
  device's http page it shows a notice (Web Bluetooth needs a secure context).

The default tab is chosen by origin (device→Live, hosted→Setup); either tab is
always selectable. The last device IP and tab are remembered in localStorage.

Pure helpers in `app.js` are unit-tested: run `node --test dashboard/app.test.js`.

These files are also copied into `firmware/main/www/` and embedded into the
firmware, so the device serves an identical UI offline.

Deploy: push to `main`, enable GitHub Pages on `/dashboard` (or copy to `/docs`).
