// Origin-aware: served from device (http) => live panel; hosted (https) => BLE setup.
const SVC  = '0000fe40-cc7a-482a-984a-7f2ed5b3e58f';
const C_SSID  = '0000fe41-cc7a-482a-984a-7f2ed5b3e58f';
const C_PASS  = '0000fe42-cc7a-482a-984a-7f2ed5b3e58f';
const C_APPLY = '0000fe43-cc7a-482a-984a-7f2ed5b3e58f';
const C_STAT  = '0000fe44-cc7a-482a-984a-7f2ed5b3e58f';

const $ = (id) => document.getElementById(id);
const enc = new TextEncoder();
// Device serves this file over http(s) from its own origin (not github.io).
const onDevice = !location.hostname.endsWith('github.io') && location.protocol.startsWith('http');

function setConn(text, ok) { const c = $('conn'); c.textContent = text; c.classList.toggle('ok', !!ok); }

if (onDevice) initLive(); else initBle();

// ---------- Device (HTTP) mode ----------
function initLive() {
  $('live-panel').hidden = false;
  let streaming = true;
  const img = $('stream');
  // Stream is served by a second httpd instance on port 81 so it can't block
  // the control endpoints (status poll, snapshot, config) on port 80.
  const streamUrl = `${location.protocol}//${location.hostname}:81/stream`;
  img.src = streamUrl;

  $('toggle').onclick = () => {
    streaming = !streaming;
    img.src = streaming ? streamUrl : '';
    $('toggle').textContent = streaming ? 'Pause' : 'Resume';
  };
  $('snap').onclick = () => window.open('/snapshot', '_blank');
  $('clip').onclick = () => window.open('/clip', '_blank');

  $('config-form').onsubmit = async (e) => {
    e.preventDefault();
    const body = JSON.stringify({
      res: $('res').value, fps: +$('fps').value, quality: +$('quality').value });
    await fetch('/api/config', { method:'POST', headers:{'Content-Type':'application/json'}, body });
  };

  async function poll() {
    try {
      const r = await fetch('/api/status');
      const s = await r.json();
      $('status').textContent = JSON.stringify(s, null, 2);
      setConn(`${s.ip} · ${s.rssi}dBm`, true);
    } catch { setConn('offline', false); }
    setTimeout(poll, 2000);
  }
  poll();
}

// ---------- Hosted (BLE) mode ----------
function initBle() {
  $('ble-panel').hidden = false;
  let chars = {};

  $('ble-connect').onclick = async () => {
    try {
      const dev = await navigator.bluetooth.requestDevice({ filters:[{ services:[SVC] }] });
      const gatt = await dev.gatt.connect();
      const svc = await gatt.getPrimaryService(SVC);
      chars.ssid  = await svc.getCharacteristic(C_SSID);
      chars.pass  = await svc.getCharacteristic(C_PASS);
      chars.apply = await svc.getCharacteristic(C_APPLY);
      chars.stat  = await svc.getCharacteristic(C_STAT);
      await chars.stat.startNotifications();
      chars.stat.addEventListener('characteristicvaluechanged', (e) => {
        const txt = new TextDecoder().decode(e.target.value);
        $('ble-status').textContent = txt;
        const m = txt.match(/(\d+\.\d+\.\d+\.\d+)/);
        if (m && m[1] !== '0.0.0.0') {
          const a = $('open-device');
          a.hidden = false; a.href = `http://${m[1]}/`;
          a.textContent = `Open device dashboard (http://${m[1]}) →`;
        }
      });
      $('wifi-form').hidden = false;
      setConn('BLE connected', true);
    } catch (err) { $('ble-status').textContent = 'BLE error: ' + err.message; }
  };

  $('wifi-form').onsubmit = async (e) => {
    e.preventDefault();
    await chars.ssid.writeValue(enc.encode($('ssid').value));
    await chars.pass.writeValue(enc.encode($('pass').value));
    await chars.apply.writeValue(Uint8Array.of(1));
    $('ble-status').textContent = 'Credentials sent; device joining WiFi…';
  };
}
