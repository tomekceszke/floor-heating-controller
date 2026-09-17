#pragma once

#include "driver/gpio.h"

/* DEVICE */
#define DEVICE_HOSTNAME                 "fh-controller"

/* GPIO */
#define GPIO_TEMP_SENSOR                GPIO_NUM_4      // DS18B20, 1-Wire
#define GPIO_PUMP                       GPIO_NUM_16     // relay, HIGH = pump on
#define GPIO_LED                        GPIO_NUM_23     // unused

/* CONTROL (the control task never needs the network) */
#define CONTROL_READ_PERIOD_S           5       // DS18B20 conversion takes ~750 ms
#define CONTROL_TASK_CORE               1       // WiFi/lwIP live on core 0
#define CONTROL_TASK_PRIORITY           10
#define SENSOR_FAIL_READS               3       // consecutive bad reads before the fail-safe (pump on)
#define SENSOR_POWER_ON_VALUE_X10       850     // DS18B20 power-on value, treated as a failed read
#define MAINTENANCE_INTERVAL_S          (7 * 24 * 3600)     // idle this long -> anti-seize run
#define MAINTENANCE_RUN_S               60
#define MANUAL_MIN_S                    60
#define MANUAL_MAX_S                    (12 * 3600)

/* THRESHOLDS (0.1 °C; defaults and hard bounds, set from the app) */
#define START_DEFAULT_X10               300
#define STOP_DEFAULT_X10                250
#define CRITICAL_DEFAULT_X10            450
#define START_MIN_X10                   200
#define START_MAX_X10                   600
#define STOP_MIN_X10                    100
#define HYSTERESIS_MIN_X10              20      // stop <= start - 2 °C
#define CRITICAL_MARGIN_MIN_X10         50      // critical >= start + 5 °C
#define CRITICAL_MAX_X10                800

/* EVENTS / HISTORY (RAM only) */
#define EVENTS_RING_SIZE                50
#define HISTORY_PERIOD_S                60
#define HISTORY_SAMPLES                 1440    // 24 h

/* OTA */
#define OTA_FILE                        "floor-heating-controller.bin"
#define OTA_URL                         "https://192.168.11.15:8070/" OTA_FILE

/* LOGGING */
#define LOG_UDP_IP                      "192.168.11.15"
#define LOG_UDP_PORT                    1344

/* HTTPD */
#define HTTPD_ALLOWED_HOSTS             { DEVICE_HOSTNAME, DEVICE_HOSTNAME ".lan" }
#define APP_URL                         "http://192.168.11.241/"

/* HEALTH */
#define HEALTH_VERIFY_TIMEOUT_S         300     // new image: control task alive + WiFi within this, else rollback
#define HEALTH_MAX_UNVERIFIED_BOOTS     3
#define HEALTH_STATS_LOG_PERIOD_S       900

/* NOTIFY */
#define NOTIFY_ERROR_COOLDOWN_S         3600
