#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config/config.h"
#include "maintenance.h"
#include "notify.h"
#include "wifi.h"
#include "log_dispatch.h"

static const char *TAG = "MAIN";

void ota(void);

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
    static bool s_ready_notified = false;
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
        if (!s_ready_notified) {
            notify_device_ready(curr_temp, is_pump_running());
            s_ready_notified = true;
        }
        if (curr_temp > PUMP_START_TEMP && !is_pump_running()) {
            ESP_LOGI(TAG, "Pump started");
            pump_start();
            notify_pump_started(curr_temp);
        } else if (curr_temp < PUMP_STOP_TEMP && is_pump_running()) {
            ESP_LOGI(TAG, "Pump stopped");
            pump_stop();
            notify_pump_stopped(curr_temp);
        }
        maintenance_check(is_pump_running(), curr_temp);
        //ESP_LOGI(TAG, "Free heap: %zu", xPortGetFreeHeapSize());
        //check_active_clients();
        xTaskDelayUntil(&last_wake_time, (SAMPLE_PERIOD_S * 1000) / portTICK_PERIOD_MS);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "\n\n");
    notify_init();
    ESP_LOGI(TAG, "Connecting to AP...");
    wifi();
    ESP_LOGI(TAG, "Init log dispatch...");
    log_dispatch_init();
    ESP_LOGI(TAG, "Checking OTA...");
    ota();
    ESP_LOGI(TAG, "Starting web server...");
    web();
    ESP_LOGI(TAG, "Setting time...");
    start_ntp_client();
    notify_device_started();
    ESP_LOGI(TAG, "Reset GPIO...");
    reset_gpio();
    ESP_LOGI(TAG, "Init temperature sensor...");
    init_sensor();
    ESP_LOGE(TAG, "(not error) All done! Built: %s %s Free heap size: %zu\n", __DATE__, __TIME__, xPortGetFreeHeapSize());
    main_loop();
}
