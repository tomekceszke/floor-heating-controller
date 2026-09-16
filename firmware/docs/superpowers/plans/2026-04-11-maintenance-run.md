# Maintenance Run Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a weekly 1-minute pump exercise run that fires automatically when the pump has been idle for 7 days, preventing impeller seizure during summer idle periods.

**Architecture:** A new `maintenance.c` module owns a non-blocking state machine (IDLE / RUNNING) called once per `main_loop()` tick. It tracks last pump activity via FreeRTOS tick count and triggers `pump_start()` / `pump_stop()` directly when the idle interval expires and water temperature is safely below the stop threshold. The main loop never blocks — the state machine returns in microseconds every tick.

**Tech Stack:** ESP-IDF 5.4.2, FreeRTOS, C99, `esp_http_server`, ntfy.sh notifications via `esp_http_client`

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `main/config/config.h` | Modify | Add 2 new compile-time constants |
| `main/maintenance.h` | Create | Public interface for maintenance module |
| `main/maintenance.c` | Create | State machine — all maintenance run logic |
| `main/notify.h` | Modify | Declare `notify_maintenance_started()` |
| `main/notify.c` | Modify | Implement `notify_maintenance_started()` |
| `main/main.c` | Modify | Call `maintenance_check()` at end of loop body |
| `main/web.c` | Modify | Update `/api/status` — remove threshold fields, add maintenance fields |
| `main/CMakeLists.txt` | Modify | Add `maintenance.c` to `SRCS` |
| `CLAUDE.md` | Modify | Document new module and constants |

---

## Task 1: Add config constants

**Files:**
- Modify: `main/config/config.h`

- [ ] **Step 1: Add constants to config.h**

Open `main/config/config.h` and add after the `PUMP_STOP_TEMP` line:

```c
#define MAINTENANCE_INTERVAL_S      (7 * 24 * 3600)  // 168 hours — weekly
#define MAINTENANCE_RUN_DURATION_S  60               // 1 minute
```

- [ ] **Step 2: Commit**

```bash
git add main/config/config.h
git commit -m "feat: add maintenance run config constants"
```

---

## Task 2: Create maintenance module

**Files:**
- Create: `main/maintenance.h`
- Create: `main/maintenance.c`

- [ ] **Step 1: Create `main/maintenance.h`**

```c
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
```

- [ ] **Step 2: Create `main/maintenance.c`**

```c
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "config/config.h"
#include "maintenance.h"

static const char *TAG = "MAINTENANCE";

/* Forward declarations — defined in pump.c and notify.c */
void pump_start(void);
void pump_stop(void);
void notify_maintenance_started(void);

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
            pump_stop();
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
```

- [ ] **Step 3: Add `maintenance.c` to CMakeLists.txt**

Open `main/CMakeLists.txt` and add `"maintenance.c"` to the SRCS list:

```cmake
idf_component_register(SRCS "main.c"
        "wifi.c"
        "log_dispatch.c"
        "log_udp.c"
        "ota.c"
        "ntp.c"
        "web.c"
        "pump.c"
        "temp_sensor.c"
        "notify.c"
        "maintenance.c"
        PRIV_REQUIRES spi_flash
        mbedtls
        esp_wifi
        esp_http_client
        esp_http_server
        esp_https_ota
        nvs_flash
        esp_driver_gpio
        esp_timer
        ds18x20
        INCLUDE_DIRS "include"
        "config"
        EMBED_TXTFILES ${project_dir}/certs/ota_server_cert_15.pem
)
```

- [ ] **Step 4: Build to verify compilation**

```bash
source "$HOME/.espressif/tools/activate_idf_v5.4.2.sh" && idf.py build
```

