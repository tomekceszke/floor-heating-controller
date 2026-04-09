# Floor Heating Controller — ESP32 Firmware

Production firmware for an ESP32-based water pump controller for underfloor heating. Reads water temperature from a DS18B20 sensor and drives a relay to start/stop the pump based on configurable thresholds.

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

## Prerequisites

- **ESP-IDF 5.4.2** — activate before building:
  ```bash
  source "/path/to/.espressif/tools/activate_idf_v5.4.2.sh"
  ```
- `main/config/credentials.h` — copy from template and fill in secrets:
  ```bash
  cp main/config/credentials-example.h main/config/credentials.h
  ```
- TLS certificate for OTA server — place at `certs/ota_server_cert_15.pem`

## Configuration

### `main/config/config.h` (committed)

| Constant | Default | Description |
|----------|---------|-------------|
| `PUMP_START_TEMP` | 30.0°C | Start pump above this temperature |
| `PUMP_STOP_TEMP` | 25.0°C | Stop pump below this temperature |
| `SAMPLE_PERIOD_S` | 60 s | Main loop interval |
| `INVALID_TEMPERATURE_INDICATOR` | 85.0 | DS18B20 error sentinel |
| `WIFI_RETRY_DELAY_S` | 60 s | Max WiFi reconnect backoff |
| `TEMP_SENSOR_SCAN_RETRY_S` | 30 s | 1-Wire scan retry on failure |
| `TEMP_SENSOR_IN_GPIO` | GPIO4 | DS18B20 data pin |
| `PUMP_CTRL_OUT_GPIO` | GPIO16 | Relay control pin |

### `main/config/credentials.h` (never committed)

```c
#define WIFI_SSID                   ""
#define WIFI_PASS                   ""
#define HEADER_AUTHORIZATION_VALUE  ""   // HTTP Authorization header value
#define NTFY_TOPIC                  ""   // ntfy.sh topic name (acts as shared secret)
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

## OTA Updates

Firmware pulls updates from a local HTTPS server on boot and on demand:

- **Automatic**: checked at every boot
- **On demand**: `POST /admin/su` triggers an OTA pull
- **Push helper**: `upload.sh` (not committed — may contain credentials)

On successful update the firmware deletes the binary from the server, then reboots.

The OTA server URL and TLS certificate are configured in `config.h` and `certs/` respectively.

## REST API

HTTP server on port 80. Authenticated endpoints require an `Authorization` header matching `HEADER_AUTHORIZATION_VALUE`.

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/api/status` | no | JSON: `current_temp`, `is_pump_running`, `pump_start_temp`, `pump_stop_temp` |
| GET | `/api/is-pump-running` | no | `"1"` or `"0"` |
| POST | `/api/toggle-pump` | yes | Toggle pump relay |
| POST | `/admin/su` | yes | Trigger OTA update |
| POST | `/admin/reboot` | yes | Restart device |
| GET | `/admin/hw-status` | no | JSON: `up_since`, `free_mem_kb` |

### Example

```bash
curl http://<device-ip>/api/status
# {"current_temp": 28.50, "is_pump_running": false, "pump_start_temp": 30.00, "pump_stop_temp": 25.00}

curl -X POST http://<device-ip>/api/toggle-pump \
  -H "Authorization: <token>"
```

## Project Structure

```
firmware/
├── CMakeLists.txt
├── sdkconfig                       # active build config (committed)
├── upload.sh                       # OTA push helper (not committed)
├── certs/                          # TLS certs (not committed)
├── components/
│   └── esp-idf-lib/                # third-party component library (submodule)
├── http_examples/                  # REST API test scripts
└── main/
    ├── main.c                      # app_main, main_loop
    ├── wifi.c                      # WiFi STA with exponential backoff reconnect
    ├── udp_logging.c               # UDP log forwarding (mirrors serial + UDP)
    ├── ota.c                       # HTTPS OTA
    ├── ntp.c                       # SNTP time sync (CET/CEST)
    ├── web.c                       # HTTP server + REST API
    ├── pump.c                      # GPIO relay control
    ├── temp_sensor.c               # DS18B20 init and read
    ├── notify.c                    # ntfy.sh push notifications
    └── config/
        ├── config.h                # non-secret configuration (committed)
        ├── credentials.h           # secrets (never committed)
        └── credentials-example.h  # template
```

## Push Notifications

The firmware sends push notifications via [ntfy.sh](https://ntfy.sh) to your phone or any ntfy client.

| Event | Title | Message |
|-------|-------|---------|
| Boot complete (WiFi + NTP ready) | `Device ready` | `Floor heating controller started` |
| Pump starts | `Pump started` | `Temperature: XX.X°C` |
| Pump stops | `Pump stopped` | `Temperature: XX.X°C` |

Configure your topic in `credentials.h`:
```c
#define NTFY_TOPIC "your-topic-name"
```

Notifications use HTTPS (`esp_crt_bundle_attach` — no certificate file required). A failed notification logs a warning and is silently ignored — it never affects pump control.

## Planned Features

- **Embedded web UI** — gzipped HTML served from flash for runtime configuration of thresholds (currently requires reflash to change `PUMP_START_TEMP` / `PUMP_STOP_TEMP`)
- **Test coverage** — unit and integration tests
- **ESP-IDF 6.0 upgrade**
