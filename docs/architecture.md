# ESP32 technical architecture

The firmware runs measurement processing, audio synthesis, and web hosting as
concurrent FreeRTOS tasks. Hardware pulse counters accumulate oscillator edges,
and DMA feeds buffered samples to the DAC. Queues carry measurements, audio
targets, browser commands, and state snapshots between tasks.

This chapter describes the implementation in `main/` and the current local
`sdkconfig` for ESP-IDF 6.1. Application settings come from
`main/theremin_config.h`. Reconfigured builds can change CPU frequency, task
affinity, and ESP-IDF defaults.

## Hardware and software overview

The external pitch and volume circuits share four IC packages powered at 5 V,
using three CD4093BE devices and one CD4070BE. The hardware chapter records the
individual gate allocation and antenna/reference/XOR signal paths. The ESP32
measurement boundary expects conditioned 0-3.3 V signals.
[Oscillator hardware and wiring](hardware.md)

| Resource | Use in this prototype |
| --- | --- |
| CPU | Original dual-core ESP32, with both cores enabled and a configured CPU frequency of 160 MHz |
| FreeRTOS | Schedules application tasks and ESP-IDF services |
| PCNT | Two independent units count rising edges on GPIO34 and GPIO35 |
| High-resolution timer | Requests counter snapshots every 10000 us |
| Internal DAC | Outputs 8-bit audio on GPIO25 through the continuous/DMA driver |
| Wi-Fi | Creates a WPA2 access point for up to two connected devices |
| Flash | Holds firmware, embedded web assets, and the saved calibration in NVS |
| RAM | Holds task stacks, queues, filter state, calibration state, and audio buffers |

```{mermaid}
flowchart TB
    Pitch[Conditioned pitch sensor signal]
    Volume[Conditioned volume sensor signal]
    Browser[Browser on phone or laptop]
    Output[GPIO25 - filter, amplifier, speaker]
    subgraph ESP32[ESP32 firmware]
        PCNT[Two hardware PCNT units]
        PCNT --> Timer[ESP timer task - counter snapshots]
        Timer --> Measurements[Measurement queue - one frame]
        Measurements --> Main[Main task - validation, filtering, calibration]
        Main --> Targets[Audio queue - latest pitch and gain]
        Targets --> Audio[Audio task - sine synthesis]
        Audio --> DMA[DAC DMA buffers]
        Main --> Status[Web status queue - latest readings]
        Status --> HTTP[HTTP server task]
        HTTP --> Commands[Calibration command queue]
        Commands --> Main
        Main --> Replies[Reply queue and calibration snapshot]
        Replies --> HTTP
        Main <-->|Load and save profile| NVS[NVS flash storage]
    end
    Pitch --> PCNT
    Volume --> PCNT
    DMA --> Output
    Browser <-->|Local Wi-Fi| HTTP
```

The two sensor paths share the snapshot and processing code. Each input keeps its
own pulse-count baseline, elapsed time, validity, and filter state.

## Tasks, priorities, and CPU cores

A FreeRTOS task has its own stack and execution state. The scheduler selects
ready tasks according to priority and core affinity. A task waiting on a queue or
DMA buffer allows other ready tasks to execute. Higher numeric priorities run
ahead of lower priorities when the scheduler can select them on that core.
[ESP-IDF FreeRTOS](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/system/freertos_idf.html)

| Execution context | Priority | Core selection | Work |
| --- | --- | --- | --- |
| ESP timer task | 22 in this ESP-IDF configuration | Core 0 | Calls `measurement_tick()` for periodic counter snapshots |
| Main task | 1 | Core 0 | Runs `app_main()`, processes frames, owns calibration, and publishes audio targets |
| Audio task | 5 | Created with `xTaskCreate()` and no explicit core affinity | Generates blocks and submits them to DAC DMA |
| HTTP server task | 2 | Starts with the HTTP driver's default of no core affinity | Serves assets and API requests |
| Wi-Fi, TCP/IP, and event services | Managed by ESP-IDF | Determined by their SDK settings | Handle radio, sockets, and system events |

The timer priority comes from `ESP_TASK_TIMER_PRIO`, defined as
`configMAX_PRIORITIES - 3`, with 25 available priority levels. The main priority
comes from `ESP_TASK_MAIN_PRIO`. The current `sdkconfig` pins the main, timer,
and Wi-Fi tasks to Core 0. Audio priority 5 and HTTP priority 2 are set directly
in the application.

