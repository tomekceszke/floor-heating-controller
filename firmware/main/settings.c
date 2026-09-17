#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs.h"

#include "config/config.h"
#include "settings.h"

static const char *TAG = "SETTINGS";
static const char *NVS_NAMESPACE = "fh_settings";
static const char *NVS_KEY = "v1";

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_write_lock;
static settings_t s_settings = {START_DEFAULT_X10, STOP_DEFAULT_X10, CRITICAL_DEFAULT_X10};

static int16_t clamp(int v, int lo, int hi)
{
    return (int16_t) (v < lo ? lo : (v > hi ? hi : v));
}

/* The start threshold wins: stop and critical move to keep their margins around it. */
static void sanitize(settings_t *s)
{
    s->start_x10 = clamp(s->start_x10, START_MIN_X10, START_MAX_X10);
    s->stop_x10 = clamp(s->stop_x10, STOP_MIN_X10, s->start_x10 - HYSTERESIS_MIN_X10);
    s->critical_x10 = clamp(s->critical_x10, s->start_x10 + CRITICAL_MARGIN_MIN_X10, CRITICAL_MAX_X10);
}

static void apply(const settings_t *s)
{
    portENTER_CRITICAL(&s_mux);
    s_settings = *s;
    portEXIT_CRITICAL(&s_mux);
}

void settings_init(void)
{
    s_write_lock = xSemaphoreCreateMutex();
    settings_t s = {START_DEFAULT_X10, STOP_DEFAULT_X10, CRITICAL_DEFAULT_X10};
    nvs_handle_t h;
    bool loaded = false;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        settings_t stored;
        size_t len = sizeof(stored);
        if (nvs_get_blob(h, NVS_KEY, &stored, &len) == ESP_OK && len == sizeof(stored)) {
            s = stored;
            loaded = true;
        }
        nvs_close(h);
    }
    if (!loaded) ESP_LOGW(TAG, "No stored settings, using defaults");
    sanitize(&s);
    apply(&s);
    ESP_LOGI(TAG, "start %d, stop %d, critical %d (0.1 C)", s.start_x10, s.stop_x10, s.critical_x10);
}

void settings_get(settings_t *out)
{
    portENTER_CRITICAL(&s_mux);
    *out = s_settings;
    portEXIT_CRITICAL(&s_mux);
}

esp_err_t settings_set(settings_t *in_out)
{
    sanitize(in_out);
    xSemaphoreTake(s_write_lock, portMAX_DELAY);
    apply(in_out);      // effective immediately, even if persisting fails
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY, in_out, sizeof(*in_out));
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    xSemaphoreGive(s_write_lock);
    if (err != ESP_OK) ESP_LOGE(TAG, "Saving settings failed: %s", esp_err_to_name(err));
    return err;
}
