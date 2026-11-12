#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A copy of processed readings. The web server never reads the counters itself. */
typedef struct {
    float sensor_hz[2];
    bool sensor_valid[2];
    float audio_pitch_hz;
    float audio_gain;
    int64_t timestamp_us;
} web_status_frame_t;

/* Returns JSON length, or -1 on invalid/insufficient output storage.
 * Pass NULL for frame until the first measurement has been processed.
 */
int web_status_format(char *buffer, size_t capacity,
                      const web_status_frame_t *frame, int64_t now_us);
