'use strict';

const calIds = ['cal-connection', 'cal-message', 'start-session', 'calibration-form', 'calibration-fields',
  'live-pitch', 'live-volume', 'volume-max', 'volume-min', 'pitch-min', 'pitch-max', 'pitch-curve', 'volume-curve',
  'pitch-path', 'volume-path', 'volume-ceiling', 'preview-state', 'validation-note', 'preview-calibration',
  'save-calibration', 'cancel-calibration', 'back-home', 'capture-dialog', 'capture-context',
  'capture-title', 'capture-instruction', 'capture-countdown', 'capture-status', 'capture-progress',
  'capture-frequency-label', 'capture-frequency', 'capture-signal', 'capture-live-summary',
  'capture-detail', 'capture-cancel', 'capture-retry', 'capture-done'];
const endpointIds = ['pitch-far', 'pitch-near', 'volume-far', 'volume-near'];
for (const id of endpointIds) calIds.push('capture-' + id, 'endpoint-' + id);
const cal = Object.fromEntries(calIds.map(id => [id, document.getElementById(id)]));
const formatHz = new Intl.NumberFormat('en', { maximumFractionDigits: 0 });
const noteNames = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];
let token = '';
try { token = sessionStorage.getItem('thereminCalibrationToken') || ''; } catch (_) { /* Private browsing. */ }
if (!/^[0-9a-f]{32}$/.test(token)) {
  token = Array.from(crypto.getRandomValues(new Uint8Array(16)), byte => byte.toString(16).padStart(2, '0')).join('');
}
try { sessionStorage.setItem('thereminCalibrationToken', token); } catch (_) { /* Session still works in memory. */ }
let state = null;
let connected = false;
let commandBusy = false;
let initialized = false;
let dirty = false;
let leaving = false;
let localError = '';
let networkTail = Promise.resolve();
let captureView = null;
const captureNames = ['Pitch · far position', 'Pitch · close position', 'Volume · far position', 'Volume · close position'];
const captureInstructions = [
  'Place your hand far from the pitch antenna, at your lowest-note position.',
  'Place your hand close to the pitch antenna, at your highest-note position.',
  'Place your hand far from the volume antenna, at your loudest position.',
  'Place your hand close to the volume antenna, at your quietest position.'
];

for (let midi = 24; midi <= 107; midi++) {
  const hz = 440 * Math.pow(2, (midi - 69) / 12);
  for (const id of ['pitch-min', 'pitch-max']) {
    const option = document.createElement('option');
    option.value = String(hz);
    option.textContent = noteNames[midi % 12] + (Math.floor(midi / 12) - 1) + ' · ' + hz.toFixed(1) + ' Hz';
    cal[id].appendChild(option);
  }
}

function request(options = {}) {
  const run = async () => {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 3000);
    try {
      const response = await fetch('/api/calibration', { cache: 'no-store', ...options, signal: controller.signal });
      const data = await response.json();
      if (!response.ok || data.ok === false) {
        const error = new Error(data.message || 'The instrument is unavailable.');
        error.rejected = response.status >= 400 && response.status < 500;
        throw error;
      }
      return data;
    } finally { clearTimeout(timer); }
  };
  const next = networkTail.then(run, run);
  networkTail = next.catch(() => {});
  return next;
}

function send(action, values = {}) {
  return request({ method: 'POST', body: new URLSearchParams({ action, token, ...values }) });
}

function setNote(select, hz) {
  const closest = Array.from(select.options).find(option => Math.abs(Number(option.value) - hz) < 0.01);
  if (closest) select.value = closest.value;
  else {
    const option = document.createElement('option');
    option.value = String(hz); option.textContent = hz.toFixed(1) + ' Hz';
    select.appendChild(option); select.value = option.value;
  }
}

function settings() {
  const values = {
    pitch_min_hz: Number(cal['pitch-min'].value), pitch_max_hz: Number(cal['pitch-max'].value),
    volume_min_pct: Number(cal['volume-min'].value), volume_max_pct: Number(cal['volume-max'].value),
    pitch_curve: cal['pitch-curve'].value, volume_curve: cal['volume-curve'].value
  };
  if (cal['volume-min'].value.trim() === '' || cal['volume-max'].value.trim() === ''
      || !Number.isFinite(values.volume_min_pct) || !Number.isFinite(values.volume_max_pct)
      || values.volume_min_pct < 0 || values.volume_max_pct > 100
      || values.volume_min_pct > values.volume_max_pct)
    throw new Error('Choose volume limits from 0–100%, with the minimum no higher than the maximum.');
  if (!Number.isFinite(values.pitch_min_hz) || !Number.isFinite(values.pitch_max_hz)
      || values.pitch_min_hz < 20 || values.pitch_max_hz > 5000 || values.pitch_min_hz >= values.pitch_max_hz)
    throw new Error('Choose a highest note above the lowest note.');
  return values;
}

