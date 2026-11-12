#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "calibration.h"

static const char *TAG = "calibration";
static calibration_engine_t s_engine;
static QueueHandle_t s_commands, s_replies, s_snapshot;
static bool s_storage_available;

typedef struct {
    uint32_t magic, version;
    calibration_profile_t profile;
} calibration_record_t;
_Static_assert(sizeof(calibration_record_t) == 64, "Update the stored format version if the profile layout changes");
typedef struct {
    uint32_t id;
    int64_t deadline_us;
    calibration_command_t command;
} request_t;
typedef struct { uint32_t id; calibration_result_t result; } reply_t;

static esp_err_t load_profile(calibration_profile_t *profile)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open("theremin_cal", NVS_READONLY, &handle);
    if (error != ESP_OK) return error;
    calibration_record_t record;
    size_t length = sizeof(record);
    error = nvs_get_blob(handle, "profile", &record, &length);
    nvs_close(handle);
    if (error != ESP_OK) return error;
    if (length != sizeof(record) || record.magic != 0x54484341u || record.version != 1
        || calibration_validate(&record.profile, CAL_ALL_ENDPOINTS) != CAL_OK) return ESP_ERR_INVALID_VERSION;
    *profile = record.profile;
    return ESP_OK;
}

esp_err_t calibration_init(void)
{
    s_commands = xQueueCreate(2, sizeof(request_t));
    s_replies = xQueueCreate(2, sizeof(reply_t));
    s_snapshot = xQueueCreate(1, sizeof(calibration_engine_t));
    if (!s_commands || !s_replies || !s_snapshot) return ESP_ERR_NO_MEM;
    s_storage_available = nvs_flash_init() == ESP_OK;
    calibration_profile_t stored;
    esp_err_t error = s_storage_available ? load_profile(&stored) : ESP_FAIL;
    calibration_engine_init(&s_engine, error == ESP_OK ? &stored : NULL);
    xQueueOverwrite(s_snapshot, &s_engine);
    if (error == ESP_OK) ESP_LOGI(TAG, "Loaded saved calibration");
    else ESP_LOGW(TAG, "Using configured endpoints; no valid stored calibration (%s)", esp_err_to_name(error));
    return ESP_OK;
}

void calibration_process(int64_t now)
{
    calibration_engine_tick(&s_engine, now);
    request_t request;
    if (xQueueReceive(s_commands, &request, 0) == pdTRUE) {
        reply_t reply = {.id = request.id};
        reply.result = now > request.deadline_us ? CAL_COMMAND_TIMEOUT
            : calibration_engine_command(&s_engine, &request.command, now);
        xQueueOverwrite(s_snapshot, &s_engine);
        xQueueSend(s_replies, &reply, 0);
    } else xQueueOverwrite(s_snapshot, &s_engine);
}

void calibration_observe(const float hz[2], const bool valid[2], int64_t sample_us, int64_t now)
{
    calibration_engine_sample(&s_engine, hz, valid, sample_us, now);
    xQueueOverwrite(s_snapshot, &s_engine);
}

bool calibration_apply(float pitch_hz, float volume_hz, float *audio_hz, float *gain)
{
    return !s_engine.saving && calibration_map_profile(
        s_engine.preview ? &s_engine.draft : &s_engine.saved, pitch_hz, volume_hz, audio_hz, gain);
}

bool calibration_save_pending(void) { return s_engine.saving; }

esp_err_t calibration_commit(void)
{
    if (!s_engine.saving) return ESP_ERR_INVALID_STATE;
    esp_err_t error = ESP_FAIL;
    if (s_storage_available) {
        nvs_handle_t handle;
        error = nvs_open("theremin_cal", NVS_READWRITE, &handle);
        if (error == ESP_OK) {
            calibration_record_t record = {0};
            record.magic = 0x54484341u; record.version = 1; record.profile = s_engine.draft;
            error = nvs_set_blob(handle, "profile", &record, sizeof(record));
            if (error == ESP_OK) error = nvs_commit(handle);
            nvs_close(handle);
        }
    }
    calibration_engine_saved(&s_engine, error == ESP_OK);
    xQueueOverwrite(s_snapshot, &s_engine);
    if (error != ESP_OK) ESP_LOGE(TAG, "Save failed: %s", esp_err_to_name(error));
    else ESP_LOGI(TAG, "Calibration saved");
    return error;
}

calibration_result_t calibration_exchange(const calibration_command_t *command,
                                         calibration_engine_t *snapshot)
{
    static uint32_t next_id;
    request_t request = {.id = ++next_id, .deadline_us = esp_timer_get_time() + 300000,
                         .command = *command};
    /* HTTP handlers are serialized; only one exchange can be in flight. */
    xQueueReset(s_replies);
    if (xQueueSend(s_commands, &request, 0) != pdTRUE) return CAL_COMMAND_TIMEOUT;
    reply_t reply;
    bool matched = false;
    int64_t wait_until = esp_timer_get_time() + 500000;
    while (esp_timer_get_time() < wait_until) {
        if (xQueueReceive(s_replies, &reply, pdMS_TO_TICKS(50)) == pdTRUE && reply.id == request.id) {
            matched = true;
            break;
        }
    }
    if (!matched) return CAL_COMMAND_TIMEOUT;
    if (snapshot && xQueuePeek(s_snapshot, snapshot, 0) != pdTRUE) return CAL_COMMAND_TIMEOUT;
    return reply.result;
}
