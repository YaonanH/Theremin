/* Host tests: compile with calibration_model.c and calibration_protocol.c, link libm. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "calibration_model.h"
#include "calibration_protocol.h"
#include "web_status.h"
#include "theremin_config.h"

static calibration_engine_t engine;
static calibration_command_t command;
static int64_t now = 1000000;

static calibration_result_t action(calibration_action_t action_id)
{
    command.action = action_id;
    return calibration_engine_command(&engine, &command, now);
}

static void capture(int endpoint, float value, bool other_valid)
{
    command.endpoint = endpoint;
    assert(action(CAL_CAPTURE) == CAL_OK);
    assert(engine.endpoint == endpoint);
    float hz[2];
    hz[0] = value; hz[1] = value;
    bool valid[2] = {true, true};
    valid[1 - endpoint / 2] = other_valid;
    int64_t start = now + CAL_COUNTDOWN_US;
    calibration_engine_sample(&engine, hz, valid, now, now);
    assert(engine.samples == 0);
    for (int i = 0; i < 100; ++i) {
        int64_t sample = start + i * 10000;
        calibration_engine_sample(&engine, hz, valid, sample, sample);
        calibration_engine_sample(&engine, hz, valid, sample, sample); /* No duplicate counting. */
    }
    now = start + CAL_CAPTURE_US;
    calibration_engine_tick(&engine, now);
    assert(engine.samples == 100);
}

static void test_mapping(void)
{
    calibration_profile_t p;
    calibration_defaults(&p);
    float hz, gain;
    assert(calibration_map_profile(&p, PITCH_FAR_HZ, VOLUME_FAR_HZ, &hz, &gain));
    assert(fabsf(hz - AUDIO_MIN_HZ) < 0.01f && fabsf(gain - AUDIO_MAX_GAIN) < 0.00001f);
    assert(calibration_map_profile(&p, PITCH_NEAR_HZ, VOLUME_NEAR_HZ, &hz, &gain));
    assert(fabsf(hz - AUDIO_MIN_HZ * exp2f(AUDIO_OCTAVES)) < 0.01f && gain == 0);
    assert(calibration_map_profile(&p, (PITCH_FAR_HZ + PITCH_NEAR_HZ) / 2,
                                    (VOLUME_FAR_HZ + VOLUME_NEAR_HZ) / 2, &hz, &gain));
    assert(fabsf(hz - AUDIO_MIN_HZ * exp2f(AUDIO_OCTAVES / 2)) < 0.01f);
    assert(fabsf(gain - AUDIO_MAX_GAIN / 4) < 0.00001f);
    /* Reverse electrical response, while keeping the same physical hand directions. */
    p.endpoint_hz[0] = 300000; p.endpoint_hz[1] = 100000;
    p.endpoint_hz[2] = 300000; p.endpoint_hz[3] = 100000;
    p.pitch_curve = p.volume_curve = 1;
    p.gain_min = AUDIO_MAX_GAIN * 0.2f;
    assert(calibration_map_profile(&p, 200000, 200000, &hz, &gain));
    assert(fabsf(hz - (p.pitch_min_hz + p.pitch_max_hz) / 2) < 0.01f);
    assert(fabsf(gain - AUDIO_MAX_GAIN * 0.6f) < 0.00001f);
    assert(calibration_map_profile(&p, 400000, 50000, &hz, &gain));
    assert(fabsf(hz - p.pitch_min_hz) < 0.01f && fabsf(gain - p.gain_min) < 0.00001f);
    p.gain_max = AUDIO_MAX_GAIN * 2;
    assert(calibration_validate(&p, CAL_ALL_ENDPOINTS) == CAL_INVALID_SETTINGS);
    calibration_engine_init(&engine, &p); /* Reject unsafe stored profiles. */
    assert(engine.saved_mask == 0 && engine.saved.gain_max == AUDIO_MAX_GAIN);
}

