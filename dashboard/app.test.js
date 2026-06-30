const { test } = require('node:test');
const assert = require('node:assert');
const { detectCaps, defaultTab, extractIp, deviceUrl } = require('./app.js');

test('detectCaps: hosted github.io https', () => {
  const c = detectCaps({ hostname: 'user.github.io', protocol: 'https:', isSecureContext: true, hasBluetooth: true });
  assert.deepStrictEqual(c, { onDevice: false, canStream: false, canBle: true });
});

test('detectCaps: device http without bluetooth', () => {
  const c = detectCaps({ hostname: '192.168.1.42', protocol: 'http:', isSecureContext: false, hasBluetooth: false });
  assert.deepStrictEqual(c, { onDevice: true, canStream: true, canBle: false });
});

test('detectCaps: bluetooth present but insecure context => canBle false', () => {
  const c = detectCaps({ hostname: '192.168.1.42', protocol: 'http:', isSecureContext: false, hasBluetooth: true });
  assert.strictEqual(c.canBle, false);
});

test('defaultTab: on-device => live', () => {
  assert.strictEqual(defaultTab({ onDevice: true }), 'live');
});

test('defaultTab: hosted => setup', () => {
  assert.strictEqual(defaultTab({ onDevice: false }), 'setup');
});

test('extractIp: real address', () => {
  assert.strictEqual(extractIp('joined wifi 192.168.1.42'), '192.168.1.42');
});

test('extractIp: 0.0.0.0 is rejected', () => {
  assert.strictEqual(extractIp('ip 0.0.0.0'), null);
});

test('extractIp: no address', () => {
  assert.strictEqual(extractIp('connecting...'), null);
});

test('deviceUrl', () => {
  assert.strictEqual(deviceUrl('192.168.1.42'), 'http://192.168.1.42/');
});
