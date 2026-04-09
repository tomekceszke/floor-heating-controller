# floor-heating-controller — CLAUDE.md

## Project Overview

Production ESP32 firmware for a floor heating water pump controller.
Logic: start pump when water temperature exceeds `PUMP_START_TEMP`, stop when it drops below
`PUMP_STOP_TEMP` (hysteresis). Running in production for 2+ years.

Planned new features:
- Embedded **web UI** served as gzipped HTML (`index_html_gz`) for configuration
  (set hysteresis and start temperature at runtime)
- Test coverage (unit/integration tests)
- Upgrade to ESP-IDF 6.0

## Hardware

| Item | Value |
|------|-------|
| Module | ESP32-WROOM-32E |
| Flash | 4 MB |
| WiFi | 802.11 b/g/n (2.4 GHz) |
| Bluetooth | BT/BLE (unused) |
| PCB | ESP32_Relay_AC X1 V1.1 (303E32AC111) — custom relay board |
| Test board | identical second unit available for testing |

### GPIO

| GPIO | Direction | Function |
|------|-----------|----------|
| GPIO_NUM_4 | IN | DS18B20 temperature sensor (1-Wire) |
| GPIO_NUM_16 | OUT | Pump relay control (HIGH = pump ON) |
| GPIO_NUM_23 | OUT | LED (currently unused/commented out) |

### Temperature Sensor

DS18B20 on 1-Wire bus (single sensor, `ds18x20` component from `esp-idf-lib`).
`INVALID_TEMPERATURE_INDICATOR = 85.00` — DS18B20 power-on default, treated as read error.

## Toolchain & Environment

**ESP-IDF version: 5.4.2**

Before any build/flash operation, activate the environment:
```bash
source "/path/to/.espressif/tools/activate_idf_v5.4.2.sh"
```

IDE: CLion (`.idea/` in `.gitignore`).

Build commands (after activation):
```bash
idf.py build
idf.py flash
idf.py flash monitor
idf.py monitor
```

Documentation: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/index.html
Framework sources: https://github.com/espressif/esp-idf

## Project Structure

```
firmware/
├── CLAUDE.md
├── CMakeLists.txt          # project root, includes components/esp-idf-lib/components
├── sdkconfig               # active build config (committed)
├── upload.sh               # OTA upload helper (in .gitignore — may contain credentials)
├── certs/                  # TLS certificates (NEVER committed — in .gitignore)
├── components/
│   └── esp-idf-lib/        # third-party component library (submodule)
├── http_examples/          # REST API test scripts
└── main/
    ├── CMakeLists.txt
    ├── main.c              # app_main, main_loop
    ├── wifi.c              # WiFi STA with exponential backoff reconnect
    ├── udp_logging.c       # UDP log forwarding (mirrors to serial + UDP)
    ├── ota.c               # HTTPS OTA from local server, deletes bin after success
    ├── ntp.c               # SNTP time sync, sets TZ to CET/CEST (Poland)
    ├── web.c               # HTTP server, REST API + future embedded UI
    ├── pump.c              # GPIO relay control
    ├── temp_sensor.c       # DS18B20 init and read
    ├── notify.c            # ntfy.sh push notifications
    └── config/
        ├── config.h        # all non-secret configuration (committed)
        ├── credentials.h   # secrets — NEVER committed
        └── credentials-example.h  # template for credentials.h
```

## Modules

### `wifi` (wifi.c)
WiFi STA mode. Exponential backoff on disconnect (doubles delay up to `WIFI_RETRY_DELAY_S = 60s`).
Blocks `app_main` until connected. Credentials from `credentials.h`.

### `udp_logging` (udp_logging.c)
Overrides `esp_log_set_vprintf` to forward all logs via UDP to `LOG_UDP_IP:LOG_UDP_PORT`
(currently `192.168.11.15:1344`) while also printing locally to serial. Buffer: 256 bytes per message.

### `ota` (ota.c)
HTTPS OTA from `OTA_URL` (`https://192.168.11.15:8070/floor-heating-controller.bin`).
Uses embedded cert `certs/ota_server_cert_15.pem`. On success: deletes the bin via HTTP DELETE, then reboots.
Triggered at boot and via `POST /admin/su`.

### `ntp_client` (ntp.c)
SNTP sync against `pool.ntp.org` + Polish servers. Timezone: `CET-1CEST,M3.5.0,M10.5.0/3`.
Sets global `boot_time[64]` string used in `/admin/hw-status` response.

