# Minimal ESP32 theremin prototype

ESP-IDF 6.1 (this workspace), original ESP32/WROOM-32. Both oscillator outputs must be buffered
0-3.3 V square waves, 100-300 kHz. No Raspberry Pi or external DAC is used.
Project and binary name: `theremin_project`.

## Documentation website

The MyST book starts at [docs/index.md](docs/index.md), with chapters for
hardware, firmware/source files, calibration, and verification. Its configuration
is [myst.yml](myst.yml). In PowerShell from this folder:

```powershell
.\docs.cmd install
.\docs.cmd preview
```

Open the local address printed in the terminal, usually http://localhost:3000.
The wrapper uses an installed Node.js/npm or the optional project-local runtime.
Run `.\docs.cmd build` to generate strictly checked HTML in `_build/html/`.
The first build/preview downloads the theme and needs internet access.
See [Editing and publishing this book](docs/writing.md) for equivalent npm commands,
adding pages/figures, and GitHub Pages setup. Documentation builds are separate
from ESP-IDF and do not flash or change the instrument.

## Wiring

| Signal | ESP32 GPIO | Connection |
| --- | --- | --- |
| Pitch oscillator | 34 | Oscillator OUT through 220 ohms |
| Volume oscillator | 35 | Oscillator OUT through 220 ohms |
| Audio DAC1 | 25 | Filter/coupling/attenuation below, then PAM8302A A+ |
| Calibration reminder button | 27 | Optional normally-open button to GND; internal pull-up |
| Ground | GND | Common with both oscillators and amplifier |

These are GPIO numbers, not module pad/header positions. GPIO24 does not exist.
GPIO34/35 have no internal pulls: do not leave an input floating when testing.
An optional external 100 kohm pull-down from each GPIO to GND defines a disconnected
input as low. Do not connect antennas or LC tank nodes directly to GPIOs.
There is no GPIO32-to-GPIO35 jumper.

```text
GPIO25 -- 1k --+-- 1uF nonpolar -- top of 10k potentiometer
              |
             22nF
              |
             GND

Pot bottom -> GND; pot wiper -> amplifier A+; amplifier A- -> GND.
Amplifier VIN -> regulated 5 V; amplifier GND -> common GND.
Speaker -> amplifier OUT+ and OUT- (neither terminal goes to GND).
```

22 nF is marked **223**; 1 uF is **105**. The pot is a setup trim; the antenna
controls volume digitally. Start with the amplifier input trim low. Some PAM8302
modules already include input capacitors and a trim pot. This wiring assumes a
breakout board, not an unpopulated PAM8302A IC. Power the ESP32 dev board via USB
and the amplifier from a suitable regulated 5 V supply, not the ESP32 3.3 V pin.
Keep speaker wiring and amplifier supply returns away from oscillator wiring.

## Files to read

- `main/theremin_config.h`: pins, fallback endpoints, output ceiling, audio range and debug mode.
- `main/main.c`: counters, timer, measurement/filter loop, and audio task.
- `main/calibration.c/.h`: control-task calibration service, command queues, and versioned NVS storage.
- `main/calibration_model.c/.h`: capture state machine, validation, curves, and pitch/volume mapping.
- `main/calibration_protocol.c/.h`: bounded command parsing and calibration JSON formatting.
- `main/calibration_web.c/.h`: calibration assets and HTTP routes.
- `main/web_monitor.c/.h`: Wi-Fi access point, embedded dashboard, and read-only status API.
- `main/web_status.c/.h`: measurement snapshot and JSON formatting, including freshness checks.
- `main/web/`: dashboard HTML, CSS, and JavaScript; no internet dependencies.

Two PCNT **units** count rising edges continuously. An `esp_timer` task callback
snapshots them every 10 ms, with actual per-input elapsed times. One-slot queues
keep the latest frame and audio targets. The main task validates and filters
fresh readings and logs at 4 Hz. A higher-priority audio task streams a phase-
continuous sine wave at nominal 48 kHz to GPIO25 using DAC DMA. Gain and pitch
are smoothed per sample. Ordinary windows do not stop/clear the counters; a rare
brief rebase before one billion pulses prevents signed accumulator overflow.
Saving calibration deliberately pauses measurement and holds audio at midpoint
while flash is written, then restarts with fresh counter baselines and filters.

## Build, flash, debug

In your IDE's ESP-IDF terminal:

```powershell
idf.py build
idf.py -p COM5 flash monitor
```

COM5 is an example; substitute your actual port. Exit the
monitor with Ctrl+]. Build is safe without hardware; flash after wiring is ready.

Change `THEREMIN_MODE` in `main/theremin_config.h`, then rebuild/flash:

| Mode | Behavior | What it isolates |
| --- | --- | --- |
| `MODE_MEASURE_ONLY` | Log both inputs; zero gain gives DAC midpoint silence | Oscillators and counters |
| `MODE_TEST_TONE` | Fixed `TEST_TONE_HZ` and `TEST_TONE_GAIN`; input validity ignored | DAC, amplifier, speaker |
| `MODE_THEREMIN` (current selection) | Pitch + volume from both inputs | Complete prototype |

