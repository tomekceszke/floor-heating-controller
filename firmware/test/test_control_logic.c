#include <string.h>
#include "control_logic.h"
#include "unity_lite.h"

static const ctl_config_t CFG = {
    .start_x10 = 300,
    .stop_x10 = 250,
    .critical_x10 = 450,
    .maintenance_interval_s = 7 * 24 * 3600,
    .maintenance_run_s = 60,
    .sensor_fail_reads = 3,
};

/* One control step: a read (temp_x10 < -1000 = failed read) followed by a decision. */
static ctl_result_t step(ctl_state_t *s, int64_t now_s, int temp_x10)
{
    ctl_result_t r;
    memset(&r, 0, sizeof(r));
    ctl_sensor(s, &CFG, now_s, temp_x10 >= -1000, (int16_t) temp_x10, &r);
    ctl_decide(s, &CFG, now_s, &r);
    return r;
}

#define FAIL -2000

static void off_at_boot_between_thresholds(void)
{
    ctl_state_t s;
    ctl_init(&s, 0);
    ctl_result_t r = step(&s, 5, 280);
    CHECK(!s.on);
    CHECK(!r.changed);
    CHECK_EQ(s.reason, CTL_AUTO_COLD);
}

static void hysteresis(void)
{
    ctl_state_t s;
    ctl_init(&s, 0);
    CHECK(!step(&s, 5, 300).changed);           // == start: not above
    ctl_result_t r = step(&s, 10, 301);
    CHECK(r.changed);
    CHECK(s.on);
    CHECK_EQ(s.reason, CTL_AUTO_HOT);
    CHECK_EQ(s.changed_s, 10);
    CHECK(!step(&s, 15, 260).changed);          // between: keeps running
    CHECK(!step(&s, 20, 250).changed);          // == stop: not below
    r = step(&s, 25, 249);
    CHECK(r.changed);
    CHECK(!s.on);
    CHECK_EQ(r.prev_reason, CTL_AUTO_HOT);
    CHECK(!step(&s, 30, 290).changed);          // between again: stays off
}

static void manual_start_expires(void)
{
    ctl_state_t s;
    ctl_init(&s, 0);
    step(&s, 5, 200);
    CHECK(ctl_manual(&s, CTL_MANUAL_START, 1800, 10));
    ctl_result_t r;
    memset(&r, 0, sizeof(r));
    ctl_decide(&s, &CFG, 10, &r);
    CHECK(r.changed);
    CHECK(s.on);
    CHECK_EQ(s.reason, CTL_MANUAL_ON);
    CHECK(!step(&s, 1809, 200).changed);
    r = step(&s, 1810, 200);
    CHECK(r.manual_expired);
    CHECK(r.changed);
    CHECK(!s.on);
    CHECK_EQ(r.prev_reason, CTL_MANUAL_ON);
    CHECK_EQ(s.manual, CTL_MANUAL_NONE);
}

static void manual_stop_holds_against_heat_then_expires(void)
{
    ctl_state_t s;
    ctl_init(&s, 0);
    step(&s, 5, 350);
    CHECK(s.on);
    CHECK(ctl_manual(&s, CTL_MANUAL_STOP, 3600, 10));
    ctl_result_t r = step(&s, 10, 350);
    CHECK(r.changed);
    CHECK(!s.on);
    CHECK_EQ(s.reason, CTL_MANUAL_OFF);
    CHECK(!step(&s, 3000, 400).changed);        // hot but below critical: manual wins
    r = step(&s, 3610, 400);
    CHECK(r.manual_expired);
    CHECK(s.on);
    CHECK_EQ(s.reason, CTL_AUTO_HOT);
}

static void overheat_overrides_manual_stop(void)
{
    ctl_state_t s;
    ctl_init(&s, 0);
    step(&s, 5, 350);
    ctl_manual(&s, CTL_MANUAL_STOP, 3600, 10);
    step(&s, 10, 350);
    CHECK(!s.on);
    ctl_result_t r = step(&s, 20, 450);
    CHECK(r.overheat_started);
    CHECK(r.manual_cancelled);
    CHECK(r.changed);
    CHECK(s.on);
    CHECK_EQ(s.reason, CTL_OVERHEAT);
    CHECK(!ctl_manual(&s, CTL_MANUAL_STOP, 3600, 25));     // refused while overheated
    CHECK(!step(&s, 30, 435).overheat_ended);   // clears only 2 °C below critical
    r = step(&s, 35, 429);
    CHECK(r.overheat_ended);
    CHECK(s.on);                                // still hot: auto keeps it running
    CHECK_EQ(s.reason, CTL_AUTO_HOT);
}

