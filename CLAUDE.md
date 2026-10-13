# floor-heating-controller

ESP32 controller for the floor heating circulation pump: a DS18B20 on the supply pipe, a relay for the pump. The pump
runs above the start threshold and stops below the stop threshold. In production for 2+ years. **Pump control is the
product: UI, notifications and history must never weaken it.**

## Status

- **Production runs firmware 2.2.0** (OTA 2026-10-13 13:30, sha256 `accb3147…`, commit `15044f1`: `GET /admin/history`
  with the read-only key; home-idf v0.1.13, built with `HOME_IDF_FROM_GIT=1 firmware/build.sh -B build-git build`).
  2.1.0 (OTA 2026-09-21 10:51, sha256 `9a0b7fb9…`, commit `2491373`: MQTT with a retained
  state topic, read-only admin key; home-idf v0.1.13). 2.0.0 (2026-09-17 20:24, sha256 `d8d83cde…`, home-idf v0.1.12)
  was migrated from the legacy firmware over the air with the one-shot migrator, so the device now has the 5.4.2
  bootloader, the water/gate partition layout (ota_0 2M / ota_1 1.875M / coredump) and native rollback. Procedure and
  log in `docs/IDF5_MIGRATION.md`; no spare-board rehearsal (owner decision, the relay board has USB for recovery).
- **History in hc-data** since 2026-10-13: `server/` (`fh-ingest`, database `floorheating`, table `sample`: temperature,
  pump, reason, thresholds every 60 s and on each pump change; `server/README.md`).