function drawCurves() {
  let values;
  try { values = settings(); } catch (_) { return; }
  const pitchRange = values.pitch_max_hz - values.pitch_min_hz;
  const path = (kind) => Array.from({ length: 41 }, (_, i) => {
    const x = i / 40;
    let y;
    if (kind === 'pitch') y = values.pitch_curve === 'linear' ? x
      : (values.pitch_min_hz * Math.pow(values.pitch_max_hz / values.pitch_min_hz, x) - values.pitch_min_hz) / pitchRange;
    else y = (values.volume_min_pct + (values.volume_max_pct - values.volume_min_pct)
      * Math.pow(1 - x, values.volume_curve === 'quadratic' ? 2 : 1)) / 100;
    return (i ? 'L' : 'M') + (20 + x * 285).toFixed(1) + ' ' + (115 - y * 100).toFixed(1);
  }).join(' ');
  cal['pitch-path'].setAttribute('d', path('pitch'));
  cal['volume-path'].setAttribute('d', path('volume'));
}

function captureFinished() {
  return captureView && ['success', 'error', 'cancelled'].includes(captureView.phase);
}

function showCapture(index, id = null) {
  const baseline = state && Number.isInteger(state.capture.id) ? state.capture.id : 0;
  captureView = {
    endpoint: index, baseline, id: id || ((baseline + 1) >>> 0) || 1,
    phase: 'starting', detail: '', remaining: 0, frequency: null,
    cancelRequested: false, stopInFlight: false, focusStart: true, focusResult: false
  };
  if (!cal['capture-dialog'].open) cal['capture-dialog'].showModal();
}

