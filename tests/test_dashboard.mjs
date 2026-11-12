// Run with Node.js: node tests/test_dashboard.mjs
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';

const source = await readFile(new URL('../main/web/dashboard.js', import.meta.url), 'utf8');
const html = await readFile(new URL('../main/web/index.html', import.meta.url), 'utf8');
const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(match => match[1]);
assert.equal(new Set(ids).size, ids.length, 'HTML element IDs must be unique');
const elements = Object.fromEntries(ids.map(id => [id, { textContent: '', dataset: {} }]));
const timers = new Map();
let nextTimer = 0;
let fetchCount = 0;
let resolveFetch;
const context = vm.createContext({
  document: {
    body: { dataset: {} },
    getElementById: id => {
      assert.ok(elements[id], `Missing HTML element: ${id}`);
      return elements[id];
    }
  },
  Intl, Number, Object, String, Error, AbortController,
  setTimeout: (callback, delay) => { timers.set(++nextTimer, { callback, delay }); return nextTimer; },
  clearTimeout: id => timers.delete(id),
  fetch: () => { fetchCount++; return new Promise(resolve => { resolveFetch = resolve; }); }
});

const initialPoll = vm.runInContext(source, context);
assert.equal(fetchCount, 1);
assert.equal([...timers.values()].filter(timer => timer.delay === 200).length, 0,
  'A second request must not be scheduled while the first is pending');
const live = {
  mode: 'theremin', sample_available: true, fresh: true, sample_age_ms: 5,
  pitch: { hz: 216873, valid: true }, volume: { hz: 200000, valid: true },
  audio: { target_hz: 523.25, gain: 0.05, enabled: true }
};
resolveFetch({ ok: true, json: async () => live });
await initialPoll;
assert.equal(elements.connection.textContent, 'Connected · Live');
assert.equal(elements['pitch-hz'].textContent, '216,873');
assert.equal(elements['volume-hz'].textContent, '200,000');
assert.equal(elements['audio-hz'].textContent, '523.3 Hz');
assert.equal(elements['audio-gain'].textContent, '0.050');
assert.equal([...timers.values()].filter(timer => timer.delay === 200).length, 1);
assert.equal([...timers.values()].filter(timer => timer.delay === 1500).length, 0);

function render(fixture) {
  context.fixture = fixture;
  vm.runInContext('renderStatus(fixture)', context);
}
render({ ...live, sample_available: false, fresh: false });
assert.equal(elements.connection.textContent, 'Waiting for readings');
assert.equal(elements['pitch-hz'].textContent, '—');
assert.equal(elements['pitch-state'].textContent, 'Waiting');

render({ ...live, fresh: false });
assert.equal(elements.connection.textContent, 'Readings paused');
assert.equal(elements['pitch-hz'].textContent, '—');
assert.equal(elements['pitch-state'].textContent, 'Stale');
assert.equal(elements['audio-hz'].textContent, '—');

render({ ...live, pitch: { hz: 0, valid: false }, audio: { target_hz: null, gain: 0, enabled: false } });
assert.equal(elements['pitch-state'].textContent, 'Invalid signal');
assert.equal(elements['volume-state'].textContent, 'Signal OK');
assert.equal(elements['audio-hz'].textContent, 'Muted');

context.fetch = async () => ({ ok: false });
await vm.runInContext('poll()', context);
assert.equal(elements.connection.textContent, 'Connection lost');
assert.equal(elements['volume-hz'].textContent, '—');
assert.equal(elements['audio-gain'].textContent, '—');
assert.equal(elements.mode.textContent, 'Instrument unavailable');

// Recover automatically when the instrument becomes reachable again.
context.fetch = async () => ({ ok: true, json: async () => live });
await vm.runInContext('poll()', context);
assert.equal(elements.connection.textContent, 'Connected · Live');
assert.equal(elements['pitch-state'].textContent, 'Signal OK');

// Malformed replies follow the same disconnected path, without stale values.
context.fetch = async () => ({ ok: true, json: async () => ({}) });
await vm.runInContext('poll()', context);
assert.equal(elements.connection.textContent, 'Connection lost');
console.log('dashboard: all checks passed');