- Updates from now on: `tools/build_release.sh`, publish `releases/floor-heating-controller.bin` as
  `floor-heating-controller.bin` on the OTA server, `POST /admin/su` (or the Device tab).

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
server/            hc-data backend: fh-ingest (MQTT state → PostgreSQL floorheating), install/deploy, tests
```

### Notifications (ntfy, two topics as in the other controllers)

Topics follow the owner's convention `<prefix>-floor-heating-controller` and `<prefix>-floor-heating-controller-error`
(set 2026-09-17, obfuscated in `credentials.h`).

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
| GET | `/admin/history` | `Authorization` header (read-only or admin) | same as `/api/history` (2.2.0) |

The legacy `/api/toggle-pump`, `/api/is-pump-running` and unauthenticated `/api/status` are gone (nothing used them).

### UI work

```sh
../home-idf/tools/dev_proxy.py 192.168.11.247 --page firmware/web/app.html --name fh-controller
```
Same layout as water and gate (shared-main-view decision 2026-09-16): wordmark + status, headline, three numbers
(°C water, time in the current state, pumping today), main view (heat source → thermometer → pump → floor loop),
latest events, dock with the swipe (start/stop for the chosen duration) and chips. English.

README screenshots (`docs/img/app-*.png`, 390×844 at 2x) come from `tools/screenshots.py`: it serves the page with
mocked `/api/*` data and shoots it in headless Chrome (which clamps the window to 500 px, so the page is zoomed
back to a phone and the image scaled down). Regenerate them after UI changes; `--keep-server` opens the same mock
in a browser.

## Infrastructure

- OTA server `https://192.168.11.15:8070`, file `floor-heating-controller.bin` (`~/apps/ota-server` on .15).
- UDP logs `192.168.11.15:1344` (`nc -ul 1344`).

## Hydraulics: the pump set

Recorded 2026-10-04 (owner + manufacturer leaflet `docs/ulotka_zestaw_pompowy_2013.pdf`, Prandelli Polska, no
longer hosted by the maker; the copy comes from the Web Archive).

- The floor circuit (3 loops) is a **Prandelli RPM pump set** on a tee from the boiler supply (Junkers Cerapur Smart,
  radiators on the same supply; boiler run by a Junkers FW100, max flow temperature set to ~55–60 °C).
- Mixing is **thermostatic injection**: valve (1) on the supply bar, opened by a capillary head (2) with an immersion
  sensor, holds a **fixed** floor supply temperature set on the head (leaflet: max 55 °C). Return regulating valve (7)
  sets the bypass. Not weather-compensated; the floor only gets heat while the boiler heats for the radiators.
- Flow path (leaflet diagram + owner's photo, 2026-10-13): boiler supply enters the **lower** bar (sticker
  "ZASILANIE") at the right through valve (1); the loops return into the same lower bar through valves (9); the pump
  on the left lifts that mix into the **upper** bar (sticker "POWRÓT"), which feeds the loops through the flow meters
  (8) and lets the surplus out at the right through return regulating valve (7) to the boiler return. The stickers
  name the boiler-side connections, not the loops: loop supply = pipes from the upper bar, loop return = pipes into
  the lower bar. So the boiler gets back mixed floor-supply water (~29 °C measured), not the loops' return.
- Pump: Wilo RS 15/4, switched by this controller's relay. Loop valves (9) have thermostatic inserts that take
  electric actuators (per-loop zoning would be possible).
- The optional strap-on 60 °C pump thermostat is **not installed** (owner, 2026-10-04).
- The DS18B20 sits on the **boiler side, right before thermostatic valve (1)/head (2)** (owner, ~95 % sure,
  2026-10-04). So it measures boiler supply, not what goes into the floor, and the overheat rule (pump on at
  `critical`) is a heat dump while the thermostatic valve still limits the floor: keep it.

What protects the floor from hot water: only the thermostatic head, plus the FW100's max flow temperature (~55–60 °C)
as the ceiling of the boiler supply. A capillary head that loses its charge lets the valve spring open fully, so the
floor would then get boiler supply, up to that ceiling, and this controller cannot see it (its sensor is upstream).
heating-controller's **Floor supply** sensor `7100000008ece828` cannot either: it sits next to this one, before the
valve (owner, 2026-10-13). The only sensors after the mixing are the hall loop pair at the manifold, on alu-pex 16:
**Hallway floor supply** `77041469f284ff28` (reads ~2–3 K low, bare strap-on on pex); an alert when it stays above
~45 °C would catch a failed head. Alert only, no pump decisions across devices.

Boiler-side option, if a real cut-off is ever wanted: the boiler has terminals 8-9 for **TB1**, a 24 V temperature
watchdog meant for floor heating connected to the boiler (bridged by default). Mounted on the floor supply after
the set, it stops the burner **and** DHW when it trips (fault d3), so radiators go cold too. A cheaper target is the
pump: a strap-on thermostat in series with the Wilo, as the set's optional 60 °C version does.

### Hydraulic layout and the pump conflict (owner, 2026-10-04)

- Low-temperature system, max ~55 °C supply, one zone, one weather controller (FW100). Heat split roughly 60 %
  radiators / 40 % floor. 14 radiators (11 active; garage, utility room and boiler room closed all year) on two
  manifolds, ground floor in the garage next to this set, attic in the small bathroom, with **no thermostatic or zone
  valves** (inventory 2026-10-08: `../heatronic-controller/docs/INSTALLATION.md`).
- Boiler supply and return run in copper to a tee in the plaster at the boiler: up to the attic manifold, and down to
  a **tee buried in the screed** with one branch to the ground floor radiator manifold, one to this pump
  set (RPM-R, the variant with flowmeters; current datasheet:
  <https://atusa.pl/wp-content/uploads/2020/12/Karta_rozdzielacz_RPM_R_2024.pdf>). **No hydraulic separator.**
- The DS18B20 is a metal probe clamped to the copper supply pipe of the floor branch, before valve (1); heating-controller's
  Floor supply `7100000008ece828` sits next to it (both read 21.9 / 22.1 °C idle on 2026-10-13). Pipes from the
  boiler are soldered copper 22 mm OD, loops alu-pex 16. All sensor positions: `../heating-controller/CLAUDE.md`,
  Sensor positions.
- Observed: the set's pump and the boiler pump fight. Depending on the set's inlet/outlet valves either only the
  radiators heat, or only the floor (and the radiators nearest the boiler). The working compromise: boiler pump on
  its **highest** speed, Wilo RS 15/4 on its **lowest**, the set's inlet/outlet ball valves (5) about half open.
- Without its pump the floor gets little flow, and it runs **backwards**: the boiler pump pushes supply through valve
  (1) into the lower bar, through the loops from their return end into the upper bar and out through (7). Seen on the
  hall loop at every heating start (2026-10-13 05:16–05:20: its return pipe +6 K while the supply pipe stayed cold)
  until this pump starts at 30 °C; then the supply leads. The loops' resistance is far above the radiators', so most
  of the boiler pump's flow goes to the radiators.

Thresholds: **30/25 °C stays** (owner decision 2026-10-04, after discussing both directions). Revisited 2026-10-13
after the FW100 curve went down (start point 30 °C since 2026-10-11): simulated on heating-controller's Floor supply
(same pipe as this sensor) over 51 h, 30/25 runs the pump during only **68 %** of the burner's heating time (starts
~3.6 min after the burner, 31 starts); the supply here peaks at 32.4 °C median, 30.5 °C at the 10th percentile, so on
milder days the pump may not start at all. 28/24 gives 89 % (14 starts, pump on 22 of 51 h), 27/23 97 %. Proposed to
the owner: 28/24; the cost is more floor pump time against the radiators (the FW100's room sensor is radiator-side).
Decision pending. Higher thresholds
would cut the floor's share further under the throttled compromise; lower ones would run the pump on water that is
barely above room temperature.

Options discussed for the conflict (none decided; the compromise works):
- A hydraulic separator next to the boiler, before the pipes enter the screed, decouples the boiler pump from both
  branches. It then needs **its own pump for the radiator branch** (on the secondary side the boiler pump no longer
  reaches the radiators). What the Cerapur Smart ZSB 22-3 C offers for that (its service manual, pp. 10–11, 23, 32,
  in heatronic-controller `docs/`): terminals NP-LP drive an external pump in an unmixed secondary circuit when
  service function 5.E = 02 (it then runs continuously in heating mode); LZ-NZ drive an external primary pump. The
  Smart has **no input for a separator flow sensor**. NP-LP is either that pump or a DHW circulation pump, not both.
- Cheaper partial fixes: balance with a proper balancing valve on the floor branch instead of half-open ball valves
  (ball valves are shut-off valves, not regulators). A check valve on the floor branch was suggested, but it does
  not decouple the pumps.
- Measure first: heatronic-controller will show the boiler's flow/return and pump modulation, heating-controller
  already logs radiator and floor supply/return; together they show what each valve setting actually does.

Possible upgrade, only if the data asks for it: replace head (2) with a proportional actuator (valves of this kind
usually take M30×1.5; verify) and drive it from here with a floor heating curve, using the outdoor temperature that
heating-controller and heatronic-controller already publish. That is a home-made equivalent of Junkers' IPM 1 module
without the FW200 it would need (the FW100 runs one heating circuit only).

## Related projects

- `../heatronic-controller/` (started 2026-10-04): a read-only ESP32-C3 on the gas boiler's Heatronic 3 bus, next to
  the FW100. The floor circuit is fed from the boiler supply through a tee and a mixer, so its data (requested and
  actual flow temperature, burner modulation, DHW priority, outdoor temperature) explains when this pump starts and
  stops; this controller's pump state and supply temperature in turn help read the boiler's behaviour. Its firmware
  is planned on this repository as the template (home-idf app, pure decision code with host tests, build/release
  tools).
- `../heating-controller/`: DS18B20 history of the floor supply/return and the other circuits in hc-data.

## Rules

- **Never** commit `credentials.h`, `certs/*`, `upload.sh`, `*.private.*`, `releases/`. Run `git check-ignore` before `git add`.
- Commit messages clean, no AI attribution.
- Anything writing the bootloader or partition table needs the owner's approval at that moment.
- `snprintf` only; no VLAs sized from request data; flag leaks and stack buffers crossing tasks.
- Keep `control_logic.c` free of ESP-IDF includes and cover changes with host tests.
