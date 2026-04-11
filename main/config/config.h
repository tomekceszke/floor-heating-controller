#pragma once

/* WIFI */
#define WIFI_RETRY_DELAY_S              60
#define WIFI_MAXIMUM_RETRY              5

/* NTP */
#define NTP_MAX_ATTEMPTS                3
#define NTP_RETRY_DELAY_S               5

/* OTA */
#define OTA_URL                          "https://192.168.11.15:8070/floor-heating-controller.bin"

/* LOGGING */
#define LOG_UDP_IP                      "192.168.11.15"
#define LOG_UDP_PORT                    1344


/* GPIO */
#define TEMP_SENSOR_IN_GPIO            GPIO_NUM_4
#define LED_OUT_GPIO                   GPIO_NUM_23
#define PUMP_CTRL_OUT_GPIO             GPIO_NUM_16
#define TEMP_SENSOR_SCAN_RETRY_S       30

/* NOTIFICATIONS */
#define NOTIFY_ERROR_COOLDOWN_S         3600    // suppress duplicate errors within this window
#define NOTIFY_ERROR_QUEUE_SIZE         4       // error notification queue depth (drops when full)

/* APP */
#define INVALID_TEMPERATURE_INDICATOR   (85.00)
#define SAMPLE_PERIOD_S                 60
#define PUMP_START_TEMP                 (30.00)
#define PUMP_STOP_TEMP                  (25.00)
