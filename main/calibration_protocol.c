#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "calibration_protocol.h"
#include "theremin_config.h"

static int hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool decode(char *text)
{
    char *out = text;
    for (char *p = text; *p; ++p) {
        int c = (unsigned char)*p;
        if (c == '%') {
            if (!p[1] || !p[2] || hex(p[1]) < 0 || hex(p[2]) < 0) return false;
            c = hex(p[1]) * 16 + hex(p[2]); p += 2;
        } else if (c == '+') c = ' ';
        if (c <= 32 || c >= 127) return false; /* Command values have no whitespace. */
        *out++ = (char)c;
    }
    *out = '\0';
    return true;
}

static bool number(const char *text, float *value)
{
    if (!text || !*text) return false;
    char *end;
    errno = 0;
    float parsed = (float)strtod(text, &end);
    if (errno || *end || !isfinite(parsed)) return false;
    *value = parsed;
    return true;
}

bool calibration_parse_form(char *body, calibration_command_t *command)
{
    const char *const keys[] = {"action", "token", "endpoint", "pitch_min_hz", "pitch_max_hz",
                               "volume_min_pct", "volume_max_pct", "pitch_curve", "volume_curve", "capture_id"};
    char *values[10] = {0};
    unsigned fields = 0;
    memset(command, 0, sizeof(*command));
    if (!body || !*body) return false;
    for (char *part = body; part;) {
        char *next = strchr(part, '&');
        if (next) *next++ = '\0';
        char *value = strchr(part, '=');
        if (!value) return false;
        *value++ = '\0';
        if (!decode(part) || !decode(value)) return false;
        int index = -1;
        for (int i = 0; i < 10; ++i) if (strcmp(part, keys[i]) == 0) index = i;
        if (index < 0 || values[index]) return false;
        values[index] = value; fields++;
        if (next && !*next) return false;
        part = next;
    }
    if (!values[0] || !values[1] || strlen(values[1]) != CAL_TOKEN_LENGTH) return false;
    for (int i = 0; i < CAL_TOKEN_LENGTH; ++i)
        if (!((values[1][i] >= '0' && values[1][i] <= '9')
              || (values[1][i] >= 'a' && values[1][i] <= 'f'))) return false;
    memcpy(command->token, values[1], sizeof(command->token));
    const char *const actions[] = {"begin", "settings", "capture", "preview", "stop_preview", "save", "cancel", "stop_capture"};
    const calibration_action_t action_ids[] = {
        CAL_BEGIN, CAL_SETTINGS, CAL_CAPTURE, CAL_PREVIEW, CAL_STOP_PREVIEW, CAL_SAVE, CAL_CANCEL, CAL_STOP_CAPTURE};
    bool found = false;
    for (int i = 0; i < 8; ++i) if (strcmp(values[0], actions[i]) == 0) {
        command->action = action_ids[i]; found = true;
    }
    if (!found) return false;
    if (command->action == CAL_STOP_CAPTURE) {
        if (fields != 3 || !values[9] || !*values[9]) return false;
        uint32_t id = 0;
        for (const char *digit = values[9]; *digit; ++digit) {
            if (*digit < '0' || *digit > '9' || id > (UINT32_MAX - (*digit - '0')) / 10u) return false;
            id = id * 10u + (*digit - '0');
        }
        if (!id) return false;
        command->capture_id = id;
        return true;
    }
    if (command->action == CAL_CAPTURE) {
        if (fields != 3 || !values[2]) return false;
        const char *const endpoints[] = {"pitch_far", "pitch_near", "volume_far", "volume_near"};
        for (int i = 0; i < 4; ++i) if (strcmp(values[2], endpoints[i]) == 0) {
            command->endpoint = i; return true;
        }
        return false;
    }
    if (command->action != CAL_SETTINGS) return fields == 2;
    if (fields != 8 || values[2]) return false;
    calibration_profile_t *p = &command->settings;
    float low, high;
    if (!number(values[3], &p->pitch_min_hz) || !number(values[4], &p->pitch_max_hz)
        || !number(values[5], &low) || !number(values[6], &high)
        || low < 0 || high > 100 || low > high || !values[7] || !values[8]) return false;
    p->gain_min = AUDIO_MAX_GAIN * (low / 100.0f);
    p->gain_max = AUDIO_MAX_GAIN * (high / 100.0f);
    if (strcmp(values[7], "musical") == 0) p->pitch_curve = 0;
    else if (strcmp(values[7], "linear") == 0) p->pitch_curve = 1;
    else return false;
    if (strcmp(values[8], "quadratic") == 0) p->volume_curve = 0;
    else if (strcmp(values[8], "linear") == 0) p->volume_curve = 1;
    else return false;
    return true; /* Semantic pitch/gain bounds are also checked by the engine. */
}

