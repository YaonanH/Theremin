import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';

const html = await readFile(new URL('../main/web/calibration.html', import.meta.url), 'utf8');
const source = await readFile(new URL('../main/web/calibration.js', import.meta.url), 'utf8');
const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]);
assert.equal(ids.length, new Set(ids).size);
assert.equal((html.match(/class="cal-section /g) || []).length, 3);
assert.ok(!/advanced/i.test(html));
assert.match(html, /<dialog[^>]+aria-labelledby="capture-title"/);
let focused = '';

function element() {
  return {
    value: '', textContent: '', dataset: {}, options: [], listeners: {}, hidden: false, disabled: false, open: false,
    addEventListener(name, handler) { this.listeners[name] = handler; },
    appendChild(option) { this.options.push(option); },
    setAttribute(name, value) { this[name] = value; },
    showModal() { this.open = true; }, close() { this.open = false; },
    focus() { focused = this.id; }
  };
}
const elements = Object.fromEntries(ids.map(id => [id, Object.assign(element(), { id })]));
const timers = new Map();
let nextTimer = 0;
let activeRequests = 0;
let maxActiveRequests = 0;
let fail = false;
let failCancel = false;
let failAfterCapture = false;
let pauseSettings = false;
let releaseSettings;
let token = '';
let current = {
  session_active: true, owned: true, preview: false, saving: false, saved: false, can_preview: true,
  captured_mask: 0, valid: false, validation_message: 'Capture all four hand positions.',
  notice: 0, message: 'Ready.', capture: { id: 0, endpoint: -1, phase: 'idle', remaining_ms: 0,
    result_endpoint: -1, result: 0, message: 'Ready.' },
  profile: { endpoint_hz: [100000, 300000, 100000, 300000], pitch_min_hz: 130.8128,
    pitch_max_hz: 2093.0048, volume_min_pct: 0, volume_max_pct: 100,
    pitch_curve: 'musical', volume_curve: 'quadratic', ceiling_gain: 0.1 },
  telemetry: { fresh: true, pitch: { hz: 130000, valid: true }, volume: { hz: 240000, valid: true } }
};
const posts = [];
const beacons = [];
const windowEvents = {};
let destination = '';
const context = vm.createContext({
  document: {
    getElementById(id) { assert.ok(elements[id], `Missing element ${id}`); return elements[id]; },
    createElement() { return element(); }
  },
  window: { addEventListener(name, handler) { windowEvents[name] = handler; } },
  navigator: { sendBeacon(url, body) { beacons.push([url, body]); return true; } },
  location: { assign(url) { destination = url; } },
  sessionStorage: { getItem() { return token; }, setItem(_key, value) { token = value; }, removeItem() { token = ''; } },
  crypto: { getRandomValues(array) { array.fill(10); return array; } },
  Intl, Number, String, Object, Array, Uint8Array, Math, Promise, Error, URLSearchParams, AbortController,
  setTimeout(callback, delay) { timers.set(++nextTimer, { callback, delay }); return nextTimer; },
  clearTimeout(id) { timers.delete(id); },
  async fetch(_url, options) {
    activeRequests++; maxActiveRequests = Math.max(maxActiveRequests, activeRequests);
    try {
      await Promise.resolve();
      if (fail) throw new Error('Disconnected');
      if (options.method === 'POST') {
        const values = Object.fromEntries(options.body);
        posts.push(values);
        if (failCancel && values.action === 'cancel') throw new Error('Disconnected');
        if (values.action === 'settings') {
          current.preview = false;
          assert.equal(values.volume_max_pct, elements['volume-max'].value);
          if (pauseSettings) {
            pauseSettings = false;
            await new Promise(resolve => { releaseSettings = resolve; });
          }
        }
        if (values.action === 'capture') {
          const endpoint = ['pitch_far', 'pitch_near', 'volume_far', 'volume_near'].indexOf(values.endpoint);
          current.capture = { id: current.capture.id + 1, endpoint, phase: 'countdown', remaining_ms: 2900,
            result_endpoint: endpoint, result: 0, message: 'Ready.' };
          if (failAfterCapture) { failAfterCapture = false; throw new Error('Reply lost'); }
          return { ok: true, status: 200, json: async () => ({ ok: true, capture_id: current.capture.id }) };
        }
        if (values.action === 'stop_capture' && Number(values.capture_id) === current.capture.id && current.capture.endpoint >= 0) {
          current.capture = { ...current.capture, endpoint: -1, phase: 'idle', remaining_ms: 0, result: 14,
            message: 'Capture cancelled. The previous hand position was retained.' };
          current.notice = 14;
        }
        if (values.action === 'preview') current.preview = true;
        if (values.action === 'stop_preview') current.preview = false;
        if (values.action === 'save') current.saving = true;
        return { ok: true, json: async () => ({ ok: true }) };
      }
      return { ok: true, json: async () => structuredClone(current) };
    } finally { activeRequests--; }
  }
});

await vm.runInContext(source, context);
assert.equal(token.length, 32);
assert.equal(elements['calibration-fields'].disabled, false);
assert.equal(elements['volume-max'].value, '100');
assert.equal(elements['save-calibration'].disabled, true);
assert.equal(elements['capture-volume-far'].disabled, false);
assert.match(elements['pitch-path'].d, /^M20\.0 115\.0/);
assert.match(elements['volume-path'].d, /^M20\.0 15\.0/);
current.profile.volume_max_pct = 37.5;
vm.runInContext('initialized = false', context);
await vm.runInContext('refresh()', context);
assert.equal(elements['volume-max'].value, '37.5');
current.profile.volume_max_pct = 100;
vm.runInContext('initialized = false', context);
await vm.runInContext('refresh()', context);

// Local edits survive polling; polling must not overwrite the user's chosen limits.
elements['volume-max'].value = '35';
elements['volume-max'].listeners.input();
await vm.runInContext('refresh()', context);
assert.equal(elements['volume-max'].value, '35');
await elements['capture-volume-far'].listeners.click();
assert.equal(posts.at(-2).action, 'settings');
assert.equal(posts.at(-2).volume_max_pct, '35');
assert.equal(posts.at(-1).endpoint, 'volume_far');
assert.equal(elements['calibration-fields'].disabled, true);
assert.match(elements['cal-message'].textContent, /3/);
assert.equal(elements['capture-dialog'].open, true);
assert.equal(elements['capture-context'].textContent, 'Volume · far position');
assert.equal(elements['capture-countdown'].textContent, '3');
assert.equal(elements['capture-frequency'].textContent, '240,000 Hz');
assert.equal(elements['capture-done'].hidden, true);
assert.equal(focused, 'capture-title');
current.capture.phase = 'capturing'; current.capture.remaining_ms = 750;
current.telemetry.volume.hz = 241300;
await vm.runInContext('refresh()', context);
assert.equal(elements['capture-countdown'].textContent, 'Hold');
assert.equal(elements['capture-frequency'].textContent, '241,300 Hz');
assert.ok(elements['capture-progress'].value >= 75);

current.capture = { ...current.capture, endpoint: -1, phase: 'idle', remaining_ms: 0, result: 11, message: 'Hand position captured.' };
current.profile.endpoint_hz[2] = 241234;
current.captured_mask = 15; current.valid = true;
await vm.runInContext('refresh()', context);
assert.equal(elements['capture-title'].textContent, 'Position captured');
assert.equal(elements['capture-frequency-label'].textContent, 'Captured average frequency');
assert.equal(elements['capture-frequency'].textContent, '241,234 Hz');
assert.equal(elements['capture-live-summary'].textContent, 'Live now: 241,300 Hz');
assert.equal(elements['calibration-fields'].disabled, true);
assert.equal(focused, 'capture-done');
elements['capture-done'].listeners.click();
assert.equal(elements['capture-dialog'].open, false);
assert.equal(focused, 'capture-volume-far');
assert.equal(elements['preview-calibration'].disabled, false);
assert.equal(elements['save-calibration'].disabled, false);
await elements['preview-calibration'].listeners.click();
assert.equal(posts.at(-1).action, 'preview');
assert.equal(elements['preview-calibration'].textContent, 'Stop preview');
await elements['preview-calibration'].listeners.click();
assert.equal(posts.at(-1).action, 'stop_preview');

// Re-capturing an already captured endpoint must not report old data as a new success.
await elements['capture-volume-far'].listeners.click();
assert.equal(elements['capture-title'].textContent, 'Get in position');
assert.equal(elements['capture-done'].hidden, true);
current.capture = { ...current.capture, endpoint: -1, phase: 'idle', remaining_ms: 0, result: 7,
  message: 'Capture failed: the reading changed too much.' };
await vm.runInContext('refresh()', context);
assert.equal(elements['capture-title'].textContent, 'Couldn’t capture');
assert.match(elements['capture-detail'].textContent, /previous hand position was kept/);
assert.equal(current.profile.endpoint_hz[2], 241234);
await elements['capture-retry'].listeners.click();
assert.equal(elements['capture-title'].textContent, 'Get in position');
assert.equal(current.capture.id, 3);

// Escape cancels only this attempt, keeping the rest of the draft and its settings.
let preventedEscape = false;
await elements['capture-dialog'].listeners.cancel({ preventDefault() { preventedEscape = true; } });
assert.equal(preventedEscape, true);
assert.equal(posts.at(-1).action, 'stop_capture');
assert.equal(posts.at(-1).capture_id, '3');
assert.equal(elements['capture-title'].textContent, 'Capture cancelled');
assert.equal(current.captured_mask, 15);
assert.equal(elements['volume-max'].value, '35');
assert.equal(current.profile.endpoint_hz[2], 241234);
elements['capture-done'].listeners.click();

// A lost POST reply is reconciled with the specific device-side attempt.
failAfterCapture = true;
await elements['capture-volume-far'].listeners.click();
assert.equal(elements['capture-title'].textContent, 'Connection lost');
assert.equal(elements['capture-frequency'].textContent, '—');
assert.equal(elements['capture-done'].hidden, true);
await vm.runInContext('pollCalibration()', context);
assert.equal(elements['capture-title'].textContent, 'Get in position');
current.capture.phase = 'capturing'; current.capture.remaining_ms = 500;
await vm.runInContext('refresh()', context);
fail = true;
await vm.runInContext('pollCalibration()', context);
assert.equal(elements['capture-title'].textContent, 'Connection lost');
assert.equal(elements['capture-frequency'].textContent, '—');
assert.equal(elements['capture-done'].hidden, true);
fail = false;
current.capture = { ...current.capture, endpoint: -1, phase: 'idle', remaining_ms: 0, result: 11, message: 'Hand position captured.' };
current.profile.endpoint_hz[2] = 242000;
await vm.runInContext('pollCalibration()', context);
assert.equal(elements['capture-title'].textContent, 'Position captured');
assert.equal(elements['capture-frequency'].textContent, '242,000 Hz');
elements['capture-done'].listeners.click();

// Cancelling while settings are being prepared must not start a capture later.
pauseSettings = true;
const captureCount = posts.filter(post => post.action === 'capture').length;
const preparation = elements['capture-volume-far'].listeners.click();
for (let i = 0; i < 12 && !releaseSettings; i++) await Promise.resolve();
assert.ok(releaseSettings);
assert.equal(elements['capture-cancel'].disabled, false);
await elements['capture-cancel'].listeners.click();
releaseSettings();
await preparation;
assert.equal(posts.filter(post => post.action === 'capture').length, captureCount);
assert.equal(elements['capture-title'].textContent, 'Capture cancelled');
elements['capture-done'].listeners.click();

// If measurement completed before Cancel arrived, report the actual completed result.
await elements['capture-volume-far'].listeners.click();
current.capture = { ...current.capture, endpoint: -1, phase: 'idle', remaining_ms: 0, result: 11, message: 'Hand position captured.' };
current.profile.endpoint_hz[2] = 243000;
await elements['capture-cancel'].listeners.click();
assert.equal(elements['capture-title'].textContent, 'Position captured');
assert.match(elements['capture-detail'].textContent, /finished before cancellation/);
assert.equal(elements['capture-frequency'].textContent, '243,000 Hz');
elements['capture-done'].listeners.click();

// Volume input failure does not disable pitch capture.
current.telemetry.volume.valid = false;
await vm.runInContext('refresh()', context);
assert.equal(elements['capture-volume-far'].disabled, true);
assert.equal(elements['capture-pitch-far'].disabled, false);

// Invalid volume limits must not become a network command.
elements['volume-min'].value = '90';
elements['volume-min'].listeners.input();
assert.equal(elements['save-calibration'].disabled, true);
const beforeInvalid = posts.length;
await vm.runInContext("action('save')", context);
assert.equal(posts.length, beforeInvalid);
assert.match(elements['cal-message'].textContent, /minimum/);
elements['volume-min'].value = '0';
await vm.runInContext("action('save')", context);
assert.equal(posts.at(-1).action, 'save');
assert.equal(elements['save-calibration'].textContent, 'Saving…');
assert.equal(elements['cancel-calibration'].disabled, true);
current.saving = false; current.saved = true; current.notice = 10; current.message = 'Calibration saved.';
await vm.runInContext('refresh()', context);
assert.match(elements['cal-message'].textContent, /saved/);
current.notice = 9; current.message = 'Could not save. The previous saved settings are still active. Retry.';
await vm.runInContext('refresh()', context);
assert.equal(elements['cal-message'].dataset.kind, 'error');
assert.match(elements['cal-message'].textContent, /previous saved/);
assert.equal(elements['volume-max'].value, '35');
current.notice = 0; current.message = 'Ready.';

// Loss of connection disables mutation, then recovers using the existing draft.
fail = true;
await vm.runInContext('pollCalibration()', context);
assert.equal(elements['calibration-fields'].disabled, true);
assert.equal(elements['live-pitch'].textContent, 'Pitch: —');
fail = false;
await vm.runInContext('pollCalibration()', context);
assert.equal(elements['calibration-fields'].disabled, false);
assert.equal(elements['volume-max'].value, '35');

// A different browser owns the edit lock: retain read-only telemetry.
current.owned = false;
await vm.runInContext('refresh()', context);
assert.equal(elements['calibration-fields'].disabled, true);
assert.equal(elements['start-session'].hidden, false);
assert.match(elements['cal-message'].textContent, /Another browser/);
current.owned = true;
await vm.runInContext('refresh()', context);
windowEvents.pagehide();
assert.equal(beacons.at(-1)[1].get('action'), 'cancel');

// Do not claim cancellation or navigate away if the instrument did not acknowledge it.
failCancel = true;
await elements['cancel-calibration'].listeners.click({ preventDefault() {} });
assert.equal(destination, '');
assert.match(elements['cal-message'].textContent, /Could not cancel/);
failCancel = false;
await elements['cancel-calibration'].listeners.click({ preventDefault() {} });
assert.equal(destination, '/');
assert.equal(posts.at(-1).action, 'cancel');
assert.equal(maxActiveRequests, 1);
console.log('calibration page: all checks passed');
