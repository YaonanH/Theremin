/* Host test of the actual status formatter. No ESP32 headers or hardware needed.
 * Example: cc -Imain main/web_status.c tests/test_web_status.c -lm -o build/test_web_status
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "theremin_config.h"
#include "web_status.h"

static char json[512];

static void format(const web_status_frame_t *frame, int64_t now)
{
    int count = web_status_format(json, sizeof(json), frame, now);
    assert(count > 0 && count == (int)strlen(json));
    puts(json); /* Also parsed by the host runner to check JSON syntax. */
}

int main(void)
{
    web_status_frame_t frame = {
        {216873.0f, 200000.0f}, {true, true}, 523.25f, 0.05f, 1000000
    };

    format(NULL, 0);
    assert(strstr(json, "\"sample_available\":false,\"fresh\":false"));
    assert(strstr(json, "\"pitch\":{\"hz\":null,\"valid\":false}"));
    assert(strstr(json, "\"audio\":{\"target_hz\":null,\"gain\":0,\"enabled\":false}"));

    format(&frame, 1005000);
    assert(strstr(json, "\"fresh\":true,\"sample_age_ms\":5.0"));
    assert(strstr(json, "\"pitch\":{\"hz\":216873,\"valid\":true}"));
    assert(strstr(json, "\"volume\":{\"hz\":200000,\"valid\":true}"));
    assert(strstr(json, "\"audio\":{\"target_hz\":523.25,\"gain\":0.05,\"enabled\":true}"));

    /* Staleness must override good readings and an old nonzero audio target. */
    format(&frame, 1000000 + CONTROL_TIMEOUT_US + 1);
    assert(strstr(json, "\"fresh\":false"));
    assert(strstr(json, "\"pitch\":{\"hz\":216873,\"valid\":false}"));
    assert(strstr(json, "\"audio\":{\"target_hz\":null,\"gain\":0,\"enabled\":false}"));
    format(&frame, 1000000 + CONTROL_TIMEOUT_US);
    assert(strstr(json, "\"fresh\":true"));

    frame.sensor_hz[0] = 0.0f;
    frame.sensor_valid[0] = false;
    frame.audio_gain = 0.0f;
    format(&frame, 1001000);
    assert(strstr(json, "\"pitch\":{\"hz\":0,\"valid\":false}"));
    assert(strstr(json, "\"volume\":{\"hz\":200000,\"valid\":true}"));
    assert(strstr(json, "\"enabled\":false"));

    /* Nonfinite sensor/control values must never produce invalid JSON tokens. */
    frame.sensor_hz[0] = NAN;
    frame.sensor_hz[1] = INFINITY;
    frame.sensor_valid[0] = frame.sensor_valid[1] = true;
    frame.audio_gain = NAN;
    format(&frame, 1001000);
    assert(strstr(json, "\"pitch\":{\"hz\":null,\"valid\":false}"));
    assert(strstr(json, "\"volume\":{\"hz\":null,\"valid\":false}"));
    assert(strstr(json, "\"enabled\":false"));

    /* A timestamp in the future is not a fresh measurement. */
    format(&frame, 999999);
    assert(strstr(json, "\"fresh\":false,\"sample_age_ms\":null"));

    char small[8] = "test";
    assert(web_status_format(small, sizeof(small), &frame, 1001000) == -1);
    assert(small[0] == '\0');
    assert(web_status_format(NULL, 100, &frame, 1001000) == -1);
    assert(web_status_format(small, 0, &frame, 1001000) == -1);
    fprintf(stderr, "web_status: all checks passed\n");
    return 0;
}
