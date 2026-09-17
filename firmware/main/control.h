#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "control_logic.h"

typedef struct {
    ctl_state_t state;
    int64_t now_mono_s;
    int64_t heartbeat_mono_s;
    uint32_t runtime_today_s;       // local day once the clock is synced, since boot before that
    uint32_t starts_today;
    uint32_t runtime_boot_s;
    uint32_t starts_boot;
} control_status_t;

/* Starts the control task (sensor, thresholds, maintenance, relay). Needs NVS settings, no network. */
void control_start(void);

/* Task heartbeat within the last few read periods. */
bool control_alive(void);

void control_status(control_status_t *out);

typedef enum {
    CONTROL_OK = 0,
    CONTROL_REFUSED_OVERHEAT,       // manual stop while overheated
} control_result_t;

/* Manual override for duration_s (clamped to MANUAL_MIN_S..MANUAL_MAX_S), or back to automatic control with
 * CTL_MANUAL_NONE. Applied immediately. `who` goes to the event and notification ("192.168.11.5", "admin"). */
control_result_t control_manual(ctl_manual_t manual, uint32_t duration_s, const char *who);

/* Threshold change from the app: applied on the next decision (immediately). */
void control_settings_changed(void);
