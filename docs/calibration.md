# Web calibration

Calibration records four hand positions and assigns the desired pitch and volume
range to them. The response curves control how those outputs change between the
captured oscillator frequencies.

## Connect and check the signals

1. Power the ESP32 and join **Theremin-Setup** using the default password
   **theremin123**.
2. Stay connected if the phone or laptop reports that the network has no internet.
3. Open **http://192.168.4.1/** and check both frequency readings and signal labels.
4. Choose **Calibrate your instrument**.

Keep the phone or laptop away from the antennas while capturing hand positions.
Valid nominal oscillator inputs are 100-300 kHz, with a 2 kHz allowance on each
side. Wi-Fi credentials and input limits are configured in
`main/theremin_config.h`.

## Choose the playing range

| Section | Output settings | Far hand position | Close hand position |
| --- | --- | --- | --- |
| Volume | Highest and lowest volume percentages | Maximum volume | Minimum volume |
| Pitch | Lowest and highest notes | Lowest pitch | Highest pitch |
| Response curves | Musical or linear pitch, quadratic or linear volume | Uses the captured endpoints | Uses the captured endpoints |

Volume percentages scale `AUDIO_MAX_GAIN`. A minimum volume of 0% gives silence
at the close position. The default pitch range is C3-C7. Musical pitch and
quadratic volume are the default curves.

## Capture all four positions

1. Choose the output limits for Volume and Pitch.
2. Press the appropriate **Capture far position** or **Capture close position** button.
3. Position your hand during the three-second countdown.
4. Hold still during the one-second sampling phase. Watch the selected antenna's
   live frequency and signal status in the dialog.
5. Check **Captured average frequency** and press **Done**.
6. Repeat until the two positions for each antenna have been captured.

After completion, **Live now** shows the current frequency alongside the captured
result. **Capture again** repeats a successful attempt. **Try again** repeats a
failed attempt.

**Cancel capture**, including Escape, stops the current attempt and retains
earlier captures. The page's **Cancel** discards the entire draft. If a capture
completed before the device received cancellation, the dialog shows that result.

Each attempt needs at least 75 fresh, valid samples from the selected antenna and
at most 1000 Hz peak-to-peak variation. A failed attempt retains its earlier
endpoint. Each antenna's far/close span must be at least 1000 Hz, or four times
the larger captured variation, whichever is greater.

Capturing physical hand positions determines direction even when approaching
an antenna lowers its oscillator frequency. After the first saved calibration,
existing endpoints can be reused when adjusting notes, levels, or curves.

## Preview and save

| Action | Behavior |
| --- | --- |
| Enter calibration | Continues playing the saved profile |
| Preview | Plays the complete draft in theremin mode |
| Stop preview | Returns to the saved profile |
| Submit changed settings or start another capture | Ends preview and selects the saved profile |
| Save calibration | Validates the draft, pauses audio for storage, and applies the saved profile |
| Cancel | Restores saved settings and returns to the dashboard |

Measurement-only mode commands silence, and test-tone mode plays its configured
fixed tone. Choose theremin mode to audition the calibrated mapping.

A successful Save survives a restart. A failed Save retains the previous saved
profile and keeps the draft available for retry. The page confirms storage
completion through status polling.

One browser session can edit at a time. Closing the page attempts cancellation.
If its requests stop reaching the ESP32, the session expires after 45 seconds
and restores saved settings.

## Resolve capture problems

| Symptom | Check or action |
| --- | --- |
| Capture button unavailable with invalid signal | Check the selected input's frequency, buffer, GPIO, and common ground |
| Missing or interrupted samples | Check the oscillator connection and live validity label, then retry |
| Unstable capture | Hold the hand still and reduce movement around the antenna |
| Far and close readings too similar | Recapture a wider hand range and inspect oscillator sensitivity |
| Connection interrupted | Reconnect and wait for the device's confirmed capture result |
| Settings revert after editing | Confirm that Save completed successfully before leaving the page |

The architecture chapter explains raw samples, session ownership, and the flash
save sequence. [ESP32 technical architecture](architecture.md)
