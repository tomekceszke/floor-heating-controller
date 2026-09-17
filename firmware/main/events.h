#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "control_logic.h"

typedef enum {
    EV_PUMP = 0,        // pump switched: on, reason, prev_reason
    EV_OVERHEAT,        // on = reached, !on = over
    EV_SENSOR,          // on = failed, !on = recovered
    EV_MANUAL_END,      // override ended without switching the pump (expired / dropped by overheat)
} event_type_t;

typedef struct {
    event_type_t type;
    int64_t mono_s;
    bool on;
    uint8_t reason;         // ctl_reason_t
    uint8_t prev_reason;    // ctl_reason_t
    bool has_temp;
    int16_t temp_x10;
    char detail[24];
} event_t;

void events_init(void);

/* Never blocks: stores the event in the RAM ring and queues notifications (ntfy topic for pump changes,
 * error topic for overheat and sensor failures). Callable from the control task. */
void events_publish(const event_t *event);

/* Newest first; returns the number copied. */
size_t events_recent(event_t *out, size_t max);

/* Wall-clock time (s) of a monotonic timestamp, 0 when the clock is not synced. */
int64_t events_unix_time(int64_t mono_s);

/* Monotonic seconds since boot. */
int64_t events_mono_s(void);
