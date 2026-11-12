#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/dac_continuous.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "calibration.h"
#include "theremin_config.h"
#include "web_monitor.h"

#if !CONFIG_IDF_TARGET_ESP32
#error "This prototype needs the original ESP32 (WROOM-32), with DAC on GPIO25."
#endif
#if !CONFIG_DAC_DMA_AUTO_16BIT_ALIGN
#error "Enable CONFIG_DAC_DMA_AUTO_16BIT_ALIGN for the uint8_t audio samples."
#endif

static const char *TAG = "theremin";
#define TWO_PI 6.28318530718f
#define PCNT_LIMIT 30000
#define SINE_TABLE_SIZE 256
#define AUDIO_DMA_DESCRIPTORS 4

typedef struct {
    pcnt_unit_handle_t unit;
    int previous_count;
    int64_t previous_us;
} frequency_input_t;

typedef struct {
    int pulses[2];          /* [0] pitch, [1] volume; rising edges only. */
    int64_t elapsed_us[2];  /* Actual time between each counter's snapshots. */
    int64_t timestamp_us;
} sensor_frame_t;

typedef struct {
    float pitch_hz;
    float gain;
    int64_t timestamp_us;
    uint32_t save_id; /* Nonzero while preparing a quiet flash-write window. */
} audio_control_t;

static frequency_input_t s_inputs[2];
static QueueHandle_t s_measurements;
static QueueHandle_t s_audio_controls;
static QueueHandle_t s_audio_quiet;
static dac_continuous_handle_t s_dac;
static float s_sine[SINE_TABLE_SIZE];

/* Read-time midpoint reduces software timestamp bias. Not a hardware latch. */
static int64_t read_counter(frequency_input_t *input, int *count)
{
    int64_t before = esp_timer_get_time();
    ESP_ERROR_CHECK(pcnt_unit_get_count(input->unit, count));
    return (before + esp_timer_get_time()) / 2;
}

static void init_frequency_input(frequency_input_t *input, int gpio)
{
    const gpio_config_t pin = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, /* GPIO34/35 have no internal pulls. */
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&pin));
    const pcnt_unit_config_t unit_config = {
        .low_limit = -PCNT_LIMIT,
        .high_limit = PCNT_LIMIT,
        .flags.accum_count = true,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &input->unit));
    const pcnt_chan_config_t channel_config = {
        .edge_gpio_num = gpio,
        .level_gpio_num = -1,
    };
    pcnt_channel_handle_t channel;
    ESP_ERROR_CHECK(pcnt_new_channel(input->unit, &channel_config, &channel));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(channel,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(channel,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_KEEP));
    /* At 300 kHz, a 50%-duty pulse is ~1667 ns; 100 ns preserves valid pulses. */
    const pcnt_glitch_filter_config_t filter = {.max_glitch_ns = 100};
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(input->unit, &filter));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(input->unit, PCNT_LIMIT));
    ESP_ERROR_CHECK(pcnt_unit_add_watch_point(input->unit, -PCNT_LIMIT));
    ESP_ERROR_CHECK(pcnt_unit_enable(input->unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(input->unit));
    ESP_ERROR_CHECK(pcnt_unit_start(input->unit));
    input->previous_us = read_counter(input, &input->previous_count);
}

/* esp_timer TASK callback: only counter snapshots and one queue overwrite.
 * It is not a GPIO ISR and runs regardless of the FreeRTOS tick setting.
 */
static void measurement_tick(void *arg)
{
    (void)arg;
    sensor_frame_t frame = {0};
    for (int i = 0; i < 2; ++i) {
        frequency_input_t *input = &s_inputs[i];
        int count;
        int64_t now = read_counter(input, &count);
        frame.pulses[i] = count - input->previous_count;
        frame.elapsed_us[i] = now - input->previous_us;
        input->previous_count = count;
        input->previous_us = now;

        /* Rare rebase avoids overflowing the driver's signed 32-bit accumulator.
         * Only this rebase pauses counting briefly; ordinary windows never do.
         */
        if (count >= 1000000000) {
            ESP_ERROR_CHECK(pcnt_unit_stop(input->unit));
            ESP_ERROR_CHECK(pcnt_unit_clear_count(input->unit));
            ESP_ERROR_CHECK(pcnt_unit_start(input->unit));
            input->previous_us = read_counter(input, &input->previous_count);
        }
    }
    frame.timestamp_us = esp_timer_get_time();
    xQueueOverwrite(s_measurements, &frame); /* Latest frame wins. */
}

static void init_audio(void)
{
    for (int i = 0; i < SINE_TABLE_SIZE; ++i) {
        s_sine[i] = sinf(TWO_PI * i / SINE_TABLE_SIZE);
    }
    const dac_continuous_config_t config = {
        .chan_mask = DAC_CHANNEL_MASK_CH0, /* GPIO25, not GPIO26. */
        .desc_num = AUDIO_DMA_DESCRIPTORS,
        .buf_size = AUDIO_BLOCK_SAMPLES * 2, /* Hardware uses 16-bit slots. */
        .freq_hz = AUDIO_SAMPLE_RATE,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_APLL,
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };
    ESP_ERROR_CHECK(dac_continuous_new_channels(&config, &s_dac));
    ESP_ERROR_CHECK(dac_continuous_enable(s_dac));
}