function renderCapture() {
  const view = captureView;
  if (!view || !cal['capture-dialog'].open) return;
  const oldPhase = view.phase;
  const input = view.endpoint < 2 ? 'pitch' : 'volume';
  const fresh = connected && state && state.telemetry.fresh;
  const sensor = fresh ? state.telemetry[input] : null;
  const liveHz = sensor && Number.isFinite(sensor.hz) ? formatHz.format(sensor.hz) + ' Hz' : '—';
  if (connected && state && !state.owned) {
    view.phase = 'error';
    view.detail = 'Your editing session has ended. Close this dialog and start a new calibration session.';
  } else if (connected && state && !captureFinished()) {
    const capture = state.capture;
    if (capture.id === view.id && capture.result_endpoint === view.endpoint) {
      if (capture.endpoint === view.endpoint) {
        view.phase = capture.phase;
        view.remaining = capture.remaining_ms;
        view.detail = view.cancelRequested ? 'Capture is still active. Cancellation has not been confirmed yet.' : '';
      } else if (capture.result === 11 && Number.isFinite(state.profile.endpoint_hz[view.endpoint])) {
        view.phase = 'success';
        view.frequency = state.profile.endpoint_hz[view.endpoint];
        view.detail = state.captured_mask === 15 && !state.valid ? state.validation_message
          : 'Added to your calibration draft. Use Preview to listen, then Save calibration to keep it.';
        if (view.cancelRequested) view.detail = 'Capture finished before cancellation. ' + view.detail;
      } else if (capture.result === 14) {
        view.phase = 'cancelled'; view.detail = capture.message;
      } else if (capture.result > 0) {
        view.phase = 'error'; view.detail = capture.message + ' The previous hand position was kept.';
      }
    } else if (view.phase === 'checking' && !commandBusy) {
      view.phase = 'error'; view.detail = 'This capture was not confirmed. Try again when the signal is ready.';
    }
  }
  if (view.phase !== oldPhase && captureFinished()) view.focusResult = true;
  const finished = captureFinished();
  const disconnected = !connected && !finished;
  const phase = disconnected ? 'disconnected' : view.phase;
  const titles = {
    starting: 'Preparing capture', countdown: 'Get in position', capturing: 'Hold your hand still',
    checking: 'Checking the capture', success: 'Position captured', error: 'Couldn’t capture',
    cancelled: 'Capture cancelled', disconnected: 'Connection lost'
  };
  cal['capture-dialog'].dataset.phase = phase;
  cal['capture-context'].textContent = captureNames[view.endpoint];
  cal['capture-title'].textContent = titles[phase] || 'Checking the capture';
  cal['capture-instruction'].textContent = disconnected
    ? 'The ESP32 may still be capturing. Reconnect to see its result.'
    : phase === 'countdown' || phase === 'starting' ? captureInstructions[view.endpoint]
    : phase === 'capturing' ? 'Keep this hand position steady while the instrument measures for one second.'
    : phase === 'success' ? 'Your measured hand position is ready.'
    : phase === 'cancelled' ? 'No new hand position was recorded.'
    : phase === 'checking' ? 'Waiting for the instrument to confirm what happened.'
    : 'Adjust your hand position or check the signal, then try again.';
  const seconds = Math.max(1, Math.ceil(view.remaining / 1000));
  cal['capture-countdown'].textContent = phase === 'countdown' ? String(seconds)
    : phase === 'capturing' ? 'Hold' : phase === 'success' ? '✓'
    : phase === 'error' ? '!' : phase === 'cancelled' || disconnected ? '—' : '…';
  const status = view.cancelRequested && !finished ? 'Cancelling capture…'
    : phase === 'countdown' ? 'Capture starts in ' + seconds + (seconds === 1 ? ' second' : ' seconds')
    : phase === 'capturing' ? 'Measuring your hand position…' : phase === 'success' ? 'Capture successful'
    : phase === 'error' ? 'Capture not accepted' : phase === 'cancelled' ? 'Previous hand position retained'
    : disconnected ? 'Waiting for the Wi-Fi connection' : 'Waiting for the instrument';
  if (cal['capture-status'].textContent !== status) cal['capture-status'].textContent = status;
  cal['capture-progress'].value = phase === 'countdown' ? Math.max(0, 75 * (1 - view.remaining / 3000))
    : phase === 'capturing' ? Math.max(75, Math.min(100, 100 - 25 * view.remaining / 1000))
    : finished ? 100 : 0;
  cal['capture-frequency-label'].textContent = view.phase === 'success' ? 'Captured average frequency'
    : 'Live ' + input + ' oscillator frequency';
  cal['capture-frequency'].textContent = view.phase === 'success' ? formatHz.format(view.frequency) + ' Hz' : liveHz;
  cal['capture-signal'].textContent = !connected ? 'Disconnected' : !fresh ? 'Readings stale'
    : sensor.valid ? 'Signal OK' : 'Invalid signal';
  cal['capture-signal'].dataset.valid = String(!!(sensor && sensor.valid));
  cal['capture-live-summary'].hidden = view.phase !== 'success';
  cal['capture-live-summary'].textContent = 'Live now: ' + liveHz;
  cal['capture-detail'].textContent = view.detail || (disconnected
    ? 'The result is not confirmed yet. This dialog will update when the connection returns.'
    : 'The result will stay here until you dismiss it.');
  cal['capture-cancel'].hidden = !!finished;
  cal['capture-cancel'].disabled = view.stopInFlight || (commandBusy && view.cancelRequested);
  cal['capture-cancel'].textContent = view.cancelRequested ? 'Retry cancel' : 'Cancel capture';
  cal['capture-retry'].hidden = !finished;
  cal['capture-retry'].disabled = commandBusy || !connected || !state || !state.owned
    || !sensor || !sensor.valid;
  cal['capture-retry'].textContent = view.phase === 'success' ? 'Capture again' : 'Try again';
  cal['capture-done'].hidden = !finished;
  cal['capture-done'].disabled = commandBusy;
  if (view.focusStart) {
    cal['capture-title'].focus({ preventScroll: true }); view.focusStart = false;
  }
  if (view.focusResult && !commandBusy) {
    cal['capture-done'].focus({ preventScroll: true }); view.focusResult = false;
  }
}

function closeCapture() {
  if (!captureFinished() || commandBusy) return;
  const trigger = cal['capture-' + endpointIds[captureView.endpoint]];
  captureView = null;
  cal['capture-dialog'].close();
  render();
  (trigger.disabled ? cal['cal-message'] : trigger).focus({ preventScroll: true });
}

async function startCapture(id) {
  if (commandBusy || (captureView && !captureFinished())) return;
  const index = endpointIds.indexOf(id);
  if (index < 0) return;
  showCapture(index);
  commandBusy = true; localError = ''; render();
  let posted = false;
  try {
    await send('settings', settings()); dirty = false;
    if (captureView.cancelRequested) {
      captureView.phase = 'cancelled'; captureView.focusResult = true;
      captureView.detail = 'Capture cancelled before measurement started. Your previous captures are unchanged.';
    } else {
      posted = true;
      const reply = await send('capture', { endpoint: id.replace('-', '_') });
      if (Number.isInteger(reply.capture_id) && reply.capture_id > 0) captureView.id = reply.capture_id;
      await refresh();
    }
  } catch (error) {
    if (!error.rejected) connected = false;
    if (posted && !error.rejected) {
      captureView.phase = 'checking';
      captureView.detail = 'The request may have reached the ESP32. Reconnecting to confirm its result.';
    } else {
      captureView.phase = 'error'; captureView.detail = error.message;
      captureView.focusResult = true;
    }
  } finally { commandBusy = false; render(); }
  if (captureView && captureView.cancelRequested && !captureFinished()) await cancelCapture();
}

