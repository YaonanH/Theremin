# Build and verification

## Build and flash the firmware

Use an ESP-IDF 6.1 terminal configured for this project and the original ESP32:

```powershell
idf.py build
idf.py -p COM5 flash monitor
```

Replace `COM5` with the connected board's port. Exit the serial monitor with
Ctrl+]. Running only `idf.py monitor` does not update firmware already on the board.

Change `THEREMIN_MODE` in `main/theremin_config.h`, then rebuild and flash for each
mode-specific bench check.

## Portable host checks

The portable C tests require a host C compiler with C11 and the math library.
The commands below are for GCC/Clang-style toolchains; create `build/` first if it
does not exist. Execute each resulting program after compiling it.

```text
cc -std=c11 -Imain main/web_status.c tests/test_web_status.c -lm -o build/test_web_status
cc -std=c11 -Imain main/calibration_model.c main/calibration_protocol.c main/web_status.c tests/test_calibration.c -lm -o build/test_calibration
```

On PowerShell with Windows executables, run:

```powershell
.\build\test_web_status.exe
.\build\test_calibration.exe
```

Run the browser-script tests with Node.js:

```powershell
node tests/test_dashboard.mjs
node tests/test_calibration_page.mjs
```

| Check | Coverage |
| --- | --- |
| `test_web_status.c` | Waiting/valid/invalid/stale readings, nonfinite values, timestamp handling, and buffer bounds |
| `test_calibration.c` | Electrical direction, clamping, curves, output limits, captures, ownership, expiry, preview/cancel, save outcomes, and form parsing |
| `test_dashboard.mjs` | Actual dashboard script with simulated data, disconnection/reconnection, and request scheduling |
| `test_calibration_page.mjs` | Actual calibration script, capture dialog, retry/cancellation, lost replies, draft fields, preview/save, and editor ownership |

`tests/lcc_math_compat.c` is only for MATLAB's bundled Windows LCC runtime, which
lacks some C99 math symbols. ESP-IDF, GCC, and Clang do not need it. Host checks do
not exercise real PCNT peripherals, DAC DMA, flash timing, or acoustic output.

## Bench sequence

Start with the amplifier input trim low and the wiring in the
[hardware guide](hardware.md).

| Step | Procedure | Expected observation |
| --- | --- | --- |
| 1. Counters | Use measurement-only mode with known square-wave inputs | Both channels show their input frequencies, valid signals, and windows near 10000 µs; audio target is muted |
| 2. Audio path | Use test-tone mode | The current 240 Hz, gain 0.01 command produces a tone if the DAC/filter/amplifier/speaker path is connected |
| 3. Mapping | Use theremin mode; vary one input while holding the other fixed | Pitch and volume respond independently |
| 4. Invalid input | Disconnect an input with a ground-defining pull-down | Invalid signal and fade to silence in theremin mode |
| 5. Calibration | Capture four positions, preview, then cancel | Captures are confirmed and Cancel restores the saved mapping |
| 6. Persistence | Capture and save, reset the ESP32 | The saved profile is restored after restart |
| 7. Flash pause | Save while monitoring the instrument | Audio pauses during storage and measurement resumes with fresh baselines |

At a nominal 10 ms window, 100/200/300 kHz inputs produce about 1000/2000/3000
rising edges. Before a saved calibration overrides defaults, those pitch inputs
map to approximately 130.8/523.3/2093.0 Hz. The same volume inputs map to
`AUDIO_MAX_GAIN`, one quarter of that gain, and zero with the quadratic curve.

## Record measured results

Add dated results here as the hardware is tested. Record the firmware revision,
configuration, instruments, input conditions, expected result, and observed result.
Keep commanded audio targets separate from oscilloscope or acoustic measurements.

No numerical end-to-end latency or acoustic output measurement is included in
this starter. Nominal single-window frequency resolution is about 100 Hz; useful
control resolution also depends on oscillator stability and antenna frequency swing.

Compare readings with the amplifier enabled/disabled and Wi-Fi active/inactive
if you suspect coupling. The existing README notes amplifier switching near the
sensor band. Reboot can also cause an analog pop because amplifier shutdown is
not controlled by the firmware.