static void audio_task(void *arg)
{
    (void)arg;
    uint8_t samples[AUDIO_BLOCK_SAMPLES];
    audio_control_t target = {
        .pitch_hz = THEREMIN_MODE == MODE_TEST_TONE ? TEST_TONE_HZ : AUDIO_MIN_HZ,
        .gain = 0.0f,
    };
    float pitch = target.pitch_hz;
    float gain = 0.0f;
    float phase = 0.0f;
    uint32_t quiet_id = 0;
    unsigned quiet_blocks = 0;
    while (true) {
        xQueuePeek(s_audio_controls, &target, 0);
        if (target.save_id != quiet_id || target.save_id == 0) {
            quiet_id = target.save_id;
            quiet_blocks = 0;
        }
        float target_gain = target.gain;
        if (esp_timer_get_time() - target.timestamp_us > CONTROL_TIMEOUT_US) {
            target_gain = 0.0f; /* Fade to midpoint if control processing stalls. */
        }
        bool silent_block = true;
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
            pitch += (target.pitch_hz - pitch) / AUDIO_SMOOTH_SAMPLES;
            gain += (target_gain - gain) / AUDIO_SMOOTH_SAMPLES;
            samples[i] = (uint8_t)(128.0f + 127.0f * gain
                         * s_sine[(unsigned)(phase * SINE_TABLE_SIZE)] + 0.5f);
            if (samples[i] != 128) silent_block = false;
            phase += pitch / AUDIO_SAMPLE_RATE;
            if (phase >= 1.0f) {
                phase -= 1.0f; /* Keep phase continuous across control changes. */
            }
        }
        size_t loaded;
        ESP_ERROR_CHECK(dac_continuous_write(s_dac, samples, sizeof(samples), &loaded,
                                             target.save_id ? -1 : 100));
        ESP_ERROR_CHECK(loaded == sizeof(samples) ? ESP_OK : ESP_FAIL);
        if (quiet_id && silent_block) {
            if (quiet_blocks < AUDIO_DMA_DESCRIPTORS + 2) quiet_blocks++;
            if (quiet_blocks >= AUDIO_DMA_DESCRIPTORS + 2) xQueueOverwrite(s_audio_quiet, &quiet_id);
        } else quiet_blocks = 0;
        /* The blocking DMA write paces this task. Do not insert vTaskDelay(). */
    }
}

static bool frequency_valid(float hz)
{
    return isfinite(hz) && hz >= SENSOR_MIN_HZ - SENSOR_MARGIN_HZ
           && hz <= SENSOR_MAX_HZ + SENSOR_MARGIN_HZ;
}

