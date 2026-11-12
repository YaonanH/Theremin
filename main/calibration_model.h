#pragma once
#include <stdbool.h>
#include <stdint.h>

#define CAL_ENDPOINTS 4
#define CAL_ALL_ENDPOINTS 15u
#define CAL_TOKEN_LENGTH 32
#define CAL_COUNTDOWN_US 3000000LL
#define CAL_CAPTURE_US 1000000LL
#define CAL_SESSION_US 45000000LL
#define CAL_SAVE_TIMEOUT_US 2000000LL

typedef enum {
    CAL_OK, CAL_BUSY, CAL_NO_SESSION, CAL_INVALID_SETTINGS, CAL_NEED_ENDPOINTS,
    CAL_BAD_SPAN, CAL_BAD_SIGNAL, CAL_UNSTABLE, CAL_EXPIRED, CAL_SAVE_FAILED,
    CAL_SAVED, CAL_CAPTURED, CAL_QUIET_TIMEOUT, CAL_COMMAND_TIMEOUT, CAL_CAPTURE_CANCELLED
} calibration_result_t;

typedef struct {
    float endpoint_hz[CAL_ENDPOINTS]; /* pitch far/near, volume far/near */
    float spread_hz[CAL_ENDPOINTS];
    float pitch_min_hz, pitch_max_hz;
    float gain_min, gain_max;
    uint32_t pitch_curve;  /* 0=musical (exponential Hz), 1=linear Hz */
    uint32_t volume_curve; /* 0=quadratic amplitude, 1=linear amplitude */
} calibration_profile_t;

typedef enum {
    CAL_QUERY, CAL_BEGIN, CAL_SETTINGS, CAL_CAPTURE, CAL_PREVIEW, CAL_STOP_PREVIEW,
    CAL_SAVE, CAL_CANCEL, CAL_STOP_CAPTURE
} calibration_action_t;

typedef struct {
    calibration_action_t action;
    char token[CAL_TOKEN_LENGTH + 1];
    int endpoint;
    uint32_t capture_id; /* Scope cancellation to a particular capture attempt. */
    calibration_profile_t settings; /* Only output limits/curves are used. */
} calibration_command_t;

typedef struct {
    calibration_profile_t saved, draft;
    uint32_t saved_mask, captured_mask;
    char token[CAL_TOKEN_LENGTH + 1];
    bool preview, saving;
    int endpoint; /* -1 when no capture is active */
    int64_t touched_us, capture_start_us, last_sample_us, save_start_us;
    double sum;
    float minimum, maximum;
    unsigned samples;
    bool bad_sample;
    calibration_result_t notice;
    uint32_t capture_id;
    int capture_endpoint; /* Last attempt, retained after completion. */
    calibration_result_t capture_result;
} calibration_engine_t;

void calibration_defaults(calibration_profile_t *profile);
calibration_result_t calibration_validate(const calibration_profile_t *profile, uint32_t mask);
bool calibration_map_profile(const calibration_profile_t *profile, float pitch_hz, float volume_hz,
                              float *audio_hz, float *gain);
void calibration_engine_init(calibration_engine_t *engine, const calibration_profile_t *stored);
calibration_result_t calibration_engine_command(calibration_engine_t *engine,
                                               const calibration_command_t *command, int64_t now_us);
void calibration_engine_tick(calibration_engine_t *engine, int64_t now_us);
void calibration_engine_sample(calibration_engine_t *engine, const float hz[2], const bool valid[2],
                               int64_t sample_us, int64_t now_us);
void calibration_engine_saved(calibration_engine_t *engine, bool success);
const char *calibration_message(calibration_result_t result);
