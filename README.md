# Floor Heating Controller — ESP32 Firmware

Production firmware for an ESP32-based water pump controller for underfloor heating. Reads water temperature from a DS18B20 sensor and drives a relay to start/stop the pump based on configurable thresholds.

> Running in production for 2+ years on an ESP32_Relay_AC board.

## Features

- **Hysteresis control** — start/stop pump at configurable temperature thresholds with 5°C hysteresis to prevent rapid cycling
- **Maintenance mode** — automatically exercises the pump for 60 s every 7 days when idle, preventing impeller seizure during summer
- **REST API** — monitor status and control the pump over HTTP
- **Push notifications** — boot, pump start/stop, and sensor error alerts via [ntfy.sh](https://ntfy.sh)
- **OTA updates** — pull firmware from a local HTTPS server at boot or on demand, no need to touch the device
- **UDP log forwarding** — mirror ESP logs to a host over UDP for remote debugging

## How It Works

The main loop samples temperature every 60 seconds:

- **Start pump** when temperature rises above `PUMP_START_TEMP` (30°C)
- **Stop pump** when temperature drops below `PUMP_STOP_TEMP` (25°C)
- Hysteresis of 5°C prevents rapid cycling
- Invalid temperature reads (DS18B20 power-on default: 85°C) are retried once, then skipped

## Hardware

| Item | Value |
|------|-------|
| Module | ESP32-WROOM-32E |
| Flash | 4 MB |
| PCB | ESP32_Relay_AC X1 V1.1 (303E32AC111) |

### GPIO

| GPIO | Direction | Function |
|------|-----------|----------|
| GPIO4 | IN | DS18B20 temperature sensor (1-Wire) |
| GPIO16 | OUT | Pump relay (HIGH = pump ON) |
| GPIO23 | OUT | LED (unused) |

## Quick Start

```bash
# 1. Clone
git clone <repo-url>
cd firmware

# 2. Fill in credentials
cp main/config/credentials-example.h main/config/credentials.h
# Edit credentials.h: WiFi SSID/password, HTTP auth token, ntfy.sh topic

# 3. Provide OTA server TLS certificate
# Place your cert at certs/ota_server_cert_15.pem
# (Or update OTA_URL and the cert filename in config.h and CMakeLists.txt)

# 4. Activate ESP-IDF 5.4.2
source "/path/to/.espressif/tools/activate_idf_v5.4.2.sh"

# 5. Build and flash
idf.py build
# Put device in download mode: hold IO0, press+release EN, release IO0
idf.py flash monitor
```

## Configuration

### `main/config/config.h` (committed — no secrets)

| Constant | Default | Description |
|----------|---------|-------------|
| `PUMP_START_TEMP` | 30.0°C | Start pump above this temperature |
| `PUMP_STOP_TEMP` | 25.0°C | Stop pump below this temperature |
| `SAMPLE_PERIOD_S` | 60 s | Main loop interval |
| `MAINTENANCE_INTERVAL_S` | 604800 s | Idle time before maintenance run (7 days) |
| `MAINTENANCE_RUN_DURATION_S` | 60 s | Duration of each maintenance run |
| `INVALID_TEMPERATURE_INDICATOR` | 85.0 | DS18B20 error sentinel |
| `OTA_URL` | https://…/floor-heating-controller.bin | OTA server URL |
| `LOG_UDP_IP` / `LOG_UDP_PORT` | 192.168.11.15 / 1344 | UDP log receiver |
| `TEMP_SENSOR_IN_GPIO` | GPIO4 | DS18B20 data pin |
| `PUMP_CTRL_OUT_GPIO` | GPIO16 | Relay control pin |

### `main/config/credentials.h` (never committed)

Copy from `credentials-example.h` and fill in your values:

```c
#define WIFI_SSID                   ""
#define WIFI_PASS                   ""
#define HEADER_AUTHORIZATION_VALUE  ""   // HTTP Authorization header value
#define NTFY_TOPIC                  ""   // ntfy.sh topic for pump events; "" to disable
#define NTFY_ERROR_TOPIC            ""   // ntfy.sh topic for error alerts; "" to disable
```

## Build & Flash

```bash
# Activate ESP-IDF
source "/path/to/.espressif/tools/activate_idf_v5.4.2.sh"

# Build
idf.py build

# Flash and monitor
idf.py flash monitor

# Monitor only (device already flashed)
idf.py monitor
```

### Entering Download Mode

The board has two buttons: **IO0** (Boot) and **EN** (Reset).

1. Press and hold **IO0**
2. Press and release **EN**
3. Release **IO0**

Then run `idf.py flash`. After flashing, press **EN** to boot.

## REST API

HTTP server on port 80. Authenticated endpoints require an `Authorization` header matching `HEADER_AUTHORIZATION_VALUE`.

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/api/status` | no | JSON: current state (see below) |
| GET | `/api/is-pump-running` | no | `"1"` or `"0"` |
| POST | `/api/toggle-pump` | yes | Toggle pump relay |
| POST | `/admin/su` | yes | Trigger OTA update |
| POST | `/admin/reboot` | yes | Restart device |
| GET | `/admin/hw-status` | no | JSON: `up_since`, `free_mem_kb` |

### `/api/status` response

```json
{
  "current_temp": 28.50,
  "is_pump_running": false,
  "is_maintenance_running": false,
  "last_pump_started_at": "2026-04-15T10:23:00"
}
```

### Example

```bash
curl http://<device-ip>/api/status

curl -X POST http://<device-ip>/api/toggle-pump \
  -H "Authorization: <token>"
```

## Push Notifications

Alerts via [ntfy.sh](https://ntfy.sh) — works with the ntfy mobile app or any HTTP client.

| Topic | Event | When |
|-------|-------|------|
| `NTFY_TOPIC` | `Device ready` | Boot complete (WiFi + NTP ready) |
| `NTFY_TOPIC` | `Pump started / stopped` | Pump relay changes state, includes temperature |
| `NTFY_ERROR_TOPIC` | `Temperature sensor error` | Repeated DS18B20 read failures |

Configure topics in `credentials.h`. Set to `""` to disable silently. Uses HTTPS with ESP-IDF's built-in CA bundle — no certificate file required. Notification failures are logged and ignored, never affecting pump control.

## Maintenance Mode

Prevents pump impeller seizure during extended idle periods (e.g. summer when heating is off).

If the pump hasn't run for 7 days **and** the water temperature is below `PUMP_STOP_TEMP`, the firmware automatically starts the pump for 60 seconds. This is a non-blocking state machine called on each main loop tick — it never delays normal temperature-based control.

Tune the behaviour in `config.h`:
- `MAINTENANCE_INTERVAL_S` — idle threshold (default: 7 days)
- `MAINTENANCE_RUN_DURATION_S` — run duration (default: 60 s)

Maintenance state is visible in `/api/status` (`is_maintenance_running`, `last_pump_started_at`).

## OTA Updates

Firmware pulls updates from a local HTTPS server:

- **Automatic**: checked at every boot
- **On demand**: `POST /admin/su` triggers an OTA pull
- **Push helper**: `upload.sh` (not committed — may contain credentials)

On successful update the firmware deletes the binary from the server, then reboots.

The OTA server URL and TLS certificate are configured in `config.h` and `certs/` respectively.

## Project Structure

```
firmware/
├── CMakeLists.txt
├── sdkconfig                       # active build config (committed)
├── upload.sh                       # OTA push helper (not committed)
├── certs/                          # TLS certs (not committed)
├── http_examples/                  # REST API test scripts
└── main/
    ├── main.c                      # app_main, main_loop
    ├── wifi.c                      # WiFi STA with exponential backoff reconnect
    ├── udp_logging.c               # UDP log forwarding (mirrors serial + UDP)
    ├── ota.c                       # HTTPS OTA
    ├── ntp.c                       # SNTP time sync (CET/CEST)
    ├── web.c                       # HTTP server + REST API
    ├── pump.c                      # GPIO relay control
    ├── maintenance.c               # weekly anti-seize pump exercise
    ├── temp_sensor.c               # DS18B20 init and read
    ├── notify.c                    # ntfy.sh push notifications
    └── config/
        ├── config.h                # non-secret configuration (committed)
        ├── credentials.h           # secrets (never committed)
        └── credentials-example.h  # template
```

## Planned Features

- **Embedded web UI** — gzipped HTML served from flash for runtime configuration of thresholds (currently requires reflash to change `PUMP_START_TEMP` / `PUMP_STOP_TEMP`)
- **Test coverage** — unit and integration tests
- **ESP-IDF 6.0 upgrade**