All modes run the same measurement and DMA paths, with a Wi-Fi dashboard and web
calibration. Preview affects theremin mode; measure-only stays muted and test-tone
mode keeps its configured tone. Calibration can be captured/saved in any mode.
No per-edge GPIO interrupts, waveform switches, or auto-tune are included.

## Live web dashboard

After building and flashing:

1. Connect your phone or laptop to **Theremin-Setup**.
2. Enter the default Wi-Fi password **theremin123**.
3. Stay connected if the phone warns that this network has no internet.
4. Open **http://192.168.4.1/** in a browser. The boot log also prints the AP address.

The dashboard shows both measured oscillator frequencies in Hz, independent signal
validity, the compiled instrument mode, and calculated audio pitch/gain targets.
The audio targets are not measurements of the DAC or speaker. It requests the
latest readings about five times per second; the counters still update every 10 ms.
Invalid inputs are labelled, readings older than 50 ms are marked stale, and a lost
connection clears the displayed values and retries automatically.

Choose **Calibrate your instrument** to open the calibration page described below.
Until a valid calibration has been saved, the instrument uses the endpoints and
audio range in `main/theremin_config.h`.

The Wi-Fi name, password, and channel are configured in `main/theremin_config.h`.
Use an 8-63 character password and rebuild/flash after changing it. Set
`WEB_MONITOR_ENABLED` to `0` and rebuild/flash to compare operation without Wi-Fi.
Compare sensor stability and audio continuity with the dashboard open and closed
on the actual hardware.

Wi-Fi initializes before counting/audio starts, so its startup flash operations do
not interrupt an active tone. The HTTP task reads a separate one-slot snapshot
queue; it never consumes the measurement queue. Calibration requests go through
a separate command queue; the control task owns calibration/audio targets.
NVS stores a versioned calibration record without erasing the partition on errors.
If dashboard startup
fails, the boot log reports the error and the instrument continues without it.

## Bench checks

Recommended first checks:

1. Use measure-only mode. Each input should report `OK`, its known generator
   frequency, and elapsed times close to 10000 us. At 100/200/300 kHz expect
   approximately 1000/2000/3000 rising edges per window, respectively.
2. Use test-tone mode with amplifier trim low. It does not require either
   oscillator. The current settings are 240 Hz and gain 0.01. Adjust loudness with
   the analog input trim.
3. Use theremin mode. Before saving a calibration, the current fallback pitch frequencies
   of 100/200/300 kHz map to about 130.8/523.3/2093.0 Hz. With volume input
   100/200/300 kHz, the target gains are `AUDIO_MAX_GAIN`, one quarter of that,
   and zero, respectively. Allow filters
   and audio smoothing to settle before comparing.
4. Hold one input fixed and vary the other to check independence. Disconnecting
   a grounded/pulled-down input should produce `BAD` and fade out in theremin mode.
5. Press the GPIO27 button: it prints a reminder to open the calibration page.
   It does not capture or change settings by itself.

The boot log shows the compiled mode: 0=theremin, 1=measure only, 2=test tone.
The debug log shows raw sensor frequencies, validity, actual window times,
and synthesis state/target frequency/gain. A silent target is shown as
`synth=MUTED target=0.0 Hz gain=0.000`. These are commanded values, not measurements
of GPIO25 or the speaker. There is no microphone or audio-output feedback input
in this project. Running only `idf.py monitor` does not update the board's firmware;
rebuild and flash after changing the mode or audio settings.

Gain is a linear amplitude multiplier, not dB
or speaker power. A `BAD` reading mutes theremin mode; it can mean no pulses,
outside 98-302 kHz (2 kHz allowance around the nominal 100-300 kHz band), or an
abnormal measurement interval. Calibration capture uses the same sensor limits.
If controls stop updating for 50 ms, the audio task also fades to silence.
Reboot may cause an analog pop; the amplifier shutdown pin is not controlled yet.

## Web calibration

Open **http://192.168.4.1/calibration** or use the dashboard link. There are exactly
three sections, with no Advanced controls:

1. **Volume:** choose the highest volume and capture your far hand position;
   choose the lowest volume (including 0% silence) and capture the close position.
   Percentages are relative to `AUDIO_MAX_GAIN` in `main/theremin_config.h`. The web interface
   cannot raise this configured ceiling.
2. **Pitch:** choose your lowest note and capture the far position; choose your
   highest note and capture the close position. The current default range is C3-C7.
3. **Response curves:** choose musical (equal note intervals) or linear frequency
   for pitch, and quadratic or linear amplitude for volume. Musical/quadratic
   preserve the original mapping behavior.

Each capture has a three-second countdown and a one-second sampling window.
Pressing a Capture button opens a focused dialog with a large **3, 2, 1** countdown,
the selected antenna's live oscillator frequency and signal status, then a
**Hold** phase with progress. The confirmed result stays open until **Done**.
On success, the large value is labelled **Captured average frequency**; **Live now**
continues to show the current reading separately. Failures show a reason and a
**Try again** button, rather than treating an existing endpoint as a new capture.
**Capture again** repeats a successful capture if you want to adjust that position.