static void test_session(void)
{
    calibration_engine_init(&engine, NULL);
    memset(&command, 0, sizeof(command));
    strcpy(command.token, "0123456789abcdef0123456789abcdef");
    assert(action(CAL_PREVIEW) == CAL_NO_SESSION);
    assert(action(CAL_BEGIN) == CAL_OK);
    command.endpoint = 0;
    assert(action(CAL_CAPTURE) == CAL_OK);
    assert(action(CAL_CANCEL) == CAL_OK && engine.endpoint == -1 && engine.captured_mask == 0);
    assert(action(CAL_BEGIN) == CAL_OK);
    assert(action(CAL_PREVIEW) == CAL_NEED_ENDPOINTS);
    calibration_command_t intruder = command;
    intruder.token[0] = 'f'; intruder.action = CAL_BEGIN;
    assert(calibration_engine_command(&engine, &intruder, now) == CAL_BUSY);
    intruder.action = CAL_SAVE;
    assert(calibration_engine_command(&engine, &intruder, now) == CAL_NO_SESSION);
    capture(0, 150000, false); /* The other antenna may be disconnected. */
    assert(engine.notice == CAL_CAPTURED && engine.captured_mask == 1);
    assert(engine.capture_result == CAL_CAPTURED && engine.capture_endpoint == 0 && engine.capture_id > 0);
    capture(1, 250000, true); capture(2, 270000, false); capture(3, 130000, true);
    assert(engine.captured_mask == CAL_ALL_ENDPOINTS);
    assert(action(CAL_PREVIEW) == CAL_OK && engine.preview);
    assert(engine.saved.endpoint_hz[0] == PITCH_FAR_HZ); /* Draft did not overwrite saved. */
    assert(action(CAL_SAVE) == CAL_OK && engine.saving && !engine.preview);
    assert(action(CAL_CANCEL) == CAL_BUSY); /* An acknowledged save cannot be cancelled midway. */
    calibration_engine_saved(&engine, false);
    assert(engine.notice == CAL_SAVE_FAILED && engine.saved_mask == 0);
    assert(action(CAL_SAVE) == CAL_OK);
    calibration_engine_saved(&engine, true);
    assert(engine.notice == CAL_SAVED && engine.saved_mask == CAL_ALL_ENDPOINTS);
    assert(engine.saved.endpoint_hz[0] == 150000);
    calibration_profile_t stored = engine.saved;
    calibration_engine_init(&engine, &stored);
    assert(engine.saved_mask == CAL_ALL_ENDPOINTS && engine.saved.endpoint_hz[2] == 270000);
    assert(action(CAL_BEGIN) == CAL_OK);
    command.settings = engine.draft;
    command.settings.gain_max = AUDIO_MAX_GAIN * 0.5f;
    assert(action(CAL_SETTINGS) == CAL_OK);
    assert(action(CAL_PREVIEW) == CAL_OK);
    now += CAL_SESSION_US + 1;
    calibration_engine_tick(&engine, now);
    assert(engine.notice == CAL_EXPIRED && !engine.preview && engine.token[0] == '\0');
    assert(engine.draft.gain_max == stored.gain_max);
    assert(action(CAL_BEGIN) == CAL_OK);
    assert(action(CAL_SAVE) == CAL_OK);
    now += CAL_SAVE_TIMEOUT_US + 1;
    calibration_engine_tick(&engine, now);
    assert(!engine.saving && engine.notice == CAL_QUIET_TIMEOUT);
    assert(action(CAL_CANCEL) == CAL_OK && engine.token[0] == '\0');
}

