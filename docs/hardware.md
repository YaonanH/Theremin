# Hardware and wiring

Use an original ESP32/WROOM-32 development board. Feed each sensor input from a
buffered 0-3.3 V oscillator output in the nominal 100-300 kHz range. Connect the
oscillator buffers and audio amplifier to a common ground.

## Pin connections

| Signal | ESP32 GPIO | Connection |
| --- | --- | --- |
| Pitch oscillator | 34 | Buffered square-wave output through 220 ohms |
| Volume oscillator | 35 | Buffered square-wave output through 220 ohms |
| Audio DAC1 | 25 | Filter, coupling capacitor, and input trim shown below |
| Calibration reminder button | 27 | Optional normally-open button to GND |
| Ground | GND | Common with oscillator buffers and amplifier |

Use the GPIO labels when locating pins on the development board. GPIO34 and
GPIO35 require external pull resistors if disconnected inputs need a defined
level. A 100 kohm pull-down from each input to GND supports disconnection tests.
Connect antenna and LC tank nodes to their oscillator/buffer circuits, and
connect only the buffered outputs to the ESP32.

GPIO27 uses its internal pull-up. Pressing its button prints a reminder to open
the web calibration page.

## DAC, filter, and amplifier

The prototype feeds a PAM8302A amplifier breakout through a filter, coupling
capacitor, and potentiometer.

```text
GPIO25 -- 1k --+-- 1uF nonpolar -- top of 10k potentiometer
              |
             22nF
              |
             GND

Pot bottom    -> GND
Pot wiper     -> amplifier A+
Amplifier A-  -> GND
Amplifier VIN -> regulated 5 V
Amplifier GND -> common GND
Speaker       -> amplifier OUT+ and OUT-
```

Keep both speaker terminals connected to the amplifier outputs and isolated from
ground. Check the breakout's existing coupling capacitors and trim before adding
equivalent components. Capacitor markings are **223** for 22 nF and **105** for
1 uF.

Power the ESP32 development board through USB and the amplifier from a suitable
regulated 5 V supply. Keep speaker wiring and amplifier supply returns away from
the oscillator wiring.

## Output level

Start with the amplifier input trim low. The potentiometer sets analog input
attenuation. Hand position controls digital gain while playing.

`AUDIO_MAX_GAIN` sets the firmware's amplitude ceiling. Calibration volume
percentages scale that ceiling. Acoustic loudness also depends on amplifier
gain, supply, speaker, and analog attenuation.

The internal DAC has 8-bit resolution. With the current sample conversion,
digital gains below approximately 0.004 round a sine wave to midpoint silence.
Analog attenuation allows a larger digital signal while controlling the level
at the amplifier input.

## Hardware records

Store project photos, oscillator schematics, and wiring diagrams in
`docs/figures/`. Include board/module identifiers and component values so another
reader can reproduce the setup. The editing chapter describes figure captions
and references. [Editing and publishing](writing.md)
