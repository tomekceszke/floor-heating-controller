# Maintenance Run Feature — Design Spec

**Date:** 2026-04-11  
**Status:** Approved

---

## Background

Circulation pump impellers can seize after weeks of inactivity due to mineral deposits, corrosion,
and sediment settling on the shaft ("stiction"). The floor heating pump sits idle all summer.
Industry standard is to exercise the pump briefly once a week during idle periods:

| Source | Frequency | Duration |
|--------|-----------|----------|
| Viessmann boilers (built-in) | every 24h of no heat call | brief |
| Caleffi zone relays (built-in) | if no heat call for 72h | brief |
| Thermostat exercise mode | 1 min/day if no heat call | 1 minute |
| Grundfos Alpha "summer mode" | periodic | slow speed, brief |
| Community consensus | 1×/month minimum | 1–2 minutes |

**Decision:** Weekly interval (7 days), 1-minute duration — protective without unnecessary wear,
matches thermostat exercise mode specs.

---

## Requirements

- Trigger a 1-minute pump run when the pump has been idle for 7 days
- Only trigger when water temperature is safely below `PUMP_STOP_TEMP` (heating not needed)
- Non-blocking: main loop continues ticking every 60s throughout the maintenance run
- Track last pump start time (for any reason) and expose it via REST API
- Send a push notification when a maintenance run starts
- Compile-time constants only — no runtime configurability needed
- No NVS persistence — timer resets on reboot (acceptable)

---

## Architecture

### New module: `maintenance.c` / `maintenance.h`

Owns all maintenance run state and logic. Called once per `main_loop()` tick.

**Public interface:**

```c
void   maintenance_check(bool pump_was_active, float curr_temp);
bool   is_maintenance_running(void);
time_t maintenance_get_last_pump_started_at(void);  // 0 = never started since boot
```

**Static state:**

```c
static TickType_t s_last_active_ticks;     // tick when pump last ran; init = xTaskGetTickCount()
static TickType_t s_run_start_ticks;       // tick when current maintenance run started
static bool       s_running;              // true while maintenance run is in progress
static bool       s_prev_pump_was_active; // previous tick pump state, for transition detection
static time_t     s_last_pump_started_at; // wall-clock time of last pump start (0 = never)
```

**`maintenance_check()` state machine:**

```
if s_running:
    if (now - s_run_start_ticks) >= MAINTENANCE_RUN_DURATION_S ticks:
        pump_stop()
        ESP_LOGI "Maintenance run complete"
        s_last_active_ticks = now
        s_running = false

else (IDLE):
    if pump_was_active && !s_prev_pump_was_active:      // pump just started (transition)
        s_last_pump_started_at = time(NULL)
    if pump_was_active:
        s_last_active_ticks = now
        s_prev_pump_was_active = true
        return
    if (now - s_last_active_ticks) >= MAINTENANCE_INTERVAL_S ticks
       AND curr_temp < PUMP_STOP_TEMP:
        pump_start()
        s_run_start_ticks = now
        s_last_pump_started_at = time(NULL)
        s_running = true
        ESP_LOGI "Maintenance run started"
        notify_maintenance_started()

s_prev_pump_was_active = pump_was_active
```

---

## File Changes

| File | Change |
|------|--------|
| `main/maintenance.c` | **New** — state machine, all maintenance logic |
| `main/maintenance.h` | **New** — public interface |
| `main/config/config.h` | Add `MAINTENANCE_INTERVAL_S` and `MAINTENANCE_RUN_DURATION_S` |
| `main/main.c` | Call `maintenance_check(is_pump_running(), curr_temp)` at end of loop body |
| `main/notify.c` | Add `notify_maintenance_started()` |
| `main/notify.h` | Declare `notify_maintenance_started()` |
| `main/web.c` | Update `get_status_handler()` — remove temp threshold fields, add maintenance fields |
| `main/CMakeLists.txt` | Add `maintenance.c` to `SRCS` |
| `CLAUDE.md` | Document new module and constants |

---

## New Config Constants (`config.h`)

```c
#define MAINTENANCE_INTERVAL_S      (7 * 24 * 3600)  // 168 hours — weekly
#define MAINTENANCE_RUN_DURATION_S  60               // 1 minute
```

---

## Updated `/api/status` Response

Removes `pump_start_temp` and `pump_stop_temp` (settings, not status). Adds maintenance and last
pump start fields.

```json
{
  "current_temp": 18.50,
  "is_pump_running": false,
  "is_maintenance_running": false,
  "last_pump_started_at": "2026-04-10 14:32:00"
}
```

`last_pump_started_at` formatted with `strftime()` (same pattern as `boot_time` in `ntp.c`).
Returns `"never"` if pump hasn't started since boot.

**Note:** Removing `pump_start_temp` / `pump_stop_temp` is a breaking API change.

---

## Notifications

One new notify function: `notify_maintenance_started()`.  
Uses `NTFY_TOPIC` (same as pump events). Message: `"Maintenance run started"`.  
No stop notification — 1-minute runs don't warrant it.

---

## Verification

1. **Build:** `idf.py build` — no warnings, links clean
2. **Simulate idle:** Set `MAINTENANCE_INTERVAL_S` to `120` temporarily, flash to test board,
   confirm maintenance run starts after 2 min of pump inactivity, stops after 1 min
3. **Simulate interruption:** During a maintenance run verify main loop still ticks and responds
   to temperature changes normally
4. **Status API:** `GET /api/status` — verify new fields present, old threshold fields absent,
   `last_pump_started_at` updates correctly
5. **Notification:** Verify ntfy.sh receives `"Maintenance run started"`
6. **Restore:** Set `MAINTENANCE_INTERVAL_S` back to `(7 * 24 * 3600)` before production flash
