#pragma once

/* Pump decisions without ESP-IDF: temperatures in 0.1 °C, time in monotonic seconds. Host-tested (test/). */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CTL_AUTO_COLD = 0,      // off: below the stop threshold (or never above start since boot)
    CTL_AUTO_HOT,           // on: above the start threshold (hysteresis)
    CTL_MANUAL_ON,
    CTL_MANUAL_OFF,
    CTL_MAINTENANCE,        // on: anti-seize run after a long idle period
    CTL_SENSOR_FAIL,        // on: no valid reading, fail-safe
    CTL_OVERHEAT,           // on: at or above the critical threshold, overrides a manual stop
} ctl_reason_t;

typedef enum {
    CTL_MANUAL_NONE = 0,
    CTL_MANUAL_START,
    CTL_MANUAL_STOP,
} ctl_manual_t;

typedef struct {
    int16_t start_x10;
    int16_t stop_x10;
    int16_t critical_x10;
    uint32_t maintenance_interval_s;
    uint32_t maintenance_run_s;
    uint8_t sensor_fail_reads;      // consecutive bad reads before the fail-safe
} ctl_config_t;

typedef struct {
    bool on;
    ctl_reason_t reason;
    int64_t changed_s;              // when `on` last changed

    bool have_temp;                 // a valid reading since boot
    int16_t temp_x10;               // last valid reading
    int64_t temp_s;
    uint8_t failures;               // consecutive bad reads
    bool sensor_failed;
    bool overheat;
    bool hot;                       // hysteresis memory

    ctl_manual_t manual;
    int64_t manual_until_s;

    bool maintenance;
    int64_t maintenance_until_s;
    int64_t last_on_s;              // last time the pump was seen running (or boot)
} ctl_state_t;

/* What happened in one step, for events and notifications. */
typedef struct {
    bool changed;                   // pump switched; state->on / state->reason are the new values
    ctl_reason_t prev_reason;
    bool sensor_failed;
    bool sensor_recovered;
    bool overheat_started;
    bool overheat_ended;
    bool manual_expired;
    bool manual_cancelled;          // manual stop dropped by overheat
    bool maintenance_started;
} ctl_result_t;

void ctl_init(ctl_state_t *s, int64_t now_s);

/* A sensor read: valid with a temperature, or a failure. Updates failure and overheat state; call ctl_decide next. */
void ctl_sensor(ctl_state_t *s, const ctl_config_t *cfg, int64_t now_s, bool valid, int16_t temp_x10,
                ctl_result_t *res);

/* Manual override for duration_s (clamped by the caller); CTL_MANUAL_NONE returns to automatic control.
 * A manual stop is refused (false) while overheated. */
bool ctl_manual(ctl_state_t *s, ctl_manual_t manual, uint32_t duration_s, int64_t now_s);

/* Expires overrides and maintenance, starts maintenance when due and sets the pump state. */
void ctl_decide(ctl_state_t *s, const ctl_config_t *cfg, int64_t now_s, ctl_result_t *res);

const char *ctl_reason_name(ctl_reason_t reason);
