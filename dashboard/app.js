// Origin-aware: served from device (http) => live panel; hosted (https) => BLE setup.
const SVC  = '0000fe40-cc7a-482a-984a-7f2ed5b3e58f';
const C_SSID  = '0000fe41-cc7a-482a-984a-7f2ed5b3e58f';
const C_PASS  = '0000fe42-cc7a-482a-984a-7f2ed5b3e58f';
const C_APPLY = '0000fe43-cc7a-482a-984a-7f2ed5b3e58f';
const C_STAT  = '0000fe44-cc7a-482a-984a-7f2ed5b3e58f';

// ---------- Pure helpers (unit-tested in app.test.js; no DOM access) ----------
function detectCaps({ hostname, protocol, isSecureContext, hasBluetooth }) {
  const onDevice = !hostname.endsWith('github.io') && protocol.startsWith('http');
  return { onDevice, canStream: onDevice, canBle: !!hasBluetooth && !!isSecureContext };
}
function defaultTab(caps) { return caps.onDevice ? 'live' : 'setup'; }
function extractIp(text) {
  const m = String(text).match(/(\d+\.\d+\.\d+\.\d+)/);
  return m && m[1] !== '0.0.0.0' ? m[1] : null;
}
function deviceUrl(ip) { return `http://${ip}/`; }

const $ = (id) => document.getElementById(id);
const enc = new TextEncoder();
const LS_TAB = 'wearcam.tab';
const LS_IP  = 'wearcam.deviceIp';

function setConn(text, ok) { const c = $('conn'); c.textContent = text; c.classList.toggle('ok', !!ok); }
function lsGet(k) { try { return localStorage.getItem(k); } catch { return null; } }
function lsSet(k, v) { try { localStorage.setItem(k, v); } catch { /* private mode */ } }

function selectTab(name) {
  const live = name === 'live';
  $('live-panel').hidden = !live;
  $('ble-panel').hidden  = live;
  $('tab-live').setAttribute('aria-selected', String(live));
  $('tab-setup').setAttribute('aria-selected', String(!live));
  lsSet(LS_TAB, name);
}

function boot() {
  const caps = detectCaps({
    hostname: location.hostname,
    protocol: location.protocol,
    isSecureContext: window.isSecureContext,
    hasBluetooth: !!navigator.bluetooth,
  });

  initLive(caps);
  initBle(caps);

  $('tab-live').onclick  = () => selectTab('live');
  $('tab-setup').onclick = () => selectTab('setup');
  selectTab(lsGet(LS_TAB) || defaultTab(caps));
}

// ---------- Live panel ----------
function initLive(caps) {
  if (caps.canStream) {
    $('live-notice').hidden = true;
    $('open-form').hidden = true;
    $('live-ui').hidden = false;
    startLiveStream();
  } else {
    $('live-ui').hidden = true;
    const n = $('live-notice');
    n.hidden = false;
    n.textContent = "Live view runs on the device's own page. Enter the device IP to open it.";
    const form = $('open-form');
    form.hidden = false;
    const savedIp = lsGet(LS_IP);
    if (savedIp) $('device-ip').value = savedIp;
    form.onsubmit = (e) => {
      e.preventDefault();
      const ip = extractIp($('device-ip').value);
      if (!ip) { n.textContent = 'Enter a valid IPv4 address (e.g. 192.168.1.42).'; return; }
      lsSet(LS_IP, ip);
      location.href = deviceUrl(ip);
    };
  }
}

function startLiveStream() {
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

// ---------- Setup (BLE) panel ----------
function initBle(caps) {
  if (!caps.canBle) {
    $('ble-ui').hidden = true;
    const n = $('ble-notice');
    n.hidden = false;
    n.textContent = 'Bluetooth setup needs the secure hosted (https) page. Open the dashboard from GitHub Pages to provision WiFi.';
    return;
  }
  $('ble-notice').hidden = true;
  $('ble-ui').hidden = false;
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
        const ip = extractIp(txt);
        if (ip) {
          lsSet(LS_IP, ip);
          const ipField = $('device-ip');
          if (ipField) ipField.value = ip; // reflect on the Live tab within this session
          const a = $('open-device');
          a.hidden = false; a.href = deviceUrl(ip);
          a.textContent = `Open device dashboard (${deviceUrl(ip)}) →`;
        }
      });
      $('wifi-form').hidden = false;
      setConn('BLE connected', true);
    } catch (err) { $('ble-status').textContent = 'BLE error: ' + err.message; }
  };

  $('wifi-form').onsubmit = async (e) => {
    e.preventDefault();
    try {
      await chars.ssid.writeValue(enc.encode($('ssid').value));
      await chars.pass.writeValue(enc.encode($('pass').value));
      await chars.apply.writeValue(Uint8Array.of(1));
      $('ble-status').textContent = 'Credentials sent; device joining WiFi…';
    } catch (err) {
      $('ble-status').textContent = 'Send failed: ' + err.message;
    }
  };
}

if (typeof document !== 'undefined') boot();

if (typeof module !== 'undefined') {
  module.exports = { detectCaps, defaultTab, extractIp, deviceUrl };
}