async function cancelCapture() {
  if (!captureView) return;
  if (captureFinished()) { closeCapture(); return; }
  captureView.cancelRequested = true;
  renderCapture();
  if (commandBusy) return;
  captureView.stopInFlight = true;
  commandBusy = true; render();
  try {
    await send('stop_capture', { capture_id: captureView.id });
    await refresh();
  } catch (_) {
    connected = false;
    captureView.detail = 'Cancellation is not confirmed. Reconnect and retry; the ESP32 may still finish this capture.';
  } finally { captureView.stopInFlight = false; commandBusy = false; render(); }
}

function render() {
  if (!captureView && connected && state && state.owned && state.capture.endpoint >= 0)
    showCapture(state.capture.endpoint, state.capture.id);
  const owned = connected && state && state.owned;
  const capturing = state && state.capture.endpoint >= 0;
  const busy = commandBusy || !!(state && state.saving) || capturing || cal['capture-dialog'].open;
  cal['calibration-fields'].disabled = !owned || busy;
  cal['start-session'].hidden = !!owned || commandBusy;
  cal['start-session'].disabled = commandBusy;
  let settingsError = '';
  if (initialized) { try { settings(); } catch (error) { settingsError = error.message; } }
  for (const [index, id] of endpointIds.entries()) {
    const input = index < 2 ? 'pitch' : 'volume';
    const signal = state && state.telemetry.fresh && state.telemetry[input].valid;
    cal['capture-' + id].disabled = !owned || busy || !signal || !!settingsError;
    const captured = state && (state.captured_mask & (1 << index));
    cal['endpoint-' + id].textContent = captured ? formatHz.format(state.profile.endpoint_hz[index]) + ' Hz captured' : 'Not captured';
    cal['endpoint-' + id].dataset.captured = String(!!captured);
  }
  cal['preview-calibration'].disabled = !owned || busy || !state.valid || !state.can_preview || !!settingsError;
  cal['save-calibration'].disabled = !owned || busy || !state.valid || !!settingsError;
  cal['cancel-calibration'].disabled = commandBusy || !!(state && state.saving) || cal['capture-dialog'].open;
  cal['save-calibration'].textContent = state && state.saving ? 'Saving…' : 'Save calibration';
  cal['preview-calibration'].textContent = state && state.preview && !dirty ? 'Stop preview' : 'Preview';
  cal['preview-state'].textContent = !connected ? 'Instrument connection unavailable.' : state && state.saving
    ? 'Saving — briefly muting audio.' : state && state.preview
      ? dirty ? 'Preview active. New edits are not playing yet.' : 'Preview active — not yet saved.'
      : 'Saved settings are playing.';
  cal['validation-note'].textContent = settingsError || (state && !state.can_preview ? 'Preview is available in theremin mode. ' : '')
    + (state ? state.valid ? dirty ? 'Press Preview to hear your edits.' : 'All hand positions are ready.' : state.validation_message : 'Connecting…');
  let message = localError || (state ? state.message : 'Connecting to the instrument…');
  let kind = localError ? 'error' : 'info';
  if (!localError && state && ![0, 10, 11, 14].includes(state.notice)) kind = 'error';
  if (!localError && state && !state.owned && state.session_active) {
    message = 'Another browser is calibrating this instrument. Wait until it finishes, then start again.'; kind = 'busy';
  }
  if (!localError && capturing) {
    const names = ['Pitch: far position', 'Pitch: close position', 'Volume: far position', 'Volume: close position'];
    message = names[state.capture.endpoint] + (state.capture.phase === 'countdown'
      ? ' — get in position. Capture starts in ' + Math.ceil(state.capture.remaining_ms / 1000) + '…'
      : ' — capturing. Hold your hand still…');
    kind = 'busy';
  }
  if (!localError && state && state.saving) { message = 'Saving your calibration. Audio will resume shortly.'; kind = 'busy'; }
  cal['cal-message'].textContent = message;
  cal['cal-message'].dataset.kind = kind;
  for (const name of ['pitch', 'volume']) {
    const live = connected && state && state.telemetry.fresh;
    const sensor = live ? state.telemetry[name] : null;
    cal['live-' + name].textContent = (name === 'pitch' ? 'Pitch: ' : 'Volume: ')
      + (sensor && Number.isFinite(sensor.hz) ? formatHz.format(sensor.hz) + ' Hz' : '—')
      + (sensor && !sensor.valid ? ' · invalid signal' : '');
    cal['live-' + name].dataset.valid = String(!!(sensor && sensor.valid));
  }
  cal['cal-connection'].textContent = connected ? 'Connected' : 'Connection lost · Retrying…';
  cal['cal-connection'].dataset.state = connected ? 'live' : 'offline';
  renderCapture();
}