int calibration_format(char *buffer, size_t capacity, const calibration_engine_t *e,
                        const char *token, int64_t now, const char *telemetry)
{
    if (!buffer || !capacity) return -1;
    bool owner = e->token[0] && token && strcmp(token, e->token) == 0;
    const char *phase = e->endpoint < 0 ? "idle" : now < e->capture_start_us ? "countdown" : "capturing";
    int64_t remaining = e->endpoint < 0 ? 0
        : (now < e->capture_start_us ? e->capture_start_us : e->capture_start_us + CAL_CAPTURE_US) - now;
    if (remaining < 0) remaining = 0;
    calibration_result_t validation = calibration_validate(&e->draft, e->captured_mask);
    const calibration_profile_t *p = &e->draft;
    int length = snprintf(buffer, capacity,
        "{\"session_active\":%s,\"owned\":%s,\"preview\":%s,\"saving\":%s,\"saved\":%s,"
        "\"can_preview\":%s,\"captured_mask\":%u,\"valid\":%s,\"validation_message\":\"%s\","
        "\"notice\":%d,\"message\":\"%s\","
        "\"capture\":{\"id\":%u,\"endpoint\":%d,\"phase\":\"%s\",\"remaining_ms\":%.0f,"
        "\"result_endpoint\":%d,\"result\":%d,\"message\":\"%s\"},"
        "\"profile\":{\"endpoint_hz\":[%.7g,%.7g,%.7g,%.7g],"
        "\"pitch_min_hz\":%.7g,\"pitch_max_hz\":%.7g,\"volume_min_pct\":%.7g,\"volume_max_pct\":%.7g,"
        "\"pitch_curve\":\"%s\",\"volume_curve\":\"%s\",\"ceiling_gain\":%.7g},\"telemetry\":%s}",
        e->token[0] ? "true" : "false", owner ? "true" : "false", e->preview ? "true" : "false",
        e->saving ? "true" : "false", e->saved_mask == CAL_ALL_ENDPOINTS ? "true" : "false",
        THEREMIN_MODE == MODE_THEREMIN ? "true" : "false", (unsigned)e->captured_mask,
        validation == CAL_OK ? "true" : "false", calibration_message(validation),
        (int)e->notice, calibration_message(e->notice), (unsigned)e->capture_id,
        e->endpoint, phase, remaining / 1000.0, e->capture_endpoint,
        (int)e->capture_result, calibration_message(e->capture_result),
        (double)p->endpoint_hz[0], (double)p->endpoint_hz[1], (double)p->endpoint_hz[2], (double)p->endpoint_hz[3],
        (double)p->pitch_min_hz, (double)p->pitch_max_hz,
        AUDIO_MAX_GAIN > 0 ? (double)(100.0f * p->gain_min / AUDIO_MAX_GAIN) : 0.0,
        AUDIO_MAX_GAIN > 0 ? (double)(100.0f * p->gain_max / AUDIO_MAX_GAIN) : 0.0,
        p->pitch_curve == 0 ? "musical" : "linear", p->volume_curve == 0 ? "quadratic" : "linear",
        (double)AUDIO_MAX_GAIN, telemetry);
    if (length < 0 || (size_t)length >= capacity) { buffer[0] = '\0'; return -1; }
    return length;
}
