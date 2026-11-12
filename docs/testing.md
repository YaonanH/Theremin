# Build and verification

## Build and flash

Open an ESP-IDF 6.1 terminal in the project folder and run the following commands.

```powershell
idf.py build
idf.py -p COM5 flash monitor
```

Replace `COM5` with the board's port. Exit the serial monitor with Ctrl+]. After
changing `main/theremin_config.h`, rebuild and flash to apply the settings to the
board. `idf.py monitor` opens the serial connection to the installed firmware.

## Bench checks

Start with the amplifier input trim low. Select the required `THEREMIN_MODE`,
rebuild, and flash for each mode-specific check. Follow the connection diagram
when wiring the bench setup. [Hardware and wiring](hardware.md)

| Check | Procedure | Expected observation |
| --- | --- | --- |
| Frequency measurement | Select measurement-only mode and apply known square waves | Both channels show valid frequencies and windows near 10000 us, with zero audio gain |
| Audio output | Select test-tone mode | A tone at the configured 240 Hz and gain 0.01 is audible through the connected output path |
| Independent controls | Select theremin mode and vary one input at a time | Pitch and volume respond to their respective inputs |
| Invalid input | Disconnect a sensor input with an external pull-down fitted | The reading becomes invalid and theremin audio fades to silence |
| Capture and Preview | Capture all four endpoints and select Preview | The draft limits and curves govern theremin playback |
| Cancel | Cancel a draft with altered settings | The previous saved mapping is restored |
| Save and restart | Save a complete draft and reset the ESP32 | The saved profile is loaded at startup |
| Save pause | Observe audio and readings during Save | Audio pauses for storage and measurement resumes with fresh baselines |

With a nominal 10 ms window, 100/200/300 kHz inputs produce approximately
1000/2000/3000 rising edges. With the configured fallback profile, those pitch
frequencies map to approximately 130.8/523.3/2093.0 Hz. The same volume inputs map
to `AUDIO_MAX_GAIN`, one quarter of that gain, and zero.

The serial log and dashboard show input measurements and commanded audio
targets. Use an oscilloscope or audio measurement setup to verify the DAC,
amplifier, and acoustic output.

## Host checks

Portable C checks cover calculation, validation, and state handling. JavaScript
checks execute the actual dashboard/calibration scripts with simulated requests
and readings.

| Test | Coverage |
| --- | --- |
| `tests/test_web_status.c` | Waiting, valid, invalid, stale, nonfinite, timestamp, and buffer-bound cases |
| `tests/test_calibration.c` | Electrical directions, curves, clamping, captures, sessions, preview, save outcomes, and command parsing |
| `tests/test_dashboard.mjs` | Polling, readings, disconnection, reconnection, and request scheduling |
| `tests/test_calibration_page.mjs` | Draft fields, capture dialog, cancellation/retry, lost replies, editor ownership, preview, and save |

For a Windows GCC-compatible host compiler, create the output folder, compile,
and run the tests from the project root.

```powershell
New-Item -ItemType Directory -Path build -Force | Out-Null
cc -std=c11 -Imain main/web_status.c tests/test_web_status.c -lm -o build/test_web_status.exe
cc -std=c11 -Imain main/calibration_model.c main/calibration_protocol.c main/web_status.c tests/test_calibration.c -lm -o build/test_calibration.exe
.\build\test_web_status.exe
.\build\test_calibration.exe
```

Run the page-script checks in a terminal with Node.js on PATH.

```powershell
node tests/test_dashboard.mjs
node tests/test_calibration_page.mjs
```

`tests/lcc_math_compat.c` supplies missing C99 math functions when using MATLAB's
bundled Windows LCC runtime. Real peripheral operation, flash timing, and audio
continuity are covered by the bench checks.

## Record results and limitations

For each hardware test, record the date, firmware revision, relevant settings,
equipment, input conditions, expected result, and observed result. Include units
and label values obtained from serial logs, electrical measurements, or acoustic
measurements.

A 10 ms counting window gives a nominal 100 Hz sensor-frequency step. Practical
control resolution depends on oscillator stability and antenna frequency swing.
Filtering, DMA buffering, smoothing, and the analog circuit contribute to the
measured end-to-end latency.

If unexplained coupling appears, compare readings with the amplifier enabled
and disabled, then with Wi-Fi enabled and disabled. Reboot behavior also depends
on the analog output circuit and amplifier startup.
