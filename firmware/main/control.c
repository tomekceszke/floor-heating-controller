#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"

#include "hi_ntp.h"

#include "config/config.h"
#include "control.h"
#include "events.h"
#include "history.h"
#include "pump.h"
#include "settings.h"
#include "temp_sensor.h"

static const char *TAG = "CONTROL";

static SemaphoreHandle_t s_lock;        // guards everything below; never held while reading the sensor
static TaskHandle_t s_task;
static ctl_state_t s_state;
static int64_t s_heartbeat_s;
static int64_t s_last_tick_s;
static int s_today_yday = -1;
static uint32_t s_runtime_today_s;
static uint32_t s_starts_today;
static uint32_t s_runtime_boot_s;
static uint32_t s_starts_boot;

static void config_from_settings(ctl_config_t *cfg)
{
    settings_t s;
    settings_get(&s);
    *cfg = (ctl_config_t) {
        .start_x10 = s.start_x10,
        .stop_x10 = s.stop_x10,
        .critical_x10 = s.critical_x10,
        .maintenance_interval_s = MAINTENANCE_INTERVAL_S,
        .maintenance_run_s = MAINTENANCE_RUN_S,
        .sensor_fail_reads = SENSOR_FAIL_READS,
    };
}

static event_t make_event(event_type_t type, int64_t now_s)
{
    event_t e = {
        .type = type,
        .mono_s = now_s,
        .on = s_state.on,
        .reason = (uint8_t) s_state.reason,
        .has_temp = s_state.have_temp,
        .temp_x10 = s_state.temp_x10,
    };
    return e;
}

static void count_runtime_locked(int64_t now_s)
{
    int yday = -1;
    if (hi_ntp_synced()) {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        yday = t.tm_yday;
    }
    if (yday >= 0 && yday != s_today_yday) {
        if (s_today_yday >= 0) {        // keep pre-sync runtime in the first synced day
            s_runtime_today_s = 0;
            s_starts_today = 0;
        }
        s_today_yday = yday;
    }
    if (s_state.on && s_last_tick_s > 0) {
        uint32_t dt = (uint32_t) (now_s - s_last_tick_s);
        s_runtime_today_s += dt;
        s_runtime_boot_s += dt;
    }
    s_last_tick_s = now_s;
}

/* Decision + relay + events. Caller holds s_lock. detail: text for a manual change. */
static void decide_locked(int64_t now_s, ctl_result_t *r, const char *detail)
{
    ctl_config_t cfg;
    config_from_settings(&cfg);
    count_runtime_locked(now_s);
    ctl_decide(&s_state, &cfg, now_s, r);
    if (r->changed) {
        pump_set(s_state.on);
        if (s_state.on) {
            s_starts_today++;
            s_starts_boot++;
        }
        ESP_LOGW(TAG, "(not error) Pump %s: %s (was %s)", s_state.on ? "ON" : "OFF", ctl_reason_name(s_state.reason),
                 ctl_reason_name(r->prev_reason));
    }

    if (r->sensor_failed || r->sensor_recovered) {
        event_t e = make_event(EV_SENSOR, now_s);
        e.on = r->sensor_failed;
        events_publish(&e);
    }
    if (r->overheat_started || r->overheat_ended) {
        event_t e = make_event(EV_OVERHEAT, now_s);
        e.on = r->overheat_started;
        events_publish(&e);
    }
    if (r->changed) {
        event_t e = make_event(EV_PUMP, now_s);
        e.prev_reason = (uint8_t) r->prev_reason;
        if (detail) snprintf(e.detail, sizeof(e.detail), "%s", detail);
        events_publish(&e);
    } else if (r->manual_expired || r->manual_cancelled) {
        event_t e = make_event(EV_MANUAL_END, now_s);
        e.prev_reason = (uint8_t) r->prev_reason;
        snprintf(e.detail, sizeof(e.detail), "%s", r->manual_cancelled ? "overheat" : "expired");
        events_publish(&e);
    }
}

static void control_task(void *arg)
{
    esp_task_wdt_add(NULL);
    int64_t next_read_s = 0;
    int64_t next_history_s = 0;
    for (;;) {
        int64_t now_s = events_mono_s();
        bool read = now_s >= next_read_s;
        bool valid = false;
        int16_t temp_x10 = 0;
        if (read) {
            next_read_s = now_s + CONTROL_READ_PERIOD_S;
            valid = temp_sensor_read(&temp_x10);     // ~750 ms, outside the lock
            now_s = events_mono_s();
        }

        ctl_result_t r;
        memset(&r, 0, sizeof(r));
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (read) {
            ctl_config_t cfg;
            config_from_settings(&cfg);
            ctl_sensor(&s_state, &cfg, now_s, valid, temp_x10, &r);
        }
        decide_locked(now_s, &r, NULL);
        if (pump_is_on() != s_state.on) pump_set(s_state.on);      // relay level must always follow the state
        bool on = s_state.on;
        s_heartbeat_s = now_s;
        xSemaphoreGive(s_lock);

        if (read && now_s >= next_history_s) {
            next_history_s = now_s + HISTORY_PERIOD_S;
            history_add(valid, temp_x10, on);
        }
        esp_task_wdt_reset();
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));      // woken early by manual commands
    }
}

void control_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    pump_init();
    ctl_init(&s_state, events_mono_s());
    if (s_lock == NULL
        || xTaskCreatePinnedToCore(control_task, "control", 6144, NULL, CONTROL_TASK_PRIORITY, &s_task,
                                   CONTROL_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "Control task not created, rebooting");
        esp_restart();
    }
}

bool control_alive(void)
{
    control_status_t s;
    control_status(&s);
    return s.now_mono_s - s.heartbeat_mono_s <= 3 * CONTROL_READ_PERIOD_S;
}

void control_status(control_status_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out->state = s_state;
    out->heartbeat_mono_s = s_heartbeat_s;
    out->runtime_today_s = s_runtime_today_s;
    out->starts_today = s_starts_today;
    out->runtime_boot_s = s_runtime_boot_s;
    out->starts_boot = s_starts_boot;
    xSemaphoreGive(s_lock);
    out->now_mono_s = events_mono_s();
}

control_result_t control_manual(ctl_manual_t manual, uint32_t duration_s, const char *who)
{
    if (duration_s < MANUAL_MIN_S) duration_s = MANUAL_MIN_S;
    if (duration_s > MANUAL_MAX_S) duration_s = MANUAL_MAX_S;
    char detail[24];
    if (manual == CTL_MANUAL_NONE) snprintf(detail, sizeof(detail), "auto, %s", who);
    else snprintf(detail, sizeof(detail), "%lu min, %s", (unsigned long) (duration_s / 60), who);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t now_s = events_mono_s();
    if (!ctl_manual(&s_state, manual, duration_s, now_s)) {
        xSemaphoreGive(s_lock);
        return CONTROL_REFUSED_OVERHEAT;
    }
    ctl_result_t r;
    memset(&r, 0, sizeof(r));
    decide_locked(now_s, &r, detail);
    xSemaphoreGive(s_lock);
    ESP_LOGW(TAG, "(not error) Manual %s for %lu s by %s",
             manual == CTL_MANUAL_START ? "start" : manual == CTL_MANUAL_STOP ? "stop" : "auto",
             (unsigned long) duration_s, who);
    xTaskNotifyGive(s_task);
    return CONTROL_OK;
}

void control_settings_changed(void)
{
    if (s_task) xTaskNotifyGive(s_task);
}
