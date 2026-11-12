# Firmware and source files

All paths in this chapter are relative to the project root. The architecture
chapter describes how the modules run together. [ESP32 technical architecture](architecture.md)

## Source-file reference

| File or directory | Responsibility |
| --- | --- |
| `main/main.c` | Pulse-counter setup, timer snapshots, input processing, audio task, and save coordination |
| `main/theremin_config.h` | GPIOs, sensor limits, fallback endpoints, audio settings, Wi-Fi credentials, and mode |
| `main/calibration_model.c` and `.h` | Calibration state machine, capture statistics, validation, and response curves |
| `main/calibration.c` and `.h` | Command/reply queues, calibration snapshots, and versioned NVS storage |
| `main/calibration_protocol.c` and `.h` | Form-command parsing and calibration JSON formatting |
| `main/calibration_web.c` and `.h` | Calibration assets and HTTP handlers |
| `main/web_monitor.c` and `.h` | Wi-Fi access point, dashboard HTTP handlers, and telemetry queue |
| `main/web_status.c` and `.h` | Telemetry JSON formatting and freshness checks |
| `main/web/index.html`, `dashboard.css`, `dashboard.js` | Live dashboard layout, styling, and polling |
| `main/web/calibration.html`, `calibration.css`, `calibration.js` | Calibration form, capture dialog, preview, and save controls |
| `main/CMakeLists.txt` | Component dependencies and HTML/CSS/JavaScript embedded in firmware |
| `sdkconfig.defaults` | Project defaults applied when configuring a new ESP-IDF build |
| `sdkconfig` | Resolved settings for the current local ESP-IDF build |
| `tests/` | Portable C checks and JavaScript checks of the browser scripts |

## Configuration reference

Change application settings in `main/theremin_config.h`, then rebuild and flash.
The values below describe the current configuration.

| Setting | Value | Purpose |
| --- | --- | --- |
| `PITCH_INPUT_GPIO`, `VOLUME_INPUT_GPIO` | 34, 35 | Buffered oscillator inputs |
| `CALIBRATION_GPIO` | 27 | Optional reminder button |
| `MEASUREMENT_US` | 10000 | Counter snapshot interval |
| `SENSOR_MIN_HZ`, `SENSOR_MAX_HZ` | 100000, 300000 Hz | Nominal sensor band |
| `SENSOR_MARGIN_HZ` | 2000 Hz | Allowance around the nominal band |
| `CONTROL_TIMEOUT_US` | 50000 | Maximum age for fresh control data |
| `FILTER_TIME_US` | 20000 | Sensor-filter parameter |
| Pitch/volume far and near endpoints | 100000, 300000 Hz | Fallback mapping until valid calibration is loaded |
| `AUDIO_MIN_HZ`, `AUDIO_OCTAVES` | 130.8128 Hz, 4 octaves | Default C3-C7 playing range |
| `AUDIO_MAX_GAIN` | 0.5 | Digital amplitude ceiling |
| `AUDIO_SAMPLE_RATE` | 48000 Hz | Nominal DAC conversion rate |
| `AUDIO_BLOCK_SAMPLES` | 128 | Samples generated per audio block |
| `AUDIO_SMOOTH_SAMPLES` | 240 | Pitch/gain smoothing divisor |
| `WEB_MONITOR_ENABLED` | 1 | Enables the ESP32 dashboard and calibration server |

A valid saved calibration supplies the playing endpoints, output limits, and
response curves at startup. Recalibrate and save to replace those stored values.

## Operating modes

Select `THEREMIN_MODE` before building.

| Mode | Sensor handling | Audio behavior |
| --- | --- | --- |
| `MODE_THEREMIN` | Both inputs must be valid and fresh | Plays the saved mapping or the draft during Preview |
| `MODE_MEASURE_ONLY` | Measures and reports both inputs | Commands zero gain |
| `MODE_TEST_TONE` | Continues measuring both inputs | Plays `TEST_TONE_HZ` at `TEST_TONE_GAIN`, independently of sensor validity |

The current test-tone settings are 240 Hz and gain 0.01. The web interface remains
available in all three modes when `WEB_MONITOR_ENABLED` is enabled.

## Where to make a change

| Change | Starting point |
| --- | --- |
| Alter pins, frequency range, output ceiling, or mode | `main/theremin_config.h` |
| Adjust counter sampling or audio buffering | `main/main.c` |
| Change capture rules or mapping equations | `main/calibration_model.c` |
| Change saved-profile layout | `main/calibration.c`, including record version and validation |
| Add a calibration command | `main/calibration_protocol.c`, `main/calibration_model.c`, and the page script |
| Change live status fields | `main/web_status.c`, its header, and the dashboard script |
| Change calibration interaction | `main/web/calibration.html`, `.css`, and `.js` |

Run the relevant host checks after behavior changes and verify peripheral/audio
changes on the instrument. [Build and verification](testing.md)
