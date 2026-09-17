#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HISTORY_NO_READING INT16_MIN

/* One sample per HISTORY_PERIOD_S for the last 24 h, RAM only (lost on reboot). */
void history_add(bool valid, int16_t temp_x10, bool pump_on);

/* Oldest first into temps[max] and pump[max]; returns the count and the monotonic time of the newest sample. */
size_t history_copy(int16_t *temps, bool *pump, size_t max, int64_t *newest_mono_s);
