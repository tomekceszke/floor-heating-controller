#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "hi_notify.h"
#include "hi_ntp.h"

#include "config/config.h"
#include "events.h"

static const char *TAG = "EVENTS";

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static event_t s_ring[EVENTS_RING_SIZE];
static size_t s_head;       // next write position
static size_t s_count;

void events_init(void)
{
    s_head = 0;
    s_count = 0;
}

int64_t events_mono_s(void)
{
    return esp_timer_get_time() / 1000000;
}

int64_t events_unix_time(int64_t mono_s)
{
    if (!hi_ntp_synced()) return 0;
    return (int64_t) time(NULL) - (events_mono_s() - mono_s);
}

static void temp_text(const event_t *e, char *out, size_t len)
{
    if (e->has_temp) snprintf(out, len, "%d.%d °C", e->temp_x10 / 10, abs(e->temp_x10 % 10));
    else snprintf(out, len, "no reading");
}

static void notify(const event_t *e)
{
    char temp[16];
    char msg[160];
    temp_text(e, temp, sizeof(temp));
    switch (e->type) {
        case EV_PUMP:
            switch ((ctl_reason_t) e->reason) {
                case CTL_AUTO_HOT:
                    snprintf(msg, sizeof(msg), "Water %s", temp);
                    hi_notify_event_ex("Pump started", msg, HI_NOTIFY_PRIO_DEFAULT, "arrows_counterclockwise");
                    break;
                case CTL_AUTO_COLD:
                    if (e->prev_reason == CTL_MAINTENANCE) break;       // end of a 60 s run: not worth a ping
                    snprintf(msg, sizeof(msg), "Water %s%s", temp,
                             e->prev_reason == CTL_MANUAL_ON ? ", manual run ended" : "");
                    hi_notify_event_ex("Pump stopped", msg,
                                       e->prev_reason == CTL_MANUAL_ON ? HI_NOTIFY_PRIO_LOW : HI_NOTIFY_PRIO_DEFAULT,
                                       "zzz");
                    break;
                case CTL_MANUAL_ON:
                case CTL_MANUAL_OFF:
                    snprintf(msg, sizeof(msg), "Water %s, %s", temp, e->detail);
                    hi_notify_event_ex(e->on ? "Pump started manually" : "Pump stopped manually", msg,
                                       HI_NOTIFY_PRIO_LOW, e->on ? "arrows_counterclockwise" : "zzz");
                    break;
                case CTL_MAINTENANCE:
                    snprintf(msg, sizeof(msg), "Anti-seize run for %d s after %d days idle, water %s",
                             MAINTENANCE_RUN_S, MAINTENANCE_INTERVAL_S / 86400, temp);
                    hi_notify_event_ex("Pump maintenance run", msg, HI_NOTIFY_PRIO_LOW, "wrench");
                    break;
                case CTL_SENSOR_FAIL:
                case CTL_OVERHEAT:
                    break;      // the error topic already has it
            }
            break;
        case EV_OVERHEAT:
            if (e->on) {
                ESP_LOGE(TAG, "Overheat: water %s, pump forced on", temp);    // error line -> error topic
            } else {
                snprintf(msg, sizeof(msg), "Overheat over: water %s", temp);
                ESP_LOGW(TAG, "(not error) %s", msg);
                hi_notify_error(msg);
            }
            break;
        case EV_SENSOR:
            if (e->on) {
                ESP_LOGE(TAG, "Temperature sensor failed %d times in a row, pump on as a fail-safe", SENSOR_FAIL_READS);
            } else {
                snprintf(msg, sizeof(msg), "Temperature sensor recovered: water %s", temp);
                ESP_LOGW(TAG, "(not error) %s", msg);
                hi_notify_error(msg);
            }
            break;
        case EV_MANUAL_END:
            break;
    }
}

void events_publish(const event_t *e)
{
    portENTER_CRITICAL(&s_mux);
    s_ring[s_head] = *e;
    s_head = (s_head + 1) % EVENTS_RING_SIZE;
    if (s_count < EVENTS_RING_SIZE) s_count++;
    portEXIT_CRITICAL(&s_mux);
    notify(e);
}

size_t events_recent(event_t *out, size_t max)
{
    size_t n = 0;
    portENTER_CRITICAL(&s_mux);
    for (size_t i = 0; i < s_count && n < max; i++) {
        out[n++] = s_ring[(s_head + EVENTS_RING_SIZE - 1 - i) % EVENTS_RING_SIZE];
    }
    portEXIT_CRITICAL(&s_mux);
    return n;
}
