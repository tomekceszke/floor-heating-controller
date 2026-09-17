# floor-heating-controller

ESP32 controller for the floor heating circulation pump: a DS18B20 on the supply pipe, a relay for the pump. The pump
runs above the start threshold and stops below the stop threshold. In production for 2+ years. **Pump control is the
product: UI, notifications and history must never weaken it.**

## Status

- **Firmware 2.0.0 is built, not on the device yet.** Production still runs the legacy firmware (own copies of
  wifi/log/ota/ntp/notify/web, IDF 5.4.x bootloader, stock `partitions_two_ota`, no rollback).
- 2.0.0 moves to home-idf (v0.1.12) and to the water/gate partition layout, so it needs the one-shot migrator:
  procedure and log in `docs/IDF5_MIGRATION.md`. Owner decision 2026-09-17: migrate production directly, without a
  spare-board rehearsal (the relay board has USB for recovery).
- Pushes of this repo and of home-idf v0.1.12 wait for the push window (see global rules); CI pins home-idf v0.1.12.

## Behaviour (design rules for every change)

- The control task (`control.c`, core 1, task watchdog) reads the sensor every 5 s and is the only code that switches
  the relay. It never calls the network and never waits on a queue; events and notifications are non-blocking.
- Decisions live in `control_logic.c` (no ESP-IDF includes, host-tested), priority from highest:
  1. **overheat**: at or above `critical` the pump runs and a manual stop is dropped (clears 2 °C below critical);
  2. **manual** start/stop for 1 min–12 h (app chips 30 min / 1 h / 4 h), then back to automatic; RAM only;
  3. **sensor failure**: 3 bad reads in a row (error or 85.0 °C) and the pump runs as a fail-safe until a good read;
  4. **hysteresis**: on above `start`, off below `stop`;
  5. **maintenance**: 60 s anti-seize run after 7 days idle while below `stop`; it stops itself.
- Thresholds are set from the app and stored in NVS (`fh_settings`), clamped: start 20–60 °C, stop ≥ 10 °C and
  ≤ start − 2, critical ≥ start + 5 and ≤ 80. Defaults 30 / 25 / 45 (`config.h`).
- A reboot returns to automatic control; the pump is off for the first read (< 1 s).

## Hardware

| Item | Value |
|---|---|
| Module | ESP32-WROOM-32E, 4 MB flash (DIO, 40 MHz) |
| PCB | ESP32_Relay_AC X1 V1.1 (303E32AC111), CP2102 USB-UART (`/dev/cu.usbserial-0001`) |
| Production | `192.168.11.247`, MAC `98:CD:AC:4E:75:54` |

| GPIO | Function |
|---|---|
| 4 | DS18B20 (1-Wire) |
| 16 | Pump relay, HIGH = on |
| 23 | LED (unused) |

Download mode for USB flashing: hold **IO0**, press and release **EN**, release **IO0**; press **EN** after flashing.
Relay clicks during a boot loop without valid firmware are expected.

## Firmware (`firmware/`)

```sh
firmware/build.sh                          # idf.py build; uses ../home-idf (sibling checkout) when present
HOME_IDF_FROM_GIT=1 firmware/build.sh      # against the pinned tag (main/idf_component.yml)
cmake -S firmware/test -B build-test && cmake --build build-test && ctest --test-dir build-test   # host tests
tools/build_release.sh                     # firmware + migrator into releases/ with sha256
```
- `sdkconfig` is generated from `sdkconfig.defaults` (never `idf.py set-target`). `partitions.csv` is the water/gate
  layout: nvs/otadata/phy as in two_ota, ota_0 2M, ota_1 1.875M at the legacy ota_1 offset, coredump.
- A home-idf change needs a tag, a bump in `firmware/main/idf_component.yml` **and** `migrator/main/idf_component.yml`,
  deleting `dependencies.lock`, then `HOME_IDF_FROM_GIT=1 firmware/build.sh` to regenerate it.
