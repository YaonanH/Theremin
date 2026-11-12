# Web calibration

## Connect to the instrument

1. Power the flashed ESP32 and connect to Wi-Fi **Theremin-Setup**.
2. Enter the current default password **theremin123**.
3. Stay connected if the device reports that the network has no internet.
4. Open **http://192.168.4.1/** and choose **Calibrate your instrument**.

The dashboard shows raw oscillator frequencies and their validity. Audio pitch
and gain are commanded targets, not microphone or speaker measurements. Keep the
phone or laptop away from the antennas while positioning your hand.

This book is a separate documentation website. The operational dashboard and
calibration page are embedded in the ESP32 firmware under `main/web/`.

## Three calibration sections

| Section | What to choose | What to capture |
| --- | --- | --- |
| Volume | Highest and lowest volume percentages | Far hand position for maximum volume; close position for minimum volume |
| Pitch | Lowest and highest notes | Far position for low pitch; close position for high pitch |
| Response curves | Musical or linear pitch; quadratic or linear volume | No additional endpoint captures |

The volume percentages refer to `AUDIO_MAX_GAIN`, not speaker power. The web page
cannot raise that compiled ceiling. Musical pitch and quadratic volume preserve
the default mapping. There are no Advanced controls in this implementation.

## Capture a position

1. Choose the output limits for the section.
2. Press the appropriate **Capture far position** or **Capture close position** button.
3. Use the dialog's three-second countdown to position your hand.
4. Hold still for the one-second sampling phase. The dialog shows the selected
   antenna's live frequency and signal status.
5. Check the confirmed **Captured average frequency**, then press **Done**.

The result stays visible until dismissed. **Live now** continues to show the
current frequency separately. Use **Capture again** after success or **Try again**
after failure when you need another attempt.

Only the selected antenna needs a valid signal for its capture. A successful
attempt requires at least 75 fresh samples and no more than 1000 Hz peak-to-peak
variation. Missing/interrupted readings or excessive variation reject the attempt
without replacing an earlier endpoint.

**Cancel capture**, including Escape, cancels only that attempt. The page's
**Cancel** discards the whole draft. If an attempt finished before cancellation
reached the ESP32, the dialog reports its completed result.

## Validate the complete range

Capture all four endpoints before the first Preview or Save. After a profile is
saved, its existing endpoints can be reused when changing notes, levels, or curves.

Each antenna needs a span of at least 1000 Hz, or four times the larger endpoint's
captured peak-to-peak variation, whichever is greater. Move the hand positions
farther apart or improve stability if validation reports an inadequate span.

Near/far frequency can increase or decrease independently for the two antennas.
Capture physical hand positions rather than assuming which reading must be larger.

## Preview, save, and cancel

| Action | Effect in the current firmware |
| --- | --- |
| Preview | Auditions a complete draft in theremin mode without writing flash |
| Stop preview | Returns to the saved profile |
| Change submitted settings or start another capture | Ends preview and uses the saved profile |
| Save calibration | Validates, briefly silences audio for NVS storage, and applies the saved profile |
| Cancel | Discards the draft, restores saved settings, and returns to the dashboard |

Opening the calibration page currently leaves the saved profile playing.
Automatically muting on entry and resuming only after Preview/Save is a requested
follow-up; it is not present in the source documented here. Measurement-only mode
stays silent; test-tone mode uses its fixed configured tone rather than auditioning
the calibrated mapping.

Save failures retain the previously saved profile and keep the draft available
for retry. A valid saved profile loads after reboot. Changing fallback endpoint
macros does not overwrite an existing valid saved calibration.

One browser session can edit at a time. Closing the page attempts cancellation;
if that request cannot arrive, the session expires after 45 seconds without
owner requests and restores the saved settings.

## If capture is unavailable

Check the live signal labels first. The nominal input range is 100–300 kHz, with
the configured 2 kHz allowance on each side. Verify the oscillator buffer, common
ground, GPIO assignment, and actual signal frequency if an input is invalid.

A lost connection clears live readings and waits for a confirmed capture outcome.
An old endpoint alone does not prove the latest capture succeeded. See the
[verification guide](testing.md) for measurement-only checks.
