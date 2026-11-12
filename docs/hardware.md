# Hardware and wiring

## Board and sensor inputs

Use an original ESP32/WROOM-32 with its internal DAC on GPIO25. The firmware
explicitly rejects other ESP-IDF targets. Both oscillator outputs must be buffered
0–3.3 V square waves, nominally 100–300 kHz.

| Signal | ESP32 GPIO | Connection |
| --- | --- | --- |
| Pitch oscillator | 34 | Buffered oscillator OUT through 220 ohms |
| Volume oscillator | 35 | Buffered oscillator OUT through 220 ohms |
| Audio DAC1 | 25 | Filter, coupling capacitor, and attenuation below |
| Calibration reminder | 27 | Optional normally-open button to GND; internal pull-up |
| Ground | GND | Common with the oscillator buffers and amplifier |

These numbers identify GPIOs, not module pad positions. GPIO34 and GPIO35 have no
internal pull resistors. An optional external 100 kohm pull-down on each input
defines a disconnected signal as low. Do not leave the inputs floating during a
disconnection test, and do not connect an antenna or LC tank directly to a GPIO.

The GPIO27 button currently prints a reminder to open web calibration. It does
not capture a hand position.

## Audio path

The present prototype uses an internal DAC, an analog filter and coupling stage,
and a PAM8302A amplifier breakout:

```text
GPIO25 -- 1k --+-- 1uF nonpolar -- top of 10k potentiometer
              |
             22nF
              |
             GND

Pot bottom -> GND
Pot wiper  -> amplifier A+
Amplifier A- -> GND

Amplifier VIN -> regulated 5 V
Amplifier GND -> common GND
Speaker -> amplifier OUT+ and OUT-
```

Neither speaker terminal connects to ground. This wiring assumes an amplifier
breakout; inspect the module because it may already include input capacitors or
an input trim. Capacitor markings are **223** for 22 nF and **105** for 1 uF.

Power the ESP32 development board through USB and use a suitable regulated 5 V
supply for the amplifier. Do not power the amplifier from the ESP32 3.3 V pin.
Keep speaker wiring and amplifier supply returns away from oscillator wiring.

## Setting the output level

Start with the amplifier's analog input trim low. `AUDIO_MAX_GAIN` caps digital
amplitude, and the calibration page's percentages are relative to that cap.
They are not percentages of acoustic loudness or speaker power.

An 8-bit DAC has limited resolution at low digital gain. The current conversion
rounds very small sine amplitudes to midpoint silence, approximately below gain
0.004. Analog attenuation lets the DAC use more of its range while controlling
the amplifier input level. Check the actual circuit before raising digital gain.

## Photos and schematics

Add original project photos, oscillator schematics, and wiring diagrams under
`docs/figures/` as they become available. The [editing guide](writing.md) explains
how to include them with captions. This starter does not substitute an unverified
oscillator schematic for your actual hardware.
