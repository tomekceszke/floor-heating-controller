#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "config/config.h"
#include "maintenance.h"
#include "notify.h"

static const char *TAG = "MAINTENANCE";

/* Forward declarations — defined in pump.c */
void pump_start(void);
void pump_stop(void);

/* Converts seconds to FreeRTOS ticks */
#define SEC_TO_TICKS(s) ((TickType_t)((s) * 1000u / portTICK_PERIOD_MS))

static TickType_t s_last_active_ticks;      /* tick when pump last ran (any reason) */
static TickType_t s_run_start_ticks;        /* tick when current maintenance run started */
static bool       s_running             = false;
static bool       s_prev_pump_was_active = false;
static time_t     s_last_pump_started_at = 0;
static bool       s_initialized         = false;

void maintenance_check(bool pump_was_active, float curr_temp)
{
    TickType_t now = xTaskGetTickCount();

    if (!s_initialized) {
        s_last_active_ticks = now;
        s_initialized = true;
    }

    if (s_running) {
        if ((now - s_run_start_ticks) >= SEC_TO_TICKS(MAINTENANCE_RUN_DURATION_S)) {
            if (!pump_was_active) {
                pump_stop();
            }
            ESP_LOGI(TAG, "Maintenance run complete");
            s_last_active_ticks = now;
            s_running = false;
        }
    } else {
        /* Detect pump start transition (off → on) */
        if (pump_was_active && !s_prev_pump_was_active) {
            s_last_pump_started_at = time(NULL);
        }
        if (pump_was_active) {
            s_last_active_ticks = now;
            s_prev_pump_was_active = true;
            return;
        }
        /* Pump is off — check if maintenance interval has elapsed */
        if ((now - s_last_active_ticks) >= SEC_TO_TICKS(MAINTENANCE_INTERVAL_S)
                && curr_temp < PUMP_STOP_TEMP) {
            pump_start();
            s_run_start_ticks = now;
            s_last_pump_started_at = time(NULL);
            s_running = true;
            ESP_LOGI(TAG, "Maintenance run started");
            notify_maintenance_started();
        }
    }

    s_prev_pump_was_active = pump_was_active;
}

bool is_maintenance_running(void)
{
    return s_running;
}

time_t maintenance_get_last_pump_started_at(void)
{
    return s_last_pump_started_at;
}
