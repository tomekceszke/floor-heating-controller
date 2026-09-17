# Migration to firmware 2.x (bootloader and partition table)

## Why

| | Legacy firmware (production until the migration) | Firmware 2.x |
|---|---|---|
| Bootloader | ESP-IDF 5.4.x (5.4.1 in the 2025 config; the migrator check records the real one) | ESP-IDF 5.4.2, native app rollback |
| Table | stock `partitions_two_ota`: factory 1M @0x10000, ota_0 1M @0x110000, ota_1 1M @0x210000 | ota_0 2M @0x10000, ota_1 1.875M @0x210000, coredump 64K @0x3F0000 |
| Image | 864 KB (84 % of a slot) | 951 KB: 93 % of a legacy slot, 48 % of ota_1 |

nvs 0x9000, otadata 0xd000 and phy 0xf000 keep their offsets; flash is DIO / 40 MHz / 4 MB in both. Same layout
and procedure as water-controller (2026-09-15) and gate-controller (2026-09-15).

The legacy bootloader would still boot 2.x (home-idf's `hi_health` rolls back in software), but the image would leave
no room in a 1 MB slot and the device would stay the only one without a coredump partition and native rollback.

## How

`migrator/` is a one-shot image built against the legacy table (must fit 1 MB, 812 KB) with the 2.x bootloader and
table embedded (`home-idf/tools/build_migrator.sh`). While it runs, **the pump is on** (GPIO16 HIGH): without
temperature control, circulating is safe at any water temperature.

1. The legacy firmware downloads `floor-heating-controller.bin` at boot or on `POST /admin/su`; at that moment the file
   is the migrator. It deletes the file after installing it.
2. The migrator copies itself to 0x210000 if it does not run from there, and reboots.
3. `GET /migrator` shows the checks: blob sha256, flash settings of the bootloader in flash, **ESP-IDF version of the
   bootloader in flash**, table MD5, nvs/otadata/phy offsets, ota_1 = running image, last reset not a brownout.
   Nothing is written unless all pass (stage `ready`).
4. `POST /migrator/commit` (admin header): partition table, then bootloader, each verified with up to 3 retries and a
   restore of the old content on failure.
5. The new bootloader boots the migrator, which downloads firmware 2.x (same file name) into ota_0.
6. Firmware 2.x verifies itself (control task alive and WiFi within 300 s, at most 3 unverified resets), otherwise it
   rolls back to the migrator.

## Risks (owner decision 2026-09-17: no spare-board rehearsal)

- The legacy firmware has no rollback: if the migrator image failed to boot, the board would loop until reflashed.
  The same migrator code (home-idf `hi_migrator`) runs on water and gate since 2026-09-15; this build differs in the
  pump GPIO, names and the extra bootloader-version line.
- A power cut during the table or bootloader write (well under a second) bricks the board.
- Recovery for both: USB (CP2102 on the board), download mode IO0 + EN, `firmware/build.sh -p /dev/cu.usbserial-0001 flash`
  (writes bootloader, table and app). NVS survives; thresholds would be defaults anyway.
- Do not open a serial monitor during the commit: DTR/RTS resets the board.

## Production procedure

1. Credentials: `firmware/main/config/credentials.h` needs `AUTH_PASSWORD_*` (web sign-in) in addition to the legacy
   values; the admin header stays the same, so the legacy `/admin/su` and the migrator commit use the same value.
2. `tools/build_release.sh`; record both sha256 values.
3. Preflight: `GET http://192.168.11.247/api/status` (legacy: temperature, pump state), note the time.
4. `scp releases/floor-heating-migrator.bin 192.168.11.15:apps/ota-server/builds/floor-heating-controller.bin`,
   start the OTA server on .15, `nc -ul 1344` for the log.
5. Legacy `POST /admin/su` with the Authorization header (runs synchronously, the device restarts into the migrator).
6. `GET http://192.168.11.247/migrator` until stage `ready`; read the bootloader version line. **Stop if any check fails.**
7. `scp releases/floor-heating-controller.bin 192.168.11.15:apps/ota-server/builds/floor-heating-controller.bin`
   **before** the commit.
8. `POST /migrator/commit` with the admin header; firmware 2.x follows within a minute.
9. Verify: `/admin/hw-status` shows 2.0.0; app sign-in works; `system.partition = ota_0`, `bootloader_idf = v5.4.2`,
   `pending_verify` false after the health window; temperature plausible and the pump follows the thresholds;
   "Started" on the ntfy topic; OTA server stopped.

## Production log

Not migrated yet. 2026-09-17 10:5x read-only check: legacy answers on 192.168.11.247 (the docs said .241), up since
2026-08-27 18:45, water 20.4 °C, pump off, last pump start 2026-09-10 18:47.
