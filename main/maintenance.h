#pragma once

#include <stdbool.h>
#include <time.h>

/**
 * Call once per main_loop() tick, after normal pump control logic.
 *
 * pump_was_active  - true if pump is currently running this tick
 * curr_temp        - current water temperature in °C
 *
 * Starts a 1-minute maintenance run when pump has been idle for
 * MAINTENANCE_INTERVAL_S and temp is safely below PUMP_STOP_TEMP.
 * Non-blocking: always returns in microseconds.
 */
void maintenance_check(bool pump_was_active, float curr_temp);

/** True while a maintenance run is in progress. */
bool is_maintenance_running(void);

/**
 * Wall-clock time of the last pump start (any reason: normal or maintenance).
 * Returns 0 if pump has not started since boot.
 */
time_t maintenance_get_last_pump_started_at(void);
