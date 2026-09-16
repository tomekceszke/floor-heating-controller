#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config/config.h"
#include "ds18x20.h"

static const char *TAG = "TEMP_SENSOR";

onewire_addr_t sensor_addr = {0};

void init_sensor() {
    size_t num_devices = 0;
    ds18x20_scan_devices(TEMP_SENSOR_IN_GPIO, &sensor_addr, 1, &num_devices);
    TickType_t last_wake_time = xTaskGetTickCount();
    while (num_devices < 1) {
        ESP_LOGE(TAG, "There is no 1-Wire device available on the bus. Scanning...");
        xTaskDelayUntil(&last_wake_time, (TEMP_SENSOR_SCAN_RETRY_S * 1000) / portTICK_PERIOD_MS);
        ds18x20_scan_devices(TEMP_SENSOR_IN_GPIO, &sensor_addr, 1, &num_devices);
    }
    ESP_LOGI(TAG, "Found DS18B20 sensor.");
}

float read_temp() {
    float temperature = INVALID_TEMPERATURE_INDICATOR;
    const esp_err_t ret = ds18x20_measure_and_read(TEMP_SENSOR_IN_GPIO, sensor_addr, &temperature);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error during conversion");
        return INVALID_TEMPERATURE_INDICATOR;
    }
    ESP_LOGI(TAG, "Read temperature: %.2f°C", temperature);
    return temperature;
}