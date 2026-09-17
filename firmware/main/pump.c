#include "driver/gpio.h"
#include "esp_log.h"

#include "config/config.h"
#include "pump.h"

static const char *TAG = "PUMP";

void pump_init(void)
{
    gpio_set_level(GPIO_PUMP, 0);
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << GPIO_PUMP,
        .mode = GPIO_MODE_INPUT_OUTPUT,     // level read back for the status
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&config) != ESP_OK) ESP_LOGE(TAG, "Pump GPIO config failed");
    gpio_set_level(GPIO_PUMP, 0);
}

void pump_set(bool on)
{
    gpio_set_level(GPIO_PUMP, on ? 1 : 0);
}

bool pump_is_on(void)
{
    return gpio_get_level(GPIO_PUMP) == 1;
}
