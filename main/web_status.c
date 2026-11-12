#include <math.h>
#include <stdio.h>
#include "theremin_config.h"
#include "web_status.h"

static void json_number(char *buffer, size_t capacity, bool available, float value)
{
    if (available && isfinite(value) && value >= 0.0f) {
        snprintf(buffer, capacity, "%.7g", (double)value);
    } else {
        snprintf(buffer, capacity, "null");
    }
}

int web_status_format(char *buffer, size_t capacity,
                      const web_status_frame_t *frame, int64_t now_us)
{
    if (!buffer || capacity == 0) {
        return -1;
    }
    buffer[0] = '\0';
    const bool have_frame = frame != NULL;
    const bool valid_time = have_frame && frame->timestamp_us >= 0
        && now_us >= frame->timestamp_us;
    const int64_t age_us = valid_time ? now_us - frame->timestamp_us : 0;
    const bool fresh = valid_time && age_us <= CONTROL_TIMEOUT_US;
    const bool pitch_valid = fresh && frame->sensor_valid[0]
        && isfinite(frame->sensor_hz[0]) && frame->sensor_hz[0] > 0.0f;
    const bool volume_valid = fresh && frame->sensor_valid[1]
        && isfinite(frame->sensor_hz[1]) && frame->sensor_hz[1] > 0.0f;
    const bool audio_enabled = fresh && isfinite(frame->audio_gain)
        && frame->audio_gain > 0.0f && frame->audio_gain <= 1.0f
        && isfinite(frame->audio_pitch_hz) && frame->audio_pitch_hz > 0.0f;

    char pitch[32], volume[32], audio[32], age[32];
    json_number(pitch, sizeof(pitch), have_frame, have_frame ? frame->sensor_hz[0] : 0);
    json_number(volume, sizeof(volume), have_frame, have_frame ? frame->sensor_hz[1] : 0);
    json_number(audio, sizeof(audio), audio_enabled, audio_enabled ? frame->audio_pitch_hz : 0);
    if (valid_time) {
        snprintf(age, sizeof(age), "%.1f", age_us / 1000.0);
    } else {
        snprintf(age, sizeof(age), "null");
    }
    const char *const modes[] = {"theremin", "measure_only", "test_tone"};
    int length = snprintf(buffer, capacity,
        "{\"mode\":\"%s\",\"sample_available\":%s,\"fresh\":%s,\"sample_age_ms\":%s,"
        "\"pitch\":{\"hz\":%s,\"valid\":%s},"
        "\"volume\":{\"hz\":%s,\"valid\":%s},"
        "\"audio\":{\"target_hz\":%s,\"gain\":%.7g,\"enabled\":%s}}",
        modes[THEREMIN_MODE], have_frame ? "true" : "false", fresh ? "true" : "false", age,
        pitch, pitch_valid ? "true" : "false", volume, volume_valid ? "true" : "false",
        audio, audio_enabled ? (double)frame->audio_gain : 0.0, audio_enabled ? "true" : "false");
    if (length < 0 || (size_t)length >= capacity) {
        buffer[0] = '\0';
        return -1;
    }
    return length;
}
