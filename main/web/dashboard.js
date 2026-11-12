'use strict';

const elements = Object.fromEntries([
  'connection', 'pitch-hz', 'volume-hz', 'pitch-state', 'volume-state',
  'audio-hz', 'audio-gain', 'mode', 'update-note'
].map(id => [id, document.getElementById(id)]));
const frequencyFormat = new Intl.NumberFormat('en', { maximumFractionDigits: 0 });
const modes = { theremin: 'Theremin mode', measure_only: 'Measure-only mode', test_tone: 'Test-tone mode' };

function setState(element, label, state) {
  element.textContent = label;
  element.dataset.state = state;
}

function renderSensor(name, sensor, available, fresh) {
  elements[name + '-hz'].textContent = available && fresh && Number.isFinite(sensor.hz)
    ? frequencyFormat.format(sensor.hz) : '—';
  const label = !available ? 'Waiting' : !fresh ? 'Stale' : sensor.valid ? 'Signal OK' : 'Invalid signal';
  const state = !available ? 'waiting' : !fresh ? 'stale' : sensor.valid ? 'good' : 'bad';
  setState(elements[name + '-state'], label, state);
}

function renderStatus(data) {
  if (!data || !data.pitch || !data.volume || !data.audio
      || typeof data.fresh !== 'boolean' || typeof data.sample_available !== 'boolean') {
    throw new Error('Invalid instrument response');
  }
  document.body.dataset.offline = String(!data.fresh);
  setState(elements.connection,
    !data.sample_available ? 'Waiting for readings' : data.fresh ? 'Connected · Live' : 'Readings paused',
    data.fresh ? 'live' : 'waiting');
  renderSensor('pitch', data.pitch, data.sample_available, data.fresh);
  renderSensor('volume', data.volume, data.sample_available, data.fresh);
  elements['audio-hz'].textContent = !data.fresh ? '—'
    : data.audio.enabled && Number.isFinite(data.audio.target_hz)
      ? data.audio.target_hz.toFixed(1) + ' Hz' : 'Muted';
  elements['audio-gain'].textContent = data.fresh && Number.isFinite(data.audio.gain)
    ? data.audio.gain.toFixed(3) : '—';
  elements.mode.textContent = modes[data.mode] || 'Instrument mode unavailable';
  elements['update-note'].textContent = !data.sample_available ? 'Waiting for the ESP32'
    : !data.fresh ? 'Waiting for fresh measurements' : 'Updates about 5 times per second';
}

function renderDisconnected() {
  document.body.dataset.offline = 'true';
  setState(elements.connection, 'Connection lost', 'offline');
  for (const name of ['pitch', 'volume']) {
    elements[name + '-hz'].textContent = '—';
    setState(elements[name + '-state'], 'Unavailable', 'stale');
  }
  elements['audio-hz'].textContent = '—';
  elements['audio-gain'].textContent = '—';
  elements.mode.textContent = 'Instrument unavailable';
  elements['update-note'].textContent = 'Check your Wi-Fi connection · Retrying…';
}

async function poll() {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 1500);
  try {
    const response = await fetch('/api/status', { cache: 'no-store', signal: controller.signal });
    if (!response.ok) throw new Error('Instrument unavailable');
    renderStatus(await response.json());
  } catch (_) {
    renderDisconnected();
  } finally {
    clearTimeout(timeout);
    setTimeout(poll, 200); // Schedule after completion; requests never overlap.
  }
}

poll();
