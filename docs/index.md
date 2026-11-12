# ESP32 Theremin

This project is a two-antenna theremin prototype built around an original
ESP32/WROOM-32. Two external oscillators provide frequency signals. The ESP32
measures them, maps hand positions to musical pitch and volume, and generates a
sine wave through its internal DAC. A local Wi-Fi interface displays readings and
captures calibration points.

## System overview

```{mermaid}
flowchart TB
    P[Pitch antenna and external oscillator] --> PC[GPIO34: pulse counter]
    V[Volume antenna and external oscillator] --> VC[GPIO35: pulse counter]
    PC --> M[10 ms snapshots and signal validation]
    VC --> M
    M --> F[Filtering and calibrated mapping]
    F --> A[Sine synthesis and DAC DMA]
    A --> D[GPIO25: filter and input trim]
    D --> S[PAM8302A amplifier and speaker]
    M --> W[Local Wi-Fi dashboard]
    W --> C[Web calibration commands]
    C --> F
    C --> N[Saved profile in NVS flash]
    N --> F
```

The diagram shows logical data flow. Oscillator outputs must be buffered,
0–3.3 V square waves; antenna and LC tank nodes are not GPIO inputs.

## Current capabilities

| Area | Implementation |
| --- | --- |
| Sensor inputs | Two independent rising-edge pulse-counter units; nominal 100–300 kHz inputs |
| Measurement | 10 ms windows using actual elapsed times; filtered control values |
| Audio | Phase-continuous sine synthesis, nominal 48 kHz sample rate, internal 8-bit DAC |
| Operating modes | Theremin, measurement only, and fixed test tone |
| Local interface | ESP32 Wi-Fi access point, live frequencies, validity, and audio targets |
| Calibration | Four hand-position endpoints, output limits, pitch/volume curves, preview, and NVS save |

The prototype uses ESP-IDF 6.1. Configuration values cited in this documentation
describe the current `main/theremin_config.h`; check that file when changing pins,
frequency limits, or audio levels.

## Read the documentation

- [Hardware and wiring](hardware.md): GPIO assignments and the audio connection.
- [Firmware and source files](firmware.md): counters, task boundaries, mapping, and file responsibilities.
- [Web calibration](calibration.md): connect, capture, preview, and save.
- [Build and verification](testing.md): firmware commands, host checks, and bench procedures.
- [Editing this book](writing.md): local preview, new chapters, figures, and GitHub Pages setup.

## Scope and evidence

The source implements frequency measurement and audio generation, with no
microphone or audio-output feedback measurement. The web interface's audio pitch
and gain are commanded targets. They do not prove that the amplifier or speaker
is producing sound.

This book documents the implementation and provides reproducible checks. It does
not claim measured end-to-end latency, acoustic output power, or a calibrated
distance in centimetres. Record those results in [Build and verification](testing.md)
when they have been measured on the actual instrument.
