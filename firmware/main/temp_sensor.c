#include <math.h>
#include "esp_log.h"
#include "ds18x20.h"

#include "config/config.h"
#include "temp_sensor.h"

static const char *TAG = "TEMP";

static onewire_addr_t s_addr;
static bool s_found;
static uint8_t s_failures;     // a sensor swapped on the bus has a new address: rescan after a few failures

bool temp_sensor_read(int16_t *temp_x10)
{
    if (!s_found) {
        size_t found = 0;
        if (ds18x20_scan_devices(GPIO_TEMP_SENSOR, &s_addr, 1, &found) != ESP_OK || found < 1) {
            ESP_LOGW(TAG, "No DS18B20 on the 1-Wire bus");
            return false;
        }
        s_found = true;
        ESP_LOGW(TAG, "(not error) DS18B20 found");
    }
    float t = 0;
    esp_err_t err = ds18x20_measure_and_read(GPIO_TEMP_SENSOR, s_addr, &t);
    int value = err == ESP_OK ? (int) lroundf(t * 10) : 0;
    if (err != ESP_OK || value == SENSOR_POWER_ON_VALUE_X10 || value < -550 || value > 1250) {
        ESP_LOGW(TAG, "Read failed: %s (%.2f)", esp_err_to_name(err), t);
        if (++s_failures >= SENSOR_FAIL_READS) {
            s_found = false;
            s_failures = 0;
        }
        return false;
    }
    s_failures = 0;
    *temp_x10 = (int16_t) value;
    return true;
}