**Cancel capture** (or Escape) stops only the current attempt and retains earlier
captures and selected output settings. It is separate from the page's **Cancel**,
which discards the entire draft. If the capture completed before cancellation
reached the instrument, the dialog reports that completed result. A connection
interruption clears the live reading and waits for the ESP32's confirmed outcome;
it does not claim success from a previously captured endpoint.

Keep the phone away from the antennas and hold your hand still during capture.
Only the selected antenna must have a valid signal. A capture requires at least
75 fresh samples and rejects interrupted signals or more than 1000 Hz peak-to-peak
variation. Rejected captures leave the previous endpoint unchanged.

Capture all four endpoints before the first Preview or Save. Once a calibration
has been saved, existing captures can be reused when changing notes, levels, or
curves. Near/far oscillator frequency may increase or decrease independently for
each antenna; capturing their physical hand positions determines the direction.
Each antenna needs at least a 1000 Hz span, or four times its captured peak-to-peak
variation if that is larger. The page explains when recapture is needed.

**Preview** auditions a complete draft without writing flash. **Stop preview**
returns to the saved profile. Changing settings or starting another capture also
ends the current preview. **Cancel** discards the draft, restores saved settings,
and returns to the dashboard. Browser closing sends a best-effort cancellation;
if that request cannot arrive, the device cancels the draft after 45 seconds
without requests from the editing page. One browser can edit at a time.

**Save calibration** validates the complete profile, fades to silent DAC samples,
waits for the DMA buffers to become quiet, stops the measurement timer/counters,
then commits one versioned NVS blob in the `theremin_cal` namespace. It restarts
measurement with cleared counters and reset filter baselines before audio resumes.
Save failures retain the previously saved profile and leave the draft available
for retry. Valid saved calibration is loaded before Wi-Fi/audio startup. Missing,
unsupported, corrupt, or out-of-bounds records fall back to the configured defaults.
Changing fallback endpoint macros does not replace an existing saved calibration.

Curves operate on normalized oscillator frequency between the captured endpoints;
they do not establish physical distance in centimetres. Multi-point distance
calibration and other Advanced settings are not implemented in this stage.

## Limits

- 10 ms rising-edge windows have a nominal 100 Hz sensor-frequency step;
  stability depends on the actual antenna frequency swing and noise.
- The internal DAC is 8 bit. Quiet signals can sound grainy; digital gain is
  capped by `AUDIO_MAX_GAIN`. Gains below approximately 0.004 round to midpoint
  silence with the current sample conversion. Use analog attenuation appropriately
  before raising `AUDIO_MAX_GAIN` toward 1.0 for better use of the DAC range.
- The 100 ns PCNT filter requires high and low pulses comfortably longer than
  100 ns; ordinary 50%-duty 100-300 kHz inputs meet this requirement.
- DMA buffering, sensor filtering and gain smoothing add latency beyond the
  10 ms measurement interval. No measured end-to-end latency is claimed.
- The PAM8302A switches around 250 kHz, inside the sensor range. Compare sensor
  readings with the amplifier on/off if you see unexplained coupling.

## Host checks

`tests/test_web_status.c` checks waiting, valid, invalid, stale, nonfinite, and
future-timestamp readings, plus output buffer bounds. It prints JSON fixtures to
stdout and a pass message to stderr. With a host C compiler, for example:

```text
cc -Imain main/web_status.c tests/test_web_status.c -lm -o build/test_web_status
```

Run the resulting executable. `node tests/test_dashboard.mjs` tests the actual
dashboard script against simulated waiting/live/stale/invalid readings, failed
requests, reconnection, and scheduling without overlapping requests.

`tests/test_calibration.c` checks both electrical directions, endpoint clamping,
curve mapping, output bounds, capture timing/stability, session ownership/expiry,
preview/cancel, failed/successful saves, restored profiles, and strict form parsing:

```text
cc -Imain main/calibration_model.c main/calibration_protocol.c main/web_status.c tests/test_calibration.c -lm -o build/test_calibration
node tests/test_calibration_page.mjs
```

The calibration-page test covers the actual script's capture, preview, save,
disconnect/reconnect, editor lock, cancellation, and draft-field behavior.
It also checks the capture dialog's countdown/live readings, held success/failure
results, retry, keyboard cancellation, cancellation during preparation, and lost
capture replies. Device tests check per-attempt results and cancellation IDs so
a delayed cancellation cannot stop a newer attempt.
`tests/lcc_math_compat.c` supplies missing C99 math symbols only when running host
tests with MATLAB's bundled Windows LCC runtime. ESP-IDF/GCC/Clang do not need it.

After flashing, verify the full four-point flow on hardware, Cancel restoring the
old mapping, Save restoring after reset, and audio resuming after the save pause.