- Setup: `cp firmware/main/config/credentials-example.h firmware/main/config/credentials.h`; values may be
  `obf1:` strings from `home-idf/tools/obfuscate.py`; the web password hash comes from `home-idf/tools/hash_password.py`
  (empty hash = sign-in disabled). `firmware/certs/ota_server_cert_15.pem` is the OTA server's trust anchor.

### Layout

```
main/
  main.c           boot: NVS → settings → events → control task → health → log → WiFi/notify/NTP/OTA/auth/httpd
  control_logic.c  pure decisions (hysteresis, manual, overheat, sensor fail-safe, maintenance), host-tested
  control.c        control task, relay, runtime/starts counters, manual commands
  temp_sensor.c    DS18B20 read, rescans the bus after failures
  pump.c           relay GPIO
  settings.c       thresholds in NVS, clamped
  events.c         RAM ring of 50 events, ntfy mapping
  history.c        24 h of one-minute samples in RAM (temperature + pump bit)
  api.c            routes on the home-idf HTTP server
web/               app.html (Live / History / Settings on the home-idf app shell), manifest, icon
test/              host tests for control_logic
migrator/          one-shot legacy → 2.x image (home-idf hi_migrator), keeps the pump on while it runs
```

### Notifications (ntfy, two topics as in the other controllers)

- `NTFY_TOPIC`: automatic start/stop (default priority), manual start/stop and end of a manual run (low),
  maintenance run (low), boot (min). The end of a maintenance run is not sent.
- `NTFY_ERROR_TOPIC`: overheat reached / over, sensor failure / recovery, every `ESP_LOGE` line, unexpected resets
  (home-idf, one-hour dedup).

### HTTP API (port 80)

Common routes come from home-idf: `/`, `/api/session`, `/api/login`, `/api/logout`, `/api/reboot`, `/api/ota`,
`/admin/su`, `/admin/reboot`, `/admin/hw-status`. Pump routes:

| Method | Path | Guard | Description |
|---|---|---|---|
| GET | `/api/status` | session | `pump`, `temp`, `today`, `since_boot`, `settings` (+ limits), `system` (with `bootloader_idf`) |
| GET | `/api/events` | session | last 50 events (pump, overheat, sensor, manual_end) |
| GET | `/api/history` | session | `{period_s, newest, newest_age_s, t:[0.1 °C or null], p:"0101…"}`, oldest first, chunked |
| POST | `/api/pump` | mutation | `{state:"start"\|"stop", minutes}` or `{state:"auto"}`; 409 `overheat` for a stop while overheated |
| POST | `/api/settings` | mutation | partial `{start_c, stop_c, critical_c}`, clamped |
| POST | `/admin/pump` | `Authorization` header | same body as `/api/pump` |
| GET | `/admin/status` | `Authorization` header | same as `/api/status` |

The legacy `/api/toggle-pump`, `/api/is-pump-running` and unauthenticated `/api/status` are gone (nothing used them).

### UI work

```sh
../home-idf/tools/dev_proxy.py 192.168.11.247 --page firmware/web/app.html --name fh-controller
```
Same layout as water and gate (shared-main-view decision 2026-09-16): wordmark + status, headline, three numbers
(°C water, time in the current state, pumping today), main view (heat source → thermometer → pump → floor loop),
latest events, dock with the swipe (start/stop for the chosen duration) and chips. English.

## Infrastructure

- OTA server `https://192.168.11.15:8070`, file `floor-heating-controller.bin` (`~/apps/ota-server` on .15).
- UDP logs `192.168.11.15:1344` (`nc -ul 1344`).

## Rules

- **Never** commit `credentials.h`, `certs/*`, `upload.sh`, `*.private.*`, `releases/`. Run `git check-ignore` before `git add`.
- Commit messages clean, no AI attribution.
- Anything writing the bootloader or partition table needs the owner's approval at that moment.
- `snprintf` only; no VLAs sized from request data; flag leaks and stack buffers crossing tasks.
- Keep `control_logic.c` free of ESP-IDF includes and cover changes with host tests.
