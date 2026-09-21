#pragma once

/* Copy to credentials.h (never committed) and fill in.
 * Secrets may be obfuscated so they are not readable at a glance (NOT encryption):
 * python3 <home-idf>/tools/obfuscate.py  ->  "obf1:..."  (plain values work too) */

#define WIFI_SSID                   ""
#define WIFI_PASS                   ""

/* Authorization header for the /admin endpoints (scripts). Empty locks them. */
#define HEADER_AUTHORIZATION_VALUE  ""

/* Web login: output of home-idf tools/hash_password.py. Empty disables the UI. */
#define AUTH_PASSWORD_ITERATIONS    20000
#define AUTH_PASSWORD_SALT_HEX      ""
#define AUTH_PASSWORD_HASH_HEX      ""

/* ntfy.sh topics (keep them unguessable). Empty disables.
 * NTFY_TOPIC: automatic and manual pump start/stop, maintenance runs, boot.
 * NTFY_ERROR_TOPIC: overheat, sensor failure, unexpected resets and every error log line. */
#define NTFY_TOPIC                  ""
#define NTFY_ERROR_TOPIC            ""

/* Mosquitto on hc-data; MQTT_PASS is MQTT_DEVICE_PASS from the broker's secrets.env.
 * Empty leaves MQTT off, and with it the retained state topic Homebridge subscribes to. */
#define MQTT_USER                   "floor-heating-controller"
#define MQTT_PASS                   ""

/* Authorization header accepted on read-only /admin endpoints (the Homebridge bridge). Unlike
 * HEADER_AUTHORIZATION_VALUE this one cannot change anything, reboot or start an OTA. Empty means
 * read-only clients have no way in. */
#define HEADER_AUTHORIZATION_READONLY_VALUE  ""
