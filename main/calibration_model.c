#include <float.h>
#include <math.h>
#include <string.h>
#include "calibration_model.h"
#include "theremin_config.h"

static bool sensor_range(float hz)
{
    return isfinite(hz) && hz >= SENSOR_MIN_HZ - SENSOR_MARGIN_HZ
        && hz <= SENSOR_MAX_HZ + SENSOR_MARGIN_HZ;
}

static bool controls_valid(const calibration_profile_t *p)
{
    return isfinite(p->pitch_min_hz) && isfinite(p->pitch_max_hz)
        && p->pitch_min_hz >= 20.0f && p->pitch_max_hz <= 5000.0f
        && p->pitch_max_hz > p->pitch_min_hz
        && isfinite(p->gain_min) && isfinite(p->gain_max)
        && p->gain_min >= 0.0f && p->gain_max >= p->gain_min
        && p->gain_max <= AUDIO_MAX_GAIN && p->gain_max <= 1.0f
        && p->pitch_curve <= 1 && p->volume_curve <= 1;
}

void calibration_defaults(calibration_profile_t *p)
{
    memset(p, 0, sizeof(*p));
    p->endpoint_hz[0] = PITCH_FAR_HZ; p->endpoint_hz[1] = PITCH_NEAR_HZ;
    p->endpoint_hz[2] = VOLUME_FAR_HZ; p->endpoint_hz[3] = VOLUME_NEAR_HZ;
    p->pitch_min_hz = AUDIO_MIN_HZ;
    p->pitch_max_hz = AUDIO_MIN_HZ * exp2f(AUDIO_OCTAVES);
    p->gain_max = AUDIO_MAX_GAIN;
}

calibration_result_t calibration_validate(const calibration_profile_t *p, uint32_t mask)
{
    if (!controls_valid(p)) return CAL_INVALID_SETTINGS;
    if (mask != CAL_ALL_ENDPOINTS) return CAL_NEED_ENDPOINTS;
    for (int i = 0; i < CAL_ENDPOINTS; ++i) {
        if (!sensor_range(p->endpoint_hz[i]) || !isfinite(p->spread_hz[i])
            || p->spread_hz[i] < 0 || p->spread_hz[i] > 1000.0f) return CAL_BAD_SIGNAL;
    }
    for (int i = 0; i < CAL_ENDPOINTS; i += 2) {
        float required = fmaxf(1000.0f, 4.0f * fmaxf(p->spread_hz[i], p->spread_hz[i + 1]));
        if (fabsf(p->endpoint_hz[i + 1] - p->endpoint_hz[i]) < required) return CAL_BAD_SPAN;
    }
    return CAL_OK;
}

static float position(float hz, float far_hz, float near_hz)
{
    return fminf(1.0f, fmaxf(0.0f, (hz - far_hz) / (near_hz - far_hz)));
}

bool calibration_map_profile(const calibration_profile_t *p, float pitch_hz, float volume_hz,
                              float *audio_hz, float *gain)
{
    if (!isfinite(pitch_hz) || !isfinite(volume_hz)
        || calibration_validate(p, CAL_ALL_ENDPOINTS) != CAL_OK) return false;
    float pitch = position(pitch_hz, p->endpoint_hz[0], p->endpoint_hz[1]);
    float loudness = 1.0f - position(volume_hz, p->endpoint_hz[2], p->endpoint_hz[3]);
    *audio_hz = p->pitch_curve == 0
        ? p->pitch_min_hz * powf(p->pitch_max_hz / p->pitch_min_hz, pitch)
        : p->pitch_min_hz + (p->pitch_max_hz - p->pitch_min_hz) * pitch;
    if (p->volume_curve == 0) loudness *= loudness;
    *gain = fminf(AUDIO_MAX_GAIN, p->gain_min + (p->gain_max - p->gain_min) * loudness);
    return true;
}

