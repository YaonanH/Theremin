# Firmware and source files

## Source-file summary

All paths below are relative to the project root.

| File | Responsibility |
| --- | --- |
| `main/theremin_config.h` | GPIOs, sensor limits, fallback endpoints, audio limits, Wi-Fi configuration, and operating mode |
| `main/main.c` | PCNT initialization, timer snapshots, measurement validation/filtering, audio task, and quiet-save coordination |
| `main/calibration_model.c` and `.h` | Portable calibration state machine, capture statistics, validation, response curves, and mapping |
| `main/calibration.c` and `.h` | Calibration service, command/reply queues, state snapshots, and versioned NVS storage |
| `main/calibration_protocol.c` and `.h` | Strict form parsing and bounded calibration JSON formatting |
| `main/calibration_web.c` and `.h` | Calibration asset handlers and GET/POST routes |
| `main/web_monitor.c` and `.h` | Wi-Fi access point, dashboard routes, and latest-status queue |
| `main/web_status.c` and `.h` | Portable telemetry JSON formatting, including signal freshness |
| `main/web/index.html`, `dashboard.css`, `dashboard.js` | Read-only live dashboard |
| `main/web/calibration.html`, `calibration.css`, `calibration.js` | Calibration form, capture dialog, preview, and save interaction |
| `main/CMakeLists.txt` | Component sources, ESP-IDF dependencies, and embedded web assets |
| `sdkconfig.defaults` | Default ESP-IDF settings for a newly configured build |
| `tests/` | Portable C checks and JavaScript tests of the actual browser scripts |

## Frequency measurement

Two PCNT units count rising edges continuously. Each has a 100 ns glitch filter,
hardware limits at ±30000, watch points, and driver accumulation enabled. There
is no GPIO interrupt for every edge.

An `esp_timer` callback running in task context snapshots the units every 10000 µs.
Each counter read uses the midpoint of its before/after software timestamps; this
reduces timestamp bias but is not a simultaneous hardware latch. Frequency is
calculated from count differences and actual elapsed time:

```text
frequency_hz = pulse_difference × 1,000,000 / elapsed_us
```

The callback overwrites a one-slot measurement queue. Ordinary windows do not
stop or clear the counters. A rare rebase before one billion accumulated pulses
briefly pauses each unit to avoid signed accumulator overflow.

## Validation and filtering

The control loop checks nonnegative pulse counts, finite frequency, the configured
sensor band plus margin, and elapsed time between half and twice the nominal
window. With the current settings, the accepted band is 98–302 kHz around the
nominal 100–300 kHz range. Theremin audio also requires a frame no older than 50 ms.

Valid input values use the update below, with `FILTER_TIME_US = 20000`:

```text
alpha = elapsed_us / (FILTER_TIME_US + elapsed_us)
filtered += alpha × (raw - filtered)
```

The first valid reading, and the first valid reading after an invalid signal,
seeds the filter directly. Calibration captures use raw valid samples, while
normal pitch/volume mapping uses filtered values.

## Mapping hand positions to sound

Each antenna's normalized position is clamped between its captured endpoints:

```text
position = clamp((sensor_hz - far_hz) / (near_hz - far_hz), 0, 1)
```

This supports either electrical direction. The physical far/near captures define
the direction even when approaching an antenna lowers its oscillator frequency.

| Control | Default curve | Alternative |
| --- | --- | --- |
| Pitch | `min_hz × (max_hz / min_hz)^position` | Linear interpolation in Hz |
| Volume | `min_gain + (max_gain - min_gain) × (1 - position)^2` | Linear amplitude |

The far pitch position gives the lowest note; the near pitch position gives the
highest. The far volume position gives maximum gain; the near position gives
minimum gain. These curves act on oscillator frequency, not measured distance.

## Audio task

A priority-5 task generates a sine wave from a 256-entry lookup table. A blocking
DAC DMA write paces 128-sample blocks at nominal 48 kHz on GPIO25. Four DMA
descriptors buffer the output; the driver is configured for automatic alignment
of 8-bit samples to hardware slots.

Pitch and gain approach each new target over successive samples using
`AUDIO_SMOOTH_SAMPLES = 240`. Phase remains continuous between blocks. Zero gain
produces DAC code 128, the midpoint. Stale audio control data causes gain to fade
to zero.

| Mode | Current behavior |
| --- | --- |
| `MODE_THEREMIN` | Maps valid pitch and volume inputs using the saved profile, or the draft during preview |
| `MODE_MEASURE_ONLY` | Measures both inputs while commanding zero audio gain |
| `MODE_TEST_TONE` | Commands the configured fixed tone regardless of oscillator validity |

The current test tone is 240 Hz at gain 0.01. Change the configuration, rebuild,
and flash to change modes or the tone.

## Web and storage boundaries

Wi-Fi initializes before counting and audio start. The HTTP task runs at priority
2 and reads a separate latest-status queue; it does not consume measurement
frames. Browser commands pass through queues to the control task, which owns
the calibration engine.

| Route | Purpose |
| --- | --- |
| `GET /` | Dashboard |
| `GET /api/status` | Latest measurements, freshness, and commanded audio targets |
| `GET /calibration` | Calibration page |
| `GET /api/calibration` | Session, draft profile, capture progress, and telemetry |
| `POST /api/calibration` | Form-encoded calibration commands |

Saving first fades audio to midpoint and waits for the audio task's matching
quiet acknowledgement. The control task stops the timer and counters before
committing one NVS blob, then resets measurement/filter baselines and restarts.
This avoids treating a flash-write pause as an ordinary counting interval.

The saved record is 64 bytes: magic, version, and profile. It is stored in the
`theremin_cal` namespace under `profile`. Invalid or unsupported records fall
back to configuration defaults. Initialization does not erase the NVS partition
to recover from an error.
