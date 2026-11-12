#pragma once
#include <stddef.h>
#include "calibration_model.h"

/* Strict URL-encoded command parser; modifies the supplied bounded body. */
bool calibration_parse_form(char *body, calibration_command_t *command);
int calibration_format(char *buffer, size_t capacity, const calibration_engine_t *engine,
                        const char *token, int64_t now_us, const char *telemetry_json);
