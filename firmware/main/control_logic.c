#include <string.h>

#include "control_logic.h"

#define OVERHEAT_CLEAR_X10 20   // overheat ends 2 °C below the critical threshold

void ctl_init(ctl_state_t *s, int64_t now_s)
{
    memset(s, 0, sizeof(*s));
    s->reason = CTL_AUTO_COLD;
    s->changed_s = now_s;
    s->last_on_s = now_s;
}

void ctl_sensor(ctl_state_t *s, const ctl_config_t *cfg, int64_t now_s, bool valid, int16_t temp_x10,
                ctl_result_t *res)
{
    if (!valid) {
        if (s->failures < UINT8_MAX) s->failures++;
        if (!s->sensor_failed && s->failures >= cfg->sensor_fail_reads) {
            s->sensor_failed = true;
            res->sensor_failed = true;
        }
        return;
    }

    if (s->sensor_failed) res->sensor_recovered = true;
    s->sensor_failed = false;
    s->failures = 0;
    s->have_temp = true;
    s->temp_x10 = temp_x10;
    s->temp_s = now_s;

    if (!s->overheat && temp_x10 >= cfg->critical_x10) {
        s->overheat = true;
        res->overheat_started = true;
    } else if (s->overheat && temp_x10 < cfg->critical_x10 - OVERHEAT_CLEAR_X10) {
        s->overheat = false;
        res->overheat_ended = true;
    }

    if (temp_x10 > cfg->start_x10) s->hot = true;
    else if (temp_x10 < cfg->stop_x10) s->hot = false;
}

bool ctl_manual(ctl_state_t *s, ctl_manual_t manual, uint32_t duration_s, int64_t now_s)
{
    if (manual == CTL_MANUAL_STOP && s->overheat) return false;
    s->manual = manual;
    s->manual_until_s = manual == CTL_MANUAL_NONE ? 0 : now_s + duration_s;
    return true;
}

void ctl_decide(ctl_state_t *s, const ctl_config_t *cfg, int64_t now_s, ctl_result_t *res)
{
    if (s->manual != CTL_MANUAL_NONE && now_s >= s->manual_until_s) {
        s->manual = CTL_MANUAL_NONE;
        res->manual_expired = true;
    }
    if (s->manual == CTL_MANUAL_STOP && s->overheat) {
        s->manual = CTL_MANUAL_NONE;
        res->manual_cancelled = true;
    }

    if (s->on) s->last_on_s = now_s;
    if (s->maintenance && now_s >= s->maintenance_until_s) s->maintenance = false;
    if (!s->maintenance && !s->on && s->manual == CTL_MANUAL_NONE && !s->sensor_failed && s->have_temp
        && s->temp_x10 < cfg->stop_x10 && now_s - s->last_on_s >= (int64_t) cfg->maintenance_interval_s) {
        s->maintenance = true;
        s->maintenance_until_s = now_s + cfg->maintenance_run_s;
        res->maintenance_started = true;
    }

    bool on;
    ctl_reason_t reason;
    if (s->overheat) {
        on = true;
        reason = CTL_OVERHEAT;
    } else if (s->manual == CTL_MANUAL_START) {
        on = true;
        reason = CTL_MANUAL_ON;
    } else if (s->manual == CTL_MANUAL_STOP) {
        on = false;
        reason = CTL_MANUAL_OFF;
    } else if (s->sensor_failed) {
        on = true;
        reason = CTL_SENSOR_FAIL;
    } else if (s->hot) {
        on = true;
        reason = CTL_AUTO_HOT;
    } else if (s->maintenance) {
        on = true;
        reason = CTL_MAINTENANCE;
    } else {
        on = false;
        reason = CTL_AUTO_COLD;
    }

    res->prev_reason = s->reason;
    if (on != s->on) {
        s->on = on;
        s->changed_s = now_s;
        res->changed = true;
    }
    if (on) s->last_on_s = now_s;
    s->reason = reason;
}

const char *ctl_reason_name(ctl_reason_t reason)
{
    switch (reason) {
        case CTL_AUTO_COLD: return "auto_cold";
        case CTL_AUTO_HOT: return "auto_hot";
        case CTL_MANUAL_ON: return "manual_on";
        case CTL_MANUAL_OFF: return "manual_off";
        case CTL_MAINTENANCE: return "maintenance";
        case CTL_SENSOR_FAIL: return "sensor_fail";
        case CTL_OVERHEAT: return "overheat";
    }
    return "?";
}