async function refresh() {
  const next = await request({ headers: { 'X-Calibration-Token': token } });
  if (!next.profile || !next.capture || !next.telemetry) throw new Error('The instrument returned an incomplete response.');
  state = next; connected = true;
  if (!initialized && state.owned) {
    setNote(cal['pitch-min'], state.profile.pitch_min_hz); setNote(cal['pitch-max'], state.profile.pitch_max_hz);
    cal['volume-min'].value = String(state.profile.volume_min_pct);
    cal['volume-max'].value = String(state.profile.volume_max_pct);
    cal['pitch-curve'].value = state.profile.pitch_curve; cal['volume-curve'].value = state.profile.volume_curve;
    cal['volume-ceiling'].textContent = '100% uses the configured maximum output (digital gain '
      + state.profile.ceiling_gain.toFixed(3) + ').';
    initialized = true; dirty = false; drawCurves();
  }
  render();
}

async function action(name, extra = {}) {
  if (commandBusy) return;
  commandBusy = true; localError = ''; render();
  try {
    if (name === 'preview' || name === 'save') {
      await send('settings', settings()); dirty = false;
    }
    await send(name, extra);
    if (name === 'begin') initialized = false;
    await refresh();
  } catch (error) { localError = error.message || 'Could not reach the instrument.'; }
  finally {
    commandBusy = false; render();
    if (captureView && captureView.cancelRequested && !captureFinished()) await cancelCapture();
  }
}

async function cancelAndLeave(event) {
  event.preventDefault();
  if (commandBusy || (state && state.saving)) return;
  commandBusy = true; render();
  try {
    if (state && state.owned) await send('cancel');
    leaving = true;
    try { sessionStorage.removeItem('thereminCalibrationToken'); } catch (_) {}
    location.assign('/');
  } catch (_) {
    localError = 'Could not cancel yet. Reconnect and retry. Unsaved preview stops after 45 seconds without a connection.';
    commandBusy = false; render();
  }
}

cal['calibration-form'].addEventListener('submit', event => event.preventDefault());
for (const id of ['volume-max', 'volume-min', 'pitch-min', 'pitch-max', 'pitch-curve', 'volume-curve']) {
  cal[id].addEventListener('input', () => { dirty = true; drawCurves(); render(); });
  cal[id].addEventListener('change', () => { dirty = true; drawCurves(); render(); });
}
for (const id of endpointIds) cal['capture-' + id].addEventListener('click', () => startCapture(id));
cal['capture-cancel'].addEventListener('click', cancelCapture);
cal['capture-retry'].addEventListener('click', () => {
  if (captureView) return startCapture(endpointIds[captureView.endpoint]);
});
cal['capture-done'].addEventListener('click', closeCapture);
cal['capture-dialog'].addEventListener('cancel', event => {
  event.preventDefault();
  return captureFinished() ? closeCapture() : cancelCapture();
});
cal['preview-calibration'].addEventListener('click', () => action(state && state.preview && !dirty ? 'stop_preview' : 'preview'));
cal['save-calibration'].addEventListener('click', () => action('save'));
cal['start-session'].addEventListener('click', () => action('begin'));
cal['cancel-calibration'].addEventListener('click', cancelAndLeave);
cal['back-home'].addEventListener('click', cancelAndLeave);
window.addEventListener('pagehide', () => {
  if (!leaving && state && state.owned && !state.saving) {
    try { navigator.sendBeacon('/api/calibration', new URLSearchParams({ action: 'cancel', token })); }
    catch (_) { /* The device also expires disconnected sessions after 45 seconds. */ }
  }
});

async function pollCalibration() {
  if (leaving) return;
  if (!commandBusy) {
    try { await refresh(); }
    catch (_) { connected = false; render(); }
  }
  setTimeout(pollCalibration, 250);
}

render();
action('begin').finally(pollCalibration);
