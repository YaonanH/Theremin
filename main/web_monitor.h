#pragma once

#include "esp_err.h"
#include "web_status.h"

/* Call once, before starting the counters/audio (Wi-Fi startup can write flash).
 * A startup error disables the dashboard, without preventing the instrument.
 */
esp_err_t web_monitor_start(void);

/* Nonblocking latest-value copy from the control task, never from audio/timer. */
void web_monitor_publish(const web_status_frame_t *frame);
bool web_monitor_snapshot(web_status_frame_t *frame);