static void test_bad_capture(void)
{
    calibration_engine_init(&engine, NULL);
    assert(action(CAL_BEGIN) == CAL_OK);
    command.endpoint = 0;
    assert(action(CAL_CAPTURE) == CAL_OK);
    float hz[2] = {150000, 200000};
    bool valid[2] = {false, true};
    now += CAL_COUNTDOWN_US;
    calibration_engine_sample(&engine, hz, valid, now, now);
    now += CAL_CAPTURE_US;
    calibration_engine_tick(&engine, now);
    assert(engine.notice == CAL_BAD_SIGNAL && engine.captured_mask == 0);
    assert(action(CAL_CAPTURE) == CAL_OK);
    int64_t interrupted_start = now + CAL_COUNTDOWN_US;
    for (int i = 0; i < 100; ++i) {
        valid[0] = i != 50;
        calibration_engine_sample(&engine, hz, valid, interrupted_start + i * 10000,
                                  interrupted_start + i * 10000);
    }
    now = interrupted_start + CAL_CAPTURE_US;
    calibration_engine_tick(&engine, now);
    assert(engine.samples == 99 && engine.notice == CAL_BAD_SIGNAL && engine.captured_mask == 0);
    assert(action(CAL_CAPTURE) == CAL_OK);
    valid[0] = true;
    int64_t start = now + CAL_COUNTDOWN_US;
    for (int i = 0; i < 100; ++i) {
        hz[0] = 150000 + i * 30;
        calibration_engine_sample(&engine, hz, valid, start + i * 10000, start + i * 10000);
    }
    now = start + CAL_CAPTURE_US;
    calibration_engine_tick(&engine, now);
    assert(engine.notice == CAL_UNSTABLE && engine.captured_mask == 0);
    calibration_profile_t p;
    calibration_defaults(&p);
    p.endpoint_hz[1] = p.endpoint_hz[0] + 500;
    assert(calibration_validate(&p, CAL_ALL_ENDPOINTS) == CAL_BAD_SPAN);
    p.endpoint_hz[1] = p.endpoint_hz[0] + 2000;
    p.spread_hz[0] = 700;
    assert(calibration_validate(&p, CAL_ALL_ENDPOINTS) == CAL_BAD_SPAN);
}

static bool parse(const char *text)
{
    char body[512];
    strcpy(body, text);
    return calibration_parse_form(body, &command);
}

static void test_protocol(void)
{
    const char *token = "0123456789abcdef0123456789abcdef";
    char body[512];
    snprintf(body, sizeof(body), "action=begin&token=%s", token);
    assert(parse(body) && command.action == CAL_BEGIN);
    assert(!parse("action=begin&token=bad"));
    snprintf(body, sizeof(body), "action=begin&token=%s&action=save", token);
    assert(!parse(body));
    snprintf(body, sizeof(body), "action=capture&token=%s&endpoint=volume_near", token);
    assert(parse(body) && command.endpoint == 3);
    snprintf(body, sizeof(body), "action=stop_capture&token=%s&capture_id=42", token);
    assert(parse(body) && command.action == CAL_STOP_CAPTURE && command.capture_id == 42);
    snprintf(body, sizeof(body), "action=stop_capture&token=%s&capture_id=4294967295", token);
    assert(parse(body) && command.capture_id == UINT32_MAX);
    snprintf(body, sizeof(body), "action=stop_capture&token=%s&capture_id=4294967296", token);
    assert(!parse(body));
    snprintf(body, sizeof(body), "action=stop_capture&token=%s&capture_id=0", token);
    assert(!parse(body));
    snprintf(body, sizeof(body), "action=stop_capture&token=%s", token);
    assert(!parse(body));
    snprintf(body, sizeof(body), "action=capture&token=%s&endpoint=volume%%00_near", token);
    assert(!parse(body));
    snprintf(body, sizeof(body), "action=settings&token=%s&pitch_min_hz=130.8&pitch_max_hz=2093"
             "&volume_min_pct=0&volume_max_pct=100&pitch_curve=musical&volume_curve=quadratic", token);
    assert(parse(body) && command.settings.gain_max == AUDIO_MAX_GAIN);
    snprintf(body, sizeof(body), "action=settings&token=%s&pitch_min_hz=nan&pitch_max_hz=2093"
             "&volume_min_pct=0&volume_max_pct=100&pitch_curve=musical&volume_curve=quadratic", token);
    assert(!parse(body));
    snprintf(body, sizeof(body), "action=settings&token=%s&pitch_min_hz=130&pitch_max_hz=2093"
             "&volume_min_pct=0&volume_max_pct=101&pitch_curve=musical&volume_curve=quadratic", token);
    assert(!parse(body));
    calibration_engine_init(&engine, NULL);
    char json[2048];
    web_status_frame_t telemetry_frame = {{150000, 250000}, {true, true}, 523.25f, 0.05f, 0};
    telemetry_frame.timestamp_us = now;
    char telemetry[512];
    assert(web_status_format(telemetry, sizeof(telemetry), &telemetry_frame, now) > 0);
    int length = calibration_format(json, sizeof(json), &engine, token, now, telemetry);
    assert(length > 0 && length == (int)strlen(json));
    assert(strstr(json, "\"owned\":false") && !strstr(json, token));
    assert(strstr(json, "\"capture\":{\"id\":0"));
    puts(json); /* Parsed independently as JSON by the host checks. */
    assert(calibration_format(json, 8, &engine, token, now, telemetry) == -1 && json[0] == '\0');
}

