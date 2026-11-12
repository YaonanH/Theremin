# ESP32 Theremin

The prototype turns hand movements near two antennas into pitch and volume.
External oscillators convert the antenna response into frequency signals. An
ESP32/WROOM-32 measures those signals, generates sine-wave audio, and hosts a
Wi-Fi dashboard for live readings and calibration.

## System overview

```{mermaid}
flowchart TB
    Pitch[Pitch antenna and oscillator] --> ESP[ESP32 measurement and synthesis]
    Volume[Volume antenna and oscillator] --> ESP
    ESP --> Filter[DAC output filter and input trim]
    Filter --> Amplifier[PAM8302A amplifier]
    Amplifier --> Speaker[Speaker]
    Browser[Phone or laptop browser] <-->|Local Wi-Fi| ESP
```

| Function | Current implementation |
| --- | --- |
| Sensor inputs | Two buffered 0-3.3 V square waves, nominally 100-300 kHz |
| Frequency measurement | Two hardware pulse counters sampled every 10 ms |
| Sound generation | Sine synthesis at nominal 48 kHz through the internal 8-bit DAC |
| Control interface | Local Wi-Fi dashboard with live frequencies and signal validity |
| Calibration | Four hand positions, pitch/volume limits, response curves, preview, and flash storage |
| Firmware platform | ESP-IDF 6.1 on the original ESP32 |

The dashboard reports oscillator measurements and commanded audio pitch/gain.
Oscilloscope and acoustic measurements establish the actual output behavior.

## Find what you need

| Goal | Chapter |
| --- | --- |
| Connect the oscillators, amplifier, and speaker | [Hardware and wiring](hardware.md) |
| Understand tasks, CPU cores, peripherals, queues, and timing | [ESP32 technical architecture](architecture.md) |
| Locate source files and configuration settings | [Firmware and source files](firmware.md) |
| Capture hand positions and adjust the playing range | [Web calibration](calibration.md) |
| Build, flash, and verify the prototype | [Build and verification](testing.md) |
| Edit chapters and publish this website | [Editing and publishing](writing.md) |

The instrument dashboard is available at **http://192.168.4.1/** while connected
to the ESP32's **Theremin-Setup** network. This documentation is published through
GitHub Pages and can also be previewed on a development computer.