### `web` (web.c)
`esp_http_server` on default port 80. Authorization via `Authorization` header
(value defined in `credentials.h` as `HEADER_AUTHORIZATION_VALUE`).

Current REST API:

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/api/status` | no | JSON: current_temp, is_pump_running, start/stop temps |
| GET | `/api/is-pump-running` | no | `"1"` or `"0"` |
| POST | `/api/toggle-pump` | yes | Toggle pump relay |
| POST | `/admin/su` | yes | Trigger OTA update |
| POST | `/admin/reboot` | yes | Restart device |
| GET | `/admin/hw-status` | no | JSON: up_since, free_mem_kb |

Commented-out `main_handler` / `index_html_gz` — planned embedded UI entry point.

### `pump` (pump.c)
GPIO relay control. `reset_gpio()` configures GPIO_NUM_16 as input/output with pull-up.
Pump state read via `gpio_get_level(PUMP_CTRL_OUT_GPIO)` — HIGH = running.

### `temp_sensor` (temp_sensor.c)
DS18B20 via `ds18x20` component. `init_sensor()` scans 1-Wire bus, retries every
`TEMP_SENSOR_SCAN_RETRY_S = 30s` until device found. `read_temp()` returns
`INVALID_TEMPERATURE_INDICATOR (85.0)` on error.

### `notify` (notify.c)
Push notifications via ntfy.sh. POSTs to `https://ntfy.sh/<NTFY_TOPIC>` using HTTPS with
ESP-IDF's built-in CA bundle (`esp_crt_bundle_attach` — no cert file needed).
Timeout: 5 s. Failures log a warning and are silently ignored — never affects main functionality.

Functions:
- `notify_device_ready()` — called once in `app_main` after NTP sync; signals successful boot + WiFi + time
- `notify_pump_started(float temp)` — called in `main_loop` after `pump_start()`
- `notify_pump_stopped(float temp)` — called in `main_loop` after `pump_stop()`

Topic configured as `NTFY_TOPIC` in `credentials.h` (treated as secret — not committed).
Requires `mbedtls` in `PRIV_REQUIRES` in `main/CMakeLists.txt`.

## Configuration

### config.h (committed — no secrets)

| Constant | Value | Description |
|----------|-------|-------------|
| `PUMP_START_TEMP` | 30.0 °C | Start pump above this |
| `PUMP_STOP_TEMP` | 25.0 °C | Stop pump below this (hysteresis) |
| `SAMPLE_PERIOD_S` | 60 s | Main loop interval |
| `INVALID_TEMPERATURE_INDICATOR` | 85.0 | DS18B20 error sentinel |
| `OTA_URL` | https://192.168.11.15:8070/... | Local OTA server |
| `LOG_UDP_IP` | 192.168.11.15 | UDP log receiver |
| `LOG_UDP_PORT` | 1344 | UDP log port |
| `TEMP_SENSOR_IN_GPIO` | GPIO_NUM_4 | 1-Wire pin |
| `PUMP_CTRL_OUT_GPIO` | GPIO_NUM_16 | Relay pin |
| `LED_OUT_GPIO` | GPIO_NUM_23 | LED pin |

### credentials.h (NEVER committed)

```c
#define HEADER_AUTHORIZATION_VALUE  ""   // HTTP Authorization header value
#define WIFI_SSID                   ""
#define WIFI_PASS                   ""
#define NTFY_TOPIC                  ""   // ntfy.sh topic (shared secret)
```

Template: `config/credentials-example.h`

## Git & Security Rules

- Respect `.gitignore` at all times.
- **NEVER** commit or stage: `certs/`, `main/config/credentials.h`, `upload.sh`,
  any `*.private.*` files, `http_examples/.http.env.json`.
- Before suggesting any `git add .` or equivalent — verify against `.gitignore` explicitly.
- Never include secrets in code, comments, or log messages.

## Developer Notes

- Primary language: Java developer with 20+ years experience. C is secondary.
- **Always flag** potential memory issues: uninitialized pointers, missing `free()` after `malloc()`,
  stack-allocated buffers passed across task boundaries, buffer overflows.
- Use `snprintf` instead of `sprintf` — flag any existing `sprintf` usage as a bug candidate.
- `char buf[length]` where `length` comes from HTTP request is a VLA — risky, flag it.
- Prefer stack allocation over heap for this embedded context.
- Stability over features. This device runs unattended in production.
- There is a second identical board available for testing — destructive tests are acceptable on it.
- `PUMP_START_TEMP` / `PUMP_STOP_TEMP` currently require reflash to change — planned: runtime-configurable via web UI.