void app_main(void)
{
    ESP_ERROR_CHECK(calibration_init());
    /* Wi-Fi initialization can save RF calibration to flash. Do it before audio. */
    if (WEB_MONITOR_ENABLED) {
        esp_err_t web_error = web_monitor_start();
        if (web_error != ESP_OK) {
            ESP_LOGW(TAG, "Dashboard unavailable (%s); continuing with the instrument",
                     esp_err_to_name(web_error));
        }
    }
    s_measurements = xQueueCreate(1, sizeof(sensor_frame_t));
    s_audio_controls = xQueueCreate(1, sizeof(audio_control_t));
    s_audio_quiet = xQueueCreate(1, sizeof(uint32_t));
    ESP_ERROR_CHECK(s_measurements && s_audio_controls && s_audio_quiet ? ESP_OK : ESP_ERR_NO_MEM);

    const gpio_config_t button = {
        .pin_bit_mask = 1ULL << CALIBRATION_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button));
    init_frequency_input(&s_inputs[0], PITCH_INPUT_GPIO);
    init_frequency_input(&s_inputs[1], VOLUME_INPUT_GPIO);
    init_audio();
    ESP_ERROR_CHECK(xTaskCreate(audio_task, "audio", 4096, NULL, 5, NULL)
                    == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    const esp_timer_create_args_t timer_config = {
        .callback = measurement_tick,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "frequency_10ms",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_config, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, MEASUREMENT_US));

    ESP_LOGI(TAG, "GPIO%d=pitch GPIO%d=volume GPIO25=DAC GPIO%d=calibration button",
             PITCH_INPUT_GPIO, VOLUME_INPUT_GPIO, CALIBRATION_GPIO);
    ESP_LOGI(TAG, "mode=%d (0=theremin 1=measure only 2=test tone), window=%d us",
             THEREMIN_MODE, MEASUREMENT_US);
    ESP_LOGI(TAG, "Open /calibration on the dashboard to capture and save hand positions");

    float filtered[2] = {0};
    bool filter_ready[2] = {false};
    int64_t last_log_us = 0;
    int64_t button_changed_us = 0;
    int button_candidate = 1;
    int button_stable = 1;
    uint32_t save_id = 0;
    sensor_frame_t frame;
    while (true) {
        bool have_frame = xQueueReceive(s_measurements, &frame, pdMS_TO_TICKS(20)) == pdTRUE;
        bool was_saving = calibration_save_pending();
        calibration_process(esp_timer_get_time());
        if (calibration_save_pending() && !was_saving) {
            if (++save_id == 0) ++save_id;
        }
        uint32_t acknowledged;
        if (calibration_save_pending() && xQueuePeek(s_audio_quiet, &acknowledged, 0) == pdTRUE
            && acknowledged == save_id) {
            /* Do not count through cache-disabled flash writes: stop callbacks first,
             * hold DAC midpoint, commit, then start with new measurement baselines. */
            ESP_ERROR_CHECK(esp_timer_stop_blocking(timer, portMAX_DELAY));
            for (int i = 0; i < 2; ++i) ESP_ERROR_CHECK(pcnt_unit_stop(s_inputs[i].unit));
            xQueueReset(s_measurements);
            calibration_commit(); /* Reports success/failure to the web page. */
            for (int i = 0; i < 2; ++i) {
                ESP_ERROR_CHECK(pcnt_unit_clear_count(s_inputs[i].unit));
                ESP_ERROR_CHECK(pcnt_unit_start(s_inputs[i].unit));
                s_inputs[i].previous_us = read_counter(&s_inputs[i], &s_inputs[i].previous_count);
                filter_ready[i] = false;
            }
            ESP_ERROR_CHECK(esp_timer_start_periodic(timer, MEASUREMENT_US));
            continue;
        }
        if (!have_frame) continue;
        float raw[2];
        bool valid[2];
        for (int i = 0; i < 2; ++i) {
            raw[i] = frame.elapsed_us[i] > 0
                ? frame.pulses[i] * 1000000.0f / frame.elapsed_us[i] : 0.0f;
            valid[i] = frame.pulses[i] >= 0 && frequency_valid(raw[i])
                && frame.elapsed_us[i] >= MEASUREMENT_US / 2
                && frame.elapsed_us[i] <= MEASUREMENT_US * 2;
            if (valid[i]) {
                float alpha = frame.elapsed_us[i] / (FILTER_TIME_US + frame.elapsed_us[i]);
                filtered[i] = filter_ready[i]
                    ? filtered[i] + alpha * (raw[i] - filtered[i]) : raw[i];
            }
            filter_ready[i] = valid[i]; /* Re-seed after a missing/bad signal. */
        }
        calibration_observe(raw, valid, frame.timestamp_us, esp_timer_get_time());

        bool inputs_ok = valid[0] && valid[1]
            && esp_timer_get_time() - frame.timestamp_us <= CONTROL_TIMEOUT_US;
        audio_control_t control = {.pitch_hz = AUDIO_MIN_HZ, .gain = 0.0f,
                                   .timestamp_us = frame.timestamp_us};
        if (THEREMIN_MODE == MODE_TEST_TONE) {
            control.pitch_hz = TEST_TONE_HZ;
            control.gain = TEST_TONE_GAIN; /* Intentionally works without oscillators. */
        } else if (THEREMIN_MODE == MODE_THEREMIN && inputs_ok) {
            calibration_apply(filtered[0], filtered[1], &control.pitch_hz, &control.gain);
        }
        if (calibration_save_pending()) {
            control.gain = 0.0f;
            control.save_id = save_id;
        }
        xQueueOverwrite(s_audio_controls, &control);

        const web_status_frame_t web_frame = {
            .sensor_hz = {raw[0], raw[1]},
            .sensor_valid = {valid[0], valid[1]},
            .audio_pitch_hz = control.pitch_hz,
            .audio_gain = control.gain,
            .timestamp_us = frame.timestamp_us,
        };
        web_monitor_publish(&web_frame);

        /* Debounce optional button for 30 ms; calibration is performed on the web page. */
        int level = gpio_get_level(CALIBRATION_GPIO);
        if (level != button_candidate) {
            button_candidate = level;
            button_changed_us = frame.timestamp_us;
        }
        if (level != button_stable && frame.timestamp_us - button_changed_us >= 30000) {
            button_stable = level;
            if (level == 0) {
                ESP_LOGI(TAG, "Open http://192.168.4.1/calibration to calibrate");
            }
        }

        if (frame.timestamp_us - last_log_us >= LOG_INTERVAL_US) {
            ESP_LOGI(TAG, "pitch=%.0f Hz [%s] volume=%.0f Hz [%s] dt=%" PRId64 "/%" PRId64
                     " us -> synth=%s target=%.1f Hz gain=%.3f",
                     (double)raw[0], valid[0] ? "OK" : "BAD",
                     (double)raw[1], valid[1] ? "OK" : "BAD",
                     frame.elapsed_us[0], frame.elapsed_us[1],
                     control.gain > 0.0f ? "ON" : "MUTED",
                     control.gain > 0.0f ? (double)control.pitch_hz : 0.0,
                     (double)control.gain);
            last_log_us = frame.timestamp_us;
        }
    }
}
