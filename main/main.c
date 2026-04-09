#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config/config.h"

static const char *TAG = "MAIN";

void wifi(void);

void ota(void);

void udp_logging(void);

void start_ntp_client(void);

void web(void);

void reset_gpio(void);

void init_sensor(void);

float read_temp(void);

bool is_pump_running(void);

void pump_start(void);

void pump_stop(void);

void check_active_clients(void);

void main_loop(void) {
    TickType_t last_wake_time = xTaskGetTickCount();
    while (1) {
        float curr_temp = read_temp();

        if (INVALID_TEMPERATURE_INDICATOR == curr_temp) {
            ESP_LOGW(TAG, "Invalid read. Retrying in next turn...");
            vTaskDelay((SAMPLE_PERIOD_S * 1000) / portTICK_PERIOD_MS);
            curr_temp = read_temp();
            if (INVALID_TEMPERATURE_INDICATOR == curr_temp) {
                ESP_LOGW(TAG, "Invalid read. Again. Ignored.");
                continue;
            }
        }
        if (curr_temp > PUMP_START_TEMP && !is_pump_running()) {
            ESP_LOGI(TAG, "Pump started");
            pump_start();
        } else if (curr_temp < PUMP_STOP_TEMP && is_pump_running()) {
            ESP_LOGI(TAG, "Pump stopped");
            pump_stop();
        }
        //ESP_LOGI(TAG, "Free heap: %zu", xPortGetFreeHeapSize());
        //check_active_clients();
        xTaskDelayUntil(&last_wake_time, (SAMPLE_PERIOD_S * 1000) / portTICK_PERIOD_MS);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "\n\n");
    ESP_LOGI(TAG, "Connecting to AP...");
    wifi();
    ESP_LOGI(TAG, "Init UDP logging...");
    udp_logging();
    ESP_LOGI(TAG, "Checking OTA...");
    ota();
    ESP_LOGI(TAG, "Starting web server...");
    web();
    ESP_LOGI(TAG, "Setting time...");
    start_ntp_client();
    ESP_LOGI(TAG, "Reset GPIO...");
    reset_gpio();
    ESP_LOGI(TAG, "Init temperature sensor...");
    init_sensor();
    ESP_LOGE(TAG, "(not error) All done! Built: %s %s Free heap size: %zu\n", __DATE__, __TIME__, xPortGetFreeHeapSize());
    main_loop();
}