void calibration_engine_init(calibration_engine_t *e, const calibration_profile_t *stored)
{
    memset(e, 0, sizeof(*e));
    calibration_defaults(&e->saved);
    if (stored && calibration_validate(stored, CAL_ALL_ENDPOINTS) == CAL_OK) {
        e->saved = *stored; e->saved_mask = CAL_ALL_ENDPOINTS;
    }
    e->draft = e->saved; e->captured_mask = e->saved_mask; e->endpoint = -1;
    e->capture_endpoint = -1;
}

static void cancel(calibration_engine_t *e)
{
    if (e->endpoint >= 0) e->capture_result = CAL_CAPTURE_CANCELLED;
    e->draft = e->saved; e->captured_mask = e->saved_mask;
    e->token[0] = '\0'; e->endpoint = -1; e->preview = false;
}

static bool token_valid(const char *token)
{
    for (int i = 0; i < CAL_TOKEN_LENGTH; ++i) {
        char c = token[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return token[CAL_TOKEN_LENGTH] == '\0';
}

void calibration_engine_tick(calibration_engine_t *e, int64_t now)
{
    if (e->saving) {
        if (now - e->save_start_us > CAL_SAVE_TIMEOUT_US) {
            e->saving = false; e->notice = CAL_QUIET_TIMEOUT;
        }
        return;
    }
    if (e->token[0] && now - e->touched_us > CAL_SESSION_US) {
        cancel(e); e->notice = CAL_EXPIRED; return;
    }
    if (e->endpoint < 0 || now < e->capture_start_us + CAL_CAPTURE_US) return;
    if (e->bad_sample || e->samples < 75) e->notice = CAL_BAD_SIGNAL;
    else if (e->maximum - e->minimum > 1000.0f) e->notice = CAL_UNSTABLE;
    else {
        e->draft.endpoint_hz[e->endpoint] = (float)(e->sum / e->samples);
        e->draft.spread_hz[e->endpoint] = e->maximum - e->minimum;
        e->captured_mask |= 1u << e->endpoint;
        e->notice = CAL_CAPTURED;
    }
    e->capture_result = e->notice;
    e->endpoint = -1;
}

calibration_result_t calibration_engine_command(calibration_engine_t *e,
                                               const calibration_command_t *c, int64_t now)
{
    calibration_engine_tick(e, now);
    bool owner = e->token[0] && strcmp(e->token, c->token) == 0;
    if (c->action == CAL_QUERY) { if (owner) e->touched_us = now; return CAL_OK; }
    if (c->action == CAL_BEGIN) {
        if (!token_valid(c->token)) return CAL_NO_SESSION;
        if (e->token[0] && !owner) return CAL_BUSY;
        if (!owner) {
            e->draft = e->saved; e->captured_mask = e->saved_mask;
            memcpy(e->token, c->token, sizeof(e->token)); e->notice = CAL_OK;
        }
        e->touched_us = now; return CAL_OK;
    }
    if (!owner) return CAL_NO_SESSION;
    e->touched_us = now;
    if (e->saving) return CAL_BUSY;
    if (c->action == CAL_CANCEL) { cancel(e); e->notice = CAL_OK; return CAL_OK; }
    if (c->action == CAL_STOP_CAPTURE) {
        if (e->endpoint >= 0 && c->capture_id == e->capture_id) {
            e->endpoint = -1;
            e->capture_result = e->notice = CAL_CAPTURE_CANCELLED;
        }
        return CAL_OK;
    }
    if (e->endpoint >= 0) return CAL_BUSY;
    calibration_result_t result = CAL_OK;
    switch (c->action) {
    case CAL_SETTINGS: {
        calibration_profile_t next = e->draft;
        next.pitch_min_hz = c->settings.pitch_min_hz; next.pitch_max_hz = c->settings.pitch_max_hz;
        next.gain_min = c->settings.gain_min; next.gain_max = c->settings.gain_max;
        next.pitch_curve = c->settings.pitch_curve; next.volume_curve = c->settings.volume_curve;
        if (!controls_valid(&next)) return CAL_INVALID_SETTINGS;
        e->draft = next; e->preview = false; break;
    }
    case CAL_CAPTURE:
        if (c->endpoint < 0 || c->endpoint >= CAL_ENDPOINTS) return CAL_INVALID_SETTINGS;
        e->preview = false; e->endpoint = c->endpoint;
        if (++e->capture_id == 0) ++e->capture_id;
        e->capture_endpoint = c->endpoint;
        e->capture_result = CAL_OK;
        e->capture_start_us = now + CAL_COUNTDOWN_US;
        e->sum = 0; e->samples = 0; e->minimum = FLT_MAX; e->maximum = 0;
        e->bad_sample = false; e->last_sample_us = -1; break;
    case CAL_PREVIEW:
    case CAL_SAVE:
        result = calibration_validate(&e->draft, e->captured_mask);
        if (result != CAL_OK) return result;
        e->preview = c->action == CAL_PREVIEW;
        if (c->action == CAL_SAVE) { e->saving = true; e->save_start_us = now; }
        break;
    case CAL_STOP_PREVIEW: e->preview = false; break;
    default: return CAL_INVALID_SETTINGS;
    }
    e->notice = CAL_OK; return result;
}

void calibration_engine_sample(calibration_engine_t *e, const float hz[2], const bool valid[2],
                               int64_t sample_us, int64_t now)
{
    calibration_engine_tick(e, now);
    if (e->endpoint < 0 || now < e->capture_start_us || sample_us < e->capture_start_us
        || sample_us >= e->capture_start_us + CAL_CAPTURE_US || sample_us <= e->last_sample_us) return;
    e->last_sample_us = sample_us;
    int input = e->endpoint / 2;
    if (!valid[input] || !sensor_range(hz[input]) || now < sample_us
        || now - sample_us > CONTROL_TIMEOUT_US) { e->bad_sample = true; return; }
    e->sum += hz[input]; e->minimum = fminf(e->minimum, hz[input]);
    e->maximum = fmaxf(e->maximum, hz[input]); e->samples++;
}

void calibration_engine_saved(calibration_engine_t *e, bool success)
{
    if (!e->saving) return;
    if (success) { e->saved = e->draft; e->saved_mask = CAL_ALL_ENDPOINTS; }
    e->saving = false; e->preview = false; e->notice = success ? CAL_SAVED : CAL_SAVE_FAILED;
}

const char *calibration_message(calibration_result_t result)
{
    switch (result) {
    case CAL_OK: return "Ready. Capture your hand positions, then preview or save.";
    case CAL_BUSY: return "Calibration is busy. Wait for the current operation to finish.";
    case CAL_NO_SESSION: return "This calibration session has ended. Start again.";
    case CAL_INVALID_SETTINGS: return "Check the pitch range, volume limits, and curve choices.";
    case CAL_NEED_ENDPOINTS: return "Capture all four hand positions before previewing or saving.";
    case CAL_BAD_SPAN: return "Near and far readings are too close or noisy. Recapture a wider hand range.";
    case CAL_BAD_SIGNAL: return "Capture failed: the selected antenna signal was missing or interrupted. Retry.";
    case CAL_UNSTABLE: return "Capture failed: the reading changed too much. Hold your hand still and retry.";
    case CAL_EXPIRED: return "Connection expired. The saved settings have been restored.";
    case CAL_SAVE_FAILED: return "Could not save. The previous saved settings are still active. Retry.";
    case CAL_SAVED: return "Calibration saved. It will be restored after restarting the instrument.";
    case CAL_CAPTURED: return "Hand position captured.";
    case CAL_QUIET_TIMEOUT: return "Saving stopped because the audio task did not become ready. Retry.";
    case CAL_COMMAND_TIMEOUT: return "The instrument did not process the request in time. Check the connection.";
    case CAL_CAPTURE_CANCELLED: return "Capture cancelled. The previous hand position was retained.";
    default: return "Calibration unavailable.";
    }
}
