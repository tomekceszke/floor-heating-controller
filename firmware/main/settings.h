#pragma once

#include <stdint.h>
#include "esp_err.h"

typedef struct {
    int16_t start_x10;      // pump on above
    int16_t stop_x10;       // pump off below
    int16_t critical_x10;   // pump forced on and alert at or above
} settings_t;

/* Loads from NVS (defaults when missing or invalid). Never fails: control must start either way. */
void settings_init(void);

/* Consistent copy, safe from any task. */
void settings_get(settings_t *out);

/* Clamps (see config.h), applies immediately and persists. The clamped values are written back. */
esp_err_t settings_set(settings_t *in_out);
