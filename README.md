# floor-heating-controller

ESP32 controller for an underfloor heating circulation pump. A DS18B20 on the supply pipe, a relay for the pump: the
pump runs when the water is warm enough to heat the floor and stops when it cools down. In production for 2+ years.

Built on [home-idf](https://github.com/tomekceszke/home-idf), the shared framework of the home controllers (WiFi, UDP
logs, NTP, OTA with rollback, ntfy, web sign-in and the app shell).

## Features

- **Hysteresis control** with thresholds set from the app (defaults: on above 30 °C, off below 25 °C), stored in NVS.
- **Critical temperature** (default 45 °C): the pump is forced on and an alert goes out, even if it was stopped by hand.
- **Sensor fail-safe**: three failed reads in a row and the pump runs until the sensor answers again.
- **Manual start and stop** for 30 min, 1 h or 4 h, then back to automatic control.
- **Maintenance run**: 60 s after 7 idle days, so the impeller does not seize over the summer.
- **App** (phone, tablet, desktop): live scheme of the loop, temperature, run time, 24 h temperature chart with pump-on
  spans, event history, thresholds, device info.
- **Notifications** via [ntfy](https://ntfy.sh): pump start/stop on one topic, critical alerts on another.
- The control task never depends on the network: WiFi, the web server or ntfy failing does not change pump behaviour.

## Hardware

| Item | Value |
|---|---|
| Module | ESP32-WROOM-32E, 4 MB flash |
| PCB | ESP32_Relay_AC X1 V1.1 (303E32AC111) |
| Sensor | DS18B20 on GPIO4 (1-Wire) |
| Relay | GPIO16, HIGH = pump on |

## Build

```sh
cp firmware/main/config/credentials-example.h firmware/main/config/credentials.h   # fill in
# firmware/certs/ota_server_cert_15.pem: certificate of your OTA server
firmware/build.sh                                  # ESP-IDF 5.4.2
firmware/build.sh -p /dev/cu.usbserial-0001 flash  # download mode: hold IO0, press+release EN, release IO0
cmake -S firmware/test -B build-test && cmake --build build-test && ctest --test-dir build-test
```

Devices still on the legacy firmware move to 2.x over the air with the migrator: `docs/IDF5_MIGRATION.md`.

## API

Sign-in and common routes come from home-idf. Controller routes:

| Method | Path | Guard | |
|---|---|---|---|
| GET | `/api/status` | session | pump, temperature, today, thresholds, system |
| GET | `/api/events` | session | last 50 events |
| GET | `/api/history` | session | 24 h of one-minute samples |
| POST | `/api/pump` | session + CSRF | `{"state":"start","minutes":60}`, `{"state":"stop","minutes":60}`, `{"state":"auto"}` |
| POST | `/api/settings` | session + CSRF | `{"start_c":30,"stop_c":25,"critical_c":45}` (partial, clamped) |
| POST | `/admin/pump` | `Authorization` header | same as `/api/pump`, for scripts |
| GET | `/admin/status` | `Authorization` header | same as `/api/status` |

## License

MIT
