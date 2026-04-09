#include <esp_err.h>
#include <esp_log.h>
#include "driver/gpio.h"
#include "config/config.h"


static const char *TAG = "PUMP";

void reset_gpio(void) {
    gpio_config_t config = {0};
    config.intr_type = GPIO_INTR_DISABLE;
    config.mode = GPIO_MODE_INPUT_OUTPUT;
    config.pin_bit_mask = (1ULL << PUMP_CTRL_OUT_GPIO);
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    if (gpio_config(&config) != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed.");
    }
}

bool is_pump_running(void) {
    return gpio_get_level(PUMP_CTRL_OUT_GPIO);
}

void pump_start(void) {
    gpio_set_level(PUMP_CTRL_OUT_GPIO, 1);
    //gpio_set_level(LED_OUT_GPIO, 0);
}

void pump_stop(void) {
    gpio_set_level(PUMP_CTRL_OUT_GPIO, 0);
    //gpio_set_level(LED_OUT_GPIO, 1);
}

esp_err_t pump_toggle() {
    if (is_pump_running()) {
        pump_stop();
    } else {
        pump_start();
    }
    return ESP_OK;
}