ESP-IDF automatically pins a task to its current core when it uses the hardware
floating-point unit. The audio task performs `float` arithmetic, so its core is
selected when it first executes FPU operations. The same rule applies to other
tasks that execute FPU operations. [Floating-point task affinity](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/system/freertos_idf.html#floating-point-usage)

The audio task's blocking DMA write allows the main task to publish new targets.
During a calibration request, the HTTP task waits on a reply queue, allowing the
main task to process the command despite its lower priority. Timer, Wi-Fi, and
other system work also compete for CPU time.

## From oscillator edges to frequency

Each input uses one PCNT unit with rising-edge counting, a 100 ns glitch filter,
and hardware limits of -30000 and +30000. Driver accumulation and limit watch
points extend the count across hardware overflows. The peripheral counts edges
between software reads, with driver interrupts supporting overflow accounting.

Every 10 ms, `measurement_tick()` reads both accumulated counts. For each read,
`read_counter()` records the time before and after the driver call and uses their
midpoint as the timestamp. The callback calculates pulse and time differences,
then overwrites the measurement queue with one `sensor_frame_t`.

The main task calculates frequency using each input's actual elapsed time.

```text
frequency_hz = pulse_difference * 1,000,000 / elapsed_us
```

| Input frequency | Expected rising edges in a nominal 10 ms window |
| --- | --- |
| 100 kHz | 1000 |
| 200 kHz | 2000 |
| 300 kHz | 3000 |

The input reads occur sequentially and have individual timestamps. Ordinary
windows keep counting continuously. At one billion accumulated pulses, a brief
stop, clear, and restart establishes a fresh baseline before signed overflow.

The callback runs through `ESP_TIMER_TASK`. It keeps its work to counter reads,
differences, and a queue update. The current FreeRTOS tick rate is 100 Hz, while
the measurement callback is scheduled through the separate microsecond-resolution
ESP Timer service. [ESP Timer dispatch](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/system/esp_timer.html#callback-dispatch-methods)

## Validation, filtering, and mapping

The main loop receives the latest frame with a queue wait of up to 20 ms. It
also advances the calibration state machine and processes a pending browser
command on each iteration, including iterations when the frame wait expires.

An input is valid when its pulse difference is nonnegative, frequency is finite
and within 98-302 kHz, and elapsed time is within 5-20 ms. These bounds derive
from the nominal 100-300 kHz band, 2 kHz margin, and 10 ms measurement interval.
Theremin playback requires both valid inputs and a frame age at most 50 ms.

Valid sensor values pass through a first-order filter.

```text
alpha = elapsed_us / (FILTER_TIME_US + elapsed_us)
filtered = filtered + alpha * (raw - filtered)
```

`FILTER_TIME_US` is 20000. The first valid sample seeds the filter directly.
An invalid reading clears that input's filter-ready flag, and the next valid
sample seeds it again. Calibration capture uses raw frequency samples.

For either antenna, captured far and near frequencies define a normalized
position. Clamping keeps the position in the playing range.

```text
position = clamp((sensor_hz - far_hz) / (near_hz - far_hz), 0, 1)
```

Capturing the physical positions establishes direction for increasing or
decreasing oscillator frequency. The available mapping equations are shown below.

| Curve | Equation |
| --- | --- |
| Musical pitch | `min_hz * (max_hz / min_hz)^position` |
| Linear pitch | `min_hz + (max_hz - min_hz) * position` |
| Quadratic volume | `min_gain + (max_gain - min_gain) * (1 - position)^2` |
| Linear volume | `min_gain + (max_gain - min_gain) * (1 - position)` |

Pitch rises from the far to the close position. Volume falls from the far to the
close position. The saved profile supplies endpoints and limits during ordinary
play, and Preview selects the draft profile. Configuration defaults supply the
initial mapping when valid stored calibration is unavailable.

## Queue ownership and synchronization

FreeRTOS queues copy complete messages between execution contexts. The main task
owns updates to the calibration engine. The audio task owns phase and smoothed
audio state. HTTP handlers use copied telemetry and calibration snapshots.

| Queue | Capacity | Producer | Consumer or reader | Contents |
| --- | --- | --- | --- | --- |
| `s_measurements` | 1 | Timer task | Main task receives | Pulse differences, per-input elapsed times, frame timestamp |
| `s_audio_controls` | 1 | Main task | Audio task peeks | Pitch, gain, timestamp, save generation |
| `s_audio_quiet` | 1 | Audio task | Main task peeks | Save generation whose silent buffers have been submitted |
| `s_status` in `web_monitor.c` | 1 | Main task | HTTP task peeks | Raw input frequencies, validity, audio targets, timestamp |
| `s_commands` | 2 | HTTP task | Main task receives | Calibration action, request ID, and deadline |
| `s_replies` | 2 | Main task | HTTP task receives | Matching request ID and command result |
| `s_snapshot` in `calibration.c` | 1 | Main task | HTTP task peeks | Copy of the calibration engine |

The single-entry queues retain the newest published value through
`xQueueOverwrite()`. Reading with `xQueuePeek()` retains that value for later
reads. Receiving with `xQueueReceive()` removes the queued item. This keeps
status and control updates current if a reader falls behind.

Calibration HTTP handlers are serialized. A request has a 300 ms processing
deadline, and its handler waits up to 500 ms for the matching reply. IDs prevent
an earlier reply from completing a later request. Capture attempts also have
IDs so a delayed cancellation applies to its intended attempt.

## Audio synthesis and DAC DMA

The audio task generates 128 samples per block at a nominal 48000 samples per
second. Each block represents about 2.67 ms. A 256-entry sine lookup table and a
persistent phase accumulator generate the waveform.

```text
pitch += (target_pitch - pitch) / AUDIO_SMOOTH_SAMPLES
gain  += (target_gain - gain) / AUDIO_SMOOTH_SAMPLES
sample = uint8(128 + 127 * gain * sine_table[phase_index] + 0.5)
phase += pitch / AUDIO_SAMPLE_RATE
```

Phase wraps after a complete cycle and continues across block boundaries.
`AUDIO_SMOOTH_SAMPLES` is 240, giving pitch and gain smoothing a nominal 5 ms
time constant. A target change approaches its final value over successive
samples. Zero gain gives midpoint code 128. Targets older than 50 ms cause gain
to approach zero.

The DAC driver is configured for GPIO25, an APLL clock source, four DMA
descriptors, and 256 bytes per DMA buffer. On the original ESP32, the DAC digital
controller uses the internal I2S0 DMA path. Automatic alignment places each
application 8-bit sample into the high byte of a hardware 16-bit slot. These four buffers
represent about 10.67 ms of audio at the nominal rate. The blocking
`dac_continuous_write()` call paces sample production as buffer space becomes
available. [Continuous DAC output](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/peripherals/dac.html#continuous-wave-output-continuous-dma-mode)

## Timing and memory reference

| Item | Configured value or nominal duration |
| --- | --- |
| Counter snapshot interval | 10 ms |
| Sensor filter parameter | 20 ms |
| Audio target freshness limit | 50 ms |
| Audio block | 128 samples, about 2.67 ms |
| Four DMA buffers | 512 aligned samples, about 10.67 ms |
| Pitch/gain smoothing time constant | About 5 ms |
| Dashboard polling delay | 200 ms after the previous request finishes |
| Calibration polling delay | 250 ms after the previous request finishes |
| Capture countdown and sampling | 3 seconds followed by 1 second |

These durations describe individual stages. End-to-end response also depends on
task scheduling, filter settling, buffer occupancy, and the analog path. Bench
measurements establish the combined latency.

| Allocation | Size requested or fixed by the source |
| --- | --- |
| Audio task stack | 4096 bytes |
| HTTP task stack | 8192 bytes |
| Sine table | 256 floats, 1024 bytes |
| Local generated sample block | 128 bytes within the audio task stack |
| Configured DMA buffer payload | Four buffers of 256 bytes |
| Persistent calibration record | 64 bytes, including magic and format version |

Total RAM use also includes the main and timer task stacks, driver allocations,
queue storage and control structures, calibration snapshots, and network buffers.
The live HTML, CSS, and JavaScript are linked into the firmware image through
`EMBED_FILES`. HTTP handlers send those assets from the linked data.

## Web requests and calibration state

The ESP32 creates the **Theremin-Setup** access point on channel 1. The HTTP task
serves the dashboard and calibration interface at **http://192.168.4.1/**. The
server allows three client sockets and two-second receive/send waits. Its
maximum registered-handler count is 12.

| Request | Result |
| --- | --- |
| `GET /` | Dashboard assets |
| `GET /api/status` | Current raw frequencies, validity, freshness, and audio targets |
| `GET /calibration` | Calibration interface |
| `GET /api/calibration` | Calibration session, draft, capture progress, and telemetry |
| `POST /api/calibration` | Form-encoded calibration action |

Telemetry includes a device timestamp. The formatter marks readings stale after
50 ms and includes audio-enabled state from the commanded target. The browser
executes page JavaScript and requests updated values at its polling interval.

A calibration command passes from the handler to the main task. The model keeps
the saved and draft profiles, a session token, four endpoint capture flags, and
the current capture result. Owner requests refresh a 45-second session lease.
The model collects at least 75 fresh samples during the one-second capture and
rejects variation above 1000 Hz peak-to-peak. Capture progress advances as normal
measurement processing continues.

The POST handler accepts bodies smaller than 512 bytes, checks form fields, and
checks browser Origin against Host when an Origin header is present. The
calibration response uses a bounded 2048-byte JSON buffer, with a separate
512-byte telemetry buffer. These bounds keep request and response storage fixed.

## Startup and flash-save sequence

At startup, `app_main()` initializes calibration queues and loads a valid saved
profile or configuration defaults. It then starts Wi-Fi and HTTP, creates the
measurement/audio queues, configures the button and counters, starts the DAC and
audio task, and starts the periodic timer. The main loop then processes frames
and calibration commands. Starting Wi-Fi first allows its RF-calibration flash
operations to finish before audio begins.

A Save command first validates the draft. The HTTP reply acknowledges acceptance,
and polling reports the later storage outcome. The main task coordinates the
flash-write interval with the audio task.

```{mermaid}
sequenceDiagram
    participant Browser
    participant HTTP as HTTP task
    participant Main as Main task
    participant Audio as Audio task
    participant Flash as NVS flash
    Browser->>HTTP: Save calibration
    HTTP->>Main: Queue Save command
    Main-->>HTTP: Draft accepted
    HTTP-->>Browser: Request accepted
    Main->>Audio: Zero gain and save generation
    Audio->>Audio: Submit silent DMA buffers
    Audio-->>Main: Quiet acknowledgement for generation
    Main->>Main: Stop timer and counters
    Main->>Flash: Write and commit versioned profile
    Flash-->>Main: Storage result
    Main->>Main: Reset counters and filter baselines, restart timer
    Browser->>HTTP: Poll calibration status
    HTTP-->>Browser: Saved or failure result from snapshot
```

The audio task acknowledges a save generation after six fully silent blocks
have been submitted, covering the four configured descriptors with additional
blocks. The main task checks that the generation matches before stopping the
timer and counters. It commits the profile under namespace `theremin_cal`, key
`profile`, then restarts measurement with fresh baselines. A two-second timeout
ends a pending save if the audio acknowledgement never arrives.

The 64-byte record contains a magic value, version 1, and the calibration profile.
Loading checks the record length, version, and profile limits before applying it.
NVS provides the underlying flash key-value store. [NVS library](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/storage/nvs_flash.html)

## Behavior when data or services fail

| Condition | Firmware response |
| --- | --- |
| Invalid or stale sensor frame in theremin mode | Commands zero gain |
| Audio controls older than 50 ms | Audio task fades gain toward zero |
| A consumer falls behind a single-entry queue | The newest published value replaces the waiting value |
| Capture loses valid samples or exceeds the stability limit | Reports failure and retains the prior endpoint |
| Save times out or storage fails | Reports failure and retains the previous saved profile |
| Calibration owner stops making requests for 45 seconds | Discards the draft and restores saved settings |
| Saved record fails validation | Uses configured fallback calibration |
| Web startup fails | Logs the error and continues instrument initialization |

Test-tone mode uses the configured fixed audio target independently of sensor
validity. Measurement-only mode commands zero gain. The calibration guide
describes the current audio behavior while editing. [Web calibration](calibration.md)

## Implementation references

Task creation, counters, synthesis, and save coordination are implemented in
`main/main.c`. Queue exchanges and NVS records are implemented in
`main/calibration.c`. HTTP task configuration is implemented in
`main/web_monitor.c`. The file-reference chapter lists the remaining modules.
[Firmware and source files](firmware.md)

ESP-IDF documents the driver and runtime APIs used by those modules.

- [FreeRTOS task scheduling and affinity](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/system/freertos_idf.html)
- [High-resolution ESP Timer](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/system/esp_timer.html)
- [Pulse counter driver](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/peripherals/pcnt.html)
- [Continuous DAC driver](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/peripherals/dac.html)
- [HTTP server task and handlers](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/protocols/esp_http_server.html)
