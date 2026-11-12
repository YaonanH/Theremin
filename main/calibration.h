#pragma once

#include "esp_err.h"
#include "calibration_model.h"

esp_err_t calibration_init(void); /* Before Wi-Fi/audio; main task owns all state. */
void calibration_process(int64_t now_us);
void calibration_observe(const float hz[2], const bool valid[2], int64_t sample_us, int64_t now_us);
bool calibration_apply(float pitch_hz, float volume_hz, float *audio_hz, float *gain);
bool calibration_save_pending(void);
/* Only after audio is quiet and the measurement timer/counters have stopped. */
esp_err_t calibration_commit(void);
/* HTTP task only; one outstanding exchange. Never called from audio/timer. */
calibration_result_t calibration_exchange(const calibration_command_t *command,
                                         calibration_engine_t *snapshot);