static void test_range_bounds(void)
{
    calibration_profile_t p;
    calibration_defaults(&p);
    assert(calibration_validate(&p, CAL_ALL_ENDPOINTS) == CAL_OK);
    p.endpoint_hz[0] = 98000;
    p.endpoint_hz[1] = 302000;
    assert(calibration_validate(&p, CAL_ALL_ENDPOINTS) == CAL_OK);
    p.endpoint_hz[0] = 97999;
    assert(calibration_validate(&p, CAL_ALL_ENDPOINTS) == CAL_BAD_SIGNAL);
    p.endpoint_hz[0] = 100000;
    p.endpoint_hz[1] = 302001;
    assert(calibration_validate(&p, CAL_ALL_ENDPOINTS) == CAL_BAD_SIGNAL);
    /* A saved profile from the previous band must not bypass the new limits. */
    p.endpoint_hz[0] = 300000; p.endpoint_hz[1] = 500000;
    calibration_engine_init(&engine, &p);
    assert(engine.saved_mask == 0 && engine.saved.endpoint_hz[0] == 100000);
}

static void test_stop_capture(void)
{
    calibration_profile_t saved;
    calibration_defaults(&saved);
    calibration_engine_init(&engine, &saved);
    strcpy(command.token, "0123456789abcdef0123456789abcdef");
    assert(action(CAL_BEGIN) == CAL_OK);
    command.settings = saved;
    command.settings.gain_max = AUDIO_MAX_GAIN * 0.5f;
    assert(action(CAL_SETTINGS) == CAL_OK);
    command.endpoint = 2;
    assert(action(CAL_CAPTURE) == CAL_OK);
    uint32_t first = engine.capture_id;
    command.capture_id = first + 1;
    assert(action(CAL_STOP_CAPTURE) == CAL_OK && engine.endpoint == 2);
    command.capture_id = first;
    assert(action(CAL_STOP_CAPTURE) == CAL_OK && engine.endpoint == -1);
    assert(engine.capture_result == CAL_CAPTURE_CANCELLED);
    assert(engine.captured_mask == CAL_ALL_ENDPOINTS && engine.draft.endpoint_hz[2] == VOLUME_FAR_HZ);
    assert(engine.draft.gain_max == AUDIO_MAX_GAIN * 0.5f && engine.token[0]);
    capture(2, 220000, true);
    assert(engine.capture_id != first && engine.capture_result == CAL_CAPTURED);
    command.capture_id = engine.capture_id;
    assert(action(CAL_STOP_CAPTURE) == CAL_OK && engine.capture_result == CAL_CAPTURED);
    assert(engine.draft.endpoint_hz[2] == 220000); /* Completion won the cancellation race. */
    command.endpoint = 2;
    assert(action(CAL_CAPTURE) == CAL_OK);
    command.capture_id = first;
    assert(action(CAL_STOP_CAPTURE) == CAL_OK && engine.endpoint == 2); /* Late cancellation of an old attempt. */
    now += CAL_COUNTDOWN_US + CAL_CAPTURE_US;
    calibration_engine_tick(&engine, now); /* No samples: a failed recapture. */
    assert(engine.capture_result == CAL_BAD_SIGNAL && engine.draft.endpoint_hz[2] == 220000);
    assert(engine.captured_mask == CAL_ALL_ENDPOINTS); /* Old mask alone is not evidence of new success. */
    engine.capture_id = UINT32_MAX;
    assert(action(CAL_CAPTURE) == CAL_OK && engine.capture_id == 1);
    command.capture_id = 1;
    assert(action(CAL_STOP_CAPTURE) == CAL_OK);
}

int main(void)
{
    test_mapping(); test_session(); test_bad_capture(); test_protocol(); test_range_bounds(); test_stop_capture();
    fprintf(stderr, "calibration: all checks passed\n");
    return 0;
}