Expected: build succeeds. If you see undefined reference errors for `pump_start`, `pump_stop`, or `notify_maintenance_started` — those are resolved in the next tasks (linker resolves at link time, but `notify_maintenance_started` doesn't exist yet so you will get a link error). That's expected — proceed to Task 3 first, then re-run the build.

- [ ] **Step 5: Commit**

```bash
git add main/maintenance.h main/maintenance.c main/CMakeLists.txt
git commit -m "feat: add maintenance module with non-blocking state machine"
```

---

## Task 3: Add notify_maintenance_started()

**Files:**
- Modify: `main/notify.h`
- Modify: `main/notify.c`

- [ ] **Step 1: Add declaration to `main/notify.h`**

Add after `notify_device_ready()`:

```c
void notify_maintenance_started(void);
```

- [ ] **Step 2: Add implementation to `main/notify.c`**

Add after `notify_device_ready()`:

```c
void notify_maintenance_started(void)
{
    send_notification_to(NTFY_TOPIC, "Maintenance run", "Weekly pump exercise run started");
}
```

- [ ] **Step 3: Build to verify no link errors**

```bash
idf.py build
```

Expected: build succeeds with no errors or warnings.

- [ ] **Step 4: Commit**

```bash
git add main/notify.h main/notify.c
git commit -m "feat: add notify_maintenance_started notification"
```

---

## Task 4: Wire maintenance_check() into main_loop()

**Files:**
- Modify: `main/main.c`

- [ ] **Step 1: Add include and call in `main/main.c`**

Add `#include "maintenance.h"` near the top with the other includes:

```c
#include "maintenance.h"
```

At the end of the `while(1)` body in `main_loop()`, after the existing pump start/stop logic and before `xTaskDelayUntil`, add:

```c
maintenance_check(is_pump_running(), curr_temp);
```

The updated `main_loop()` should look like:

```c
void main_loop(void) {
    static bool s_ready_notified = false;
    TickType_t last_wake_time = xTaskGetTickCount();
    while (1) {
        float curr_temp = read_temp();

        if (INVALID_TEMPERATURE_INDICATOR == curr_temp) {
            ESP_LOGW(TAG, "Invalid read. Retrying in next turn...");
            vTaskDelay((SAMPLE_PERIOD_S * 1000) / portTICK_PERIOD_MS);
            curr_temp = read_temp();
            if (INVALID_TEMPERATURE_INDICATOR == curr_temp) {
                ESP_LOGW(TAG, "Invalid read. Again. Ignored.");
                continue;
            }
        }
        if (!s_ready_notified) {
            notify_device_ready(curr_temp, is_pump_running());
            s_ready_notified = true;
        }
        if (curr_temp > PUMP_START_TEMP && !is_pump_running()) {
            ESP_LOGI(TAG, "Pump started");
            pump_start();
            notify_pump_started(curr_temp);
        } else if (curr_temp < PUMP_STOP_TEMP && is_pump_running()) {
            ESP_LOGI(TAG, "Pump stopped");
            pump_stop();
            notify_pump_stopped(curr_temp);
        }
        maintenance_check(is_pump_running(), curr_temp);
        xTaskDelayUntil(&last_wake_time, (SAMPLE_PERIOD_S * 1000) / portTICK_PERIOD_MS);
    }
}
```

- [ ] **Step 2: Build**

```bash
idf.py build
```

Expected: clean build, no warnings.

- [ ] **Step 3: Commit**

```bash
git add main/main.c
git commit -m "feat: wire maintenance_check into main_loop"
```

---

## Task 5: Update /api/status response

**Files:**
- Modify: `main/web.c`

- [ ] **Step 1: Add include to `main/web.c`**

Add `#include "maintenance.h"` near the top with the other includes:

```c
#include "maintenance.h"
```

- [ ] **Step 2: Replace `get_status_handler()`**

Replace the entire `get_status_handler` function with:

```c
static esp_err_t get_status_handler(httpd_req_t *req) {
    char last_started[32];
    time_t last_started_at = maintenance_get_last_pump_started_at();
    if (last_started_at == 0) {
        snprintf(last_started, sizeof(last_started), "never");
    } else {
        struct tm timeinfo;
        localtime_r(&last_started_at, &timeinfo);
        strftime(last_started, sizeof(last_started), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }

    char data[256];
    snprintf(data, sizeof(data),
            "{\n"
            "   \"current_temp\": %0.2f,\n"
            "   \"is_pump_running\": %s,\n"
            "   \"is_maintenance_running\": %s,\n"
            "   \"last_pump_started_at\": \"%s\"\n"
            "}",
            read_temp(),
            bool2string(is_pump_running()),
            bool2string(is_maintenance_running()),
            last_started
    );
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, data, strlen(data));
}
```

Note: buffer increased from 200 → 256 to fit the new fields. `pump_start_temp` and `pump_stop_temp` are intentionally removed (they are settings, not runtime status).

- [ ] **Step 3: Build**

```bash
idf.py build
```

Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add main/web.c
git commit -m "feat: update /api/status with maintenance fields, remove threshold fields"
```

---

## Task 6: Update CLAUDE.md documentation

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Add maintenance.c to project structure table**

In the `## Project Structure` section, add `maintenance.c` to the `main/` listing:

```
    ├── maintenance.c       # weekly pump exercise run (anti-seize, non-blocking state machine)
```

Place it after `pump.c`.

- [ ] **Step 2: Add maintenance module description**

In the `## Modules` section, add after the `### pump` section:

```markdown
### `maintenance` (maintenance.c)
Non-blocking state machine preventing pump impeller seizure during idle periods (summer).
Tracks last pump activity via FreeRTOS ticks. Triggers a `MAINTENANCE_RUN_DURATION_S`-second
run when pump has been idle for `MAINTENANCE_INTERVAL_S` and `curr_temp < PUMP_STOP_TEMP`.
Called once per `main_loop()` tick — never blocks.

Functions:
- `maintenance_check(bool pump_was_active, float curr_temp)` — called each loop tick
- `is_maintenance_running()` — used by `web.c` for `/api/status`
- `maintenance_get_last_pump_started_at()` — returns `time_t` of last pump start (0 = never since boot)
```

- [ ] **Step 3: Add constants to config.h table**

In the `### config.h` table, add:

```
| `MAINTENANCE_INTERVAL_S` | 604800 s (7 days) | Idle interval before maintenance run |
| `MAINTENANCE_RUN_DURATION_S` | 60 s | Duration of each maintenance run |
```

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: document maintenance module in CLAUDE.md"
```

---

## Task 7: Hardware verification

This project has no unit test framework — correctness is verified by flashing to the test board and observing serial output.

- [ ] **Step 1: Shorten interval for testing**

Temporarily change `MAINTENANCE_INTERVAL_S` in `main/config/config.h` to trigger quickly:

```c
#define MAINTENANCE_INTERVAL_S      120   // 2 minutes — TESTING ONLY, revert before production flash
```

- [ ] **Step 2: Full build and flash**

```bash
idf.py build && idf.py flash monitor
```

(Enter download mode first: hold IO0, press+release EN, release IO0.)

- [ ] **Step 3: Verify maintenance run triggers**

With pump initially off and temp below 25°C, wait ~2 minutes. Expected serial output:

```
I (MAINTENANCE): Maintenance run started
... (60 seconds later) ...
I (MAINTENANCE): Maintenance run complete
```

Also verify ntfy.sh receives "Weekly pump exercise run started".

- [ ] **Step 4: Verify /api/status response**

```bash
curl http://192.168.11.241/api/status
```

Expected (maintenance not running):
```json
{
   "current_temp": 18.50,
   "is_pump_running": false,
   "is_maintenance_running": false,
   "last_pump_started_at": "2026-04-11 10:32:00"
}
```

Verify `pump_start_temp` and `pump_stop_temp` are absent.

- [ ] **Step 5: Verify main loop keeps ticking during maintenance run**

During the 1-minute maintenance run, watch serial output. The `main_loop` should continue logging temperature reads every 60s — confirming the non-blocking design works.

- [ ] **Step 6: Restore production interval**

Revert `main/config/config.h`:

```c
#define MAINTENANCE_INTERVAL_S      (7 * 24 * 3600)  // 168 hours — weekly
```

- [ ] **Step 7: Final build and flash**

```bash
idf.py build && idf.py flash
```

Press EN to boot. Verify normal operation via serial monitor.

- [ ] **Step 8: Final commit**

```bash
git add main/config/config.h
git commit -m "feat: restore maintenance interval to production value (7 days)"
```
