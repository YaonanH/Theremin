#pragma once

/* Start here when debugging. Change MODE, rebuild, then flash. */
#define MODE_THEREMIN       0
#define MODE_MEASURE_ONLY   1
#define MODE_TEST_TONE      2
#define THEREMIN_MODE       MODE_THEREMIN

#define PITCH_INPUT_GPIO    34
#define VOLUME_INPUT_GPIO   35
#define CALIBRATION_GPIO    27  /* Optional button to GND; internal pull-up. */
/* The DAC driver selects GPIO25 through DAC_CHANNEL_MASK_CH0. */

/* Local dashboard. Change credentials here, then rebuild/flash. */
#define WEB_MONITOR_ENABLED 1   /* Set to 0 to compare operation without Wi-Fi. */
#define WIFI_AP_SSID        "Theremin-Setup"
#define WIFI_AP_PASSWORD    "theremin123" /* WPA2 password: 8-63 characters. */
#define WIFI_AP_CHANNEL     1

#define MEASUREMENT_US      10000
#define LOG_INTERVAL_US     250000
#define SENSOR_MIN_HZ       100000.0f
#define SENSOR_MAX_HZ       300000.0f
#define SENSOR_MARGIN_HZ    2000.0f /* Allow endpoint quantization/timing error. */
#define CONTROL_TIMEOUT_US  50000
#define FILTER_TIME_US      20000.0f

/* Fallback endpoints until a saved web calibration is available.
 * The current settings assume approaching either antenna raises its frequency.
 * The near pitch position gives high pitch; the near volume position is quiet.
 */
#define PITCH_FAR_HZ        100000.0f
#define PITCH_NEAR_HZ       300000.0f
#define VOLUME_FAR_HZ       100000.0f
#define VOLUME_NEAR_HZ      300000.0f

#define AUDIO_MIN_HZ        130.8128f /* C3 */
#define AUDIO_OCTAVES       4.0f
#define AUDIO_SAMPLE_RATE   48000
#define AUDIO_BLOCK_SAMPLES 128
#define AUDIO_MAX_GAIN      0.5f /* Conservative prototype level; trim amp too. */
#define AUDIO_SMOOTH_SAMPLES 240.0f /* Approximately 5 ms smoothing. */
#define TEST_TONE_HZ        240.0f
#define TEST_TONE_GAIN      0.01f /* Low bench level; set amplifier trim low too. */

#if THEREMIN_MODE < MODE_THEREMIN || THEREMIN_MODE > MODE_TEST_TONE
#error "Invalid THEREMIN_MODE"
#endif
