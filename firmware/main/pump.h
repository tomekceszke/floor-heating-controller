#pragma once

#include <stdbool.h>

/* Relay output, off after init. Only the control module calls pump_set(). */
void pump_init(void);
void pump_set(bool on);
bool pump_is_on(void);