static void maintenance_runs_after_idle_and_stops_itself(void)
{
    ctl_state_t s;
    ctl_init(&s, 0);
    int64_t week = CFG.maintenance_interval_s;
    CHECK(!step(&s, week - 1, 200).maintenance_started);
    ctl_result_t r = step(&s, week, 200);
    CHECK(r.maintenance_started);
    CHECK(r.changed);
    CHECK(s.on);
    CHECK_EQ(s.reason, CTL_MAINTENANCE);
    CHECK(!step(&s, week + 59, 200).changed);
    r = step(&s, week + 60, 200);
    CHECK(r.changed);
    CHECK(!s.on);
    CHECK_EQ(r.prev_reason, CTL_MAINTENANCE);
    CHECK(!step(&s, week + 120, 200).maintenance_started);  // idle clock restarted
    CHECK(step(&s, 2 * week + 60, 200).maintenance_started);
}

static void maintenance_not_when_warm_or_recently_run(void)
{
    ctl_state_t s;
    ctl_init(&s, 0);
    int64_t week = CFG.maintenance_interval_s;
    CHECK(!step(&s, week, 270).maintenance_started);        // between thresholds: not below stop
    step(&s, week + 10, 310);                                // runs
    step(&s, week + 20, 240);                                // stops
    CHECK(!step(&s, week + 30, 200).maintenance_started);
    CHECK(step(&s, 2 * week + 20, 200).maintenance_started);
}

static void sensor_failure_fail_safe_and_recovery(void)
{
    ctl_state_t s;
    ctl_init(&s, 0);
    step(&s, 5, 200);
    CHECK(!step(&s, 10, FAIL).sensor_failed);
    CHECK(!step(&s, 15, FAIL).sensor_failed);
    ctl_result_t r = step(&s, 20, FAIL);
    CHECK(r.sensor_failed);
    CHECK(r.changed);
    CHECK(s.on);
    CHECK_EQ(s.reason, CTL_SENSOR_FAIL);
    CHECK(!step(&s, 25, FAIL).sensor_failed);   // reported once
    r = step(&s, 30, 200);
    CHECK(r.sensor_recovered);
    CHECK(r.changed);
    CHECK(!s.on);
    CHECK_EQ(s.failures, 0);
}

static void sensor_missing_from_boot(void)
{
    ctl_state_t s;
    ctl_init(&s, 0);
    step(&s, 5, FAIL);
    step(&s, 10, FAIL);
    CHECK(!s.on);
    step(&s, 15, FAIL);
    CHECK(s.on);
    CHECK(!s.have_temp);
    CHECK(!step(&s, 7 * 24 * 3600 + 100, FAIL).maintenance_started);
}

static void manual_stop_wins_over_sensor_failure(void)
{
    ctl_state_t s;
    ctl_init(&s, 0);
    step(&s, 5, FAIL);
    step(&s, 10, FAIL);
    step(&s, 15, FAIL);
    CHECK(s.on);
    CHECK(ctl_manual(&s, CTL_MANUAL_STOP, 600, 20));
    ctl_result_t r = step(&s, 20, FAIL);
    CHECK(r.changed);
    CHECK(!s.on);
    r = step(&s, 620, FAIL);
    CHECK(r.manual_expired);
    CHECK(s.on);
    CHECK_EQ(s.reason, CTL_SENSOR_FAIL);
}

static void back_to_auto(void)
{
    ctl_state_t s;
    ctl_init(&s, 0);
    step(&s, 5, 200);
    ctl_manual(&s, CTL_MANUAL_START, 3600, 10);
    step(&s, 10, 200);
    CHECK(s.on);
    CHECK(ctl_manual(&s, CTL_MANUAL_NONE, 0, 20));
    ctl_result_t r = step(&s, 20, 200);
    CHECK(r.changed);
    CHECK(!r.manual_expired);
    CHECK(!s.on);
}

int main(void)
{
    RUN(off_at_boot_between_thresholds);
    RUN(hysteresis);
    RUN(manual_start_expires);
    RUN(manual_stop_holds_against_heat_then_expires);
    RUN(overheat_overrides_manual_stop);
    RUN(maintenance_runs_after_idle_and_stops_itself);
    RUN(maintenance_not_when_warm_or_recently_run);
    RUN(sensor_failure_fail_safe_and_recovery);
    RUN(sensor_missing_from_boot);
    RUN(manual_stop_wins_over_sensor_failure);
    RUN(back_to_auto);
    return report();
}
