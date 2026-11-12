# Hardware and wiring

The prototype combines an original ESP32/WROOM-32 development board with two
external oscillator channels for pitch and volume. The oscillator assembly uses
a **5 V supply** and **four physical IC packages**, comprising three CD4093BE
packages and one CD4070BE package. Connect the oscillator assembly, ESP32, and
audio amplifier to a common ground.

## Oscillator supply and IC allocation

The supplied reference schematics label the oscillator logic supply as +15 V.
The assembled prototype runs these oscillator circuits at **+5 V**. The labels
below retain the gate identifiers from those schematics, while each table row
represents one physical IC in the assembly.

| Physical package | IC type | Schematic gates grouped in the package | Function | Gates used |
| --- | --- | --- | --- | --- |
| 1 | CD4093BE | U2A, U2B, U9A, U9B | Pitch and volume reference oscillators and their following stages | 4 of 4 |
| 2 | CD4093BE | U8A, U8B, U1A, U1B | Volume and pitch antenna oscillators and their following stages | 4 of 4 |
| 3 | CD4070BE | U3A, U10A | Pitch and volume XOR stages | 2 of 4 |
| 4 | CD4093BE | U4A, U11A | Final pitch and volume Schmitt-trigger stages | 2 of 4 |

The assembly therefore uses **3 x CD4093BE + 1 x CD4070BE = 4 IC packages**.
Ten of the twelve available CD4093BE gates and two of the four CD4070BE gates
are used by the two channels shown. The two additional gates in each partly
used package are outside these signal paths.

The CD4093BE contains four two-input NAND gates with Schmitt-trigger inputs.
The CD4070BE contains four two-input XOR gates. Both device families support
operation at 5 V. The gate identifiers describe the stages in the reference
drawings, and physical pin assignments follow the selected gate within each
shared package. [CD4093B device information](https://www.ti.com/product/CD4093B),
[CD4070B device information](https://www.ti.com/product/CD4070B)

## Pitch and volume signal paths

Each channel has an antenna-dependent oscillator and an adjustable reference
oscillator. A following CD4093BE Schmitt-trigger inverter feeds each signal to the
CD4070BE XOR stage. The XOR output passes through an RC network and a final
CD4093BE Schmitt-trigger stage.

```{mermaid}
flowchart TB
    subgraph Pitch[Pitch channel - 5 V logic supply]
        PA[Pitch antenna] --> P1[U1A - antenna oscillator]
        P1 --> P2[U1B - Schmitt inverter]
        PR[U2A - adjustable reference oscillator] --> P3[U2B - Schmitt inverter]
        P2 --> PX[U3A - CD4070BE XOR]
        P3 --> PX
        PX --> PF[R2 and C3 - RC network]
        PF --> PO[U4A - Schmitt-trigger output stage]
    end
    subgraph Volume[Volume channel - 5 V logic supply]
        VA[Volume antenna] --> V1[U8A - antenna oscillator]
        V1 --> V2[U8B - Schmitt inverter]
        VR[U9A - adjustable reference oscillator] --> V3[U9B - Schmitt inverter]
        V2 --> VX[U10A - CD4070BE XOR]
        V3 --> VX
        VX --> VF[R16 and C10 - RC network]
        VF --> VO[U11A - Schmitt-trigger output stage]
    end
```

The component labels and values visible in the supplied schematics are recorded
below. These identify the reference circuit used for the prototype.

| Circuit element | Pitch channel | Volume channel |
| --- | --- | --- |
| Antenna oscillator capacitor | C1, 22 pF | C8, 22 pF |
| Antenna oscillator feedback resistor | R1, 15 kohm | R15, 15 kohm |
| Reference oscillator capacitor | C2, 33 pF | C9, 33 pF |
| Adjustable reference resistance | RV1, 20 kohm | RV7, 20 kohm |
| Resistor after the XOR stage | R2, 1 kohm | R16, 1 kohm |
| Capacitor before the final Schmitt stage | C3, 470 pF | C10, 470 pF |

## ESP32 input interface

The firmware expects conditioned **0-3.3 V square waves** in the nominal
100-300 kHz range at GPIO34 and GPIO35. The prototype uses buffered outputs with
reduced amplitude for this connection. The buffer and amplitude reduction provide
the interface between the 5 V oscillator logic and the ESP32 input level. The
220 ohm series resistors are part of the input connections.
[ESP32 electrical characteristics](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)

The two oscillator channels have symmetric circuit structures. Either assembled
channel can serve pitch or volume, with its role determined by the ESP32 wiring
and calibration. GPIO34 is assigned to pitch and GPIO35 to volume in the current
firmware.

## Pin connections

| Signal | ESP32 GPIO | Connection |
| --- | --- | --- |
| Pitch sensor input | 34 | Conditioned 0-3.3 V square wave through 220 ohms |
| Volume sensor input | 35 | Conditioned 0-3.3 V square wave through 220 ohms |
| Audio DAC1 | 25 | Filter, coupling capacitor, and input trim shown below |
| Calibration reminder button | 27 | Optional normally-open button to GND |
| Ground | GND | Common with oscillator buffers and amplifier |

Use the GPIO labels when locating pins on the development board. GPIO34 and
GPIO35 require external pull resistors if disconnected inputs need a defined
level. A 100 kohm pull-down from each input to GND supports disconnection tests.
Connect antennas to the oscillator circuits and route the selected, level-
conditioned signals to the ESP32.

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
