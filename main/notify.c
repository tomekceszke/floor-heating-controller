#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "notify.h"
#include "wifi.h"
#include "config/config.h"
#include "config/credentials.h"

static const char *TAG = "NOTIFY";

#define ERROR_MSG_SIZE 128

static QueueHandle_t s_error_queue = NULL;
static volatile bool s_error_suppressed = false;

static struct {
    uint32_t hash;
    int64_t last_sent_ms;
} s_dedup[8];

static uint32_t djb2(const char *s)
{
    uint32_t h = 5381;
    int c;
    while ((c = (unsigned char)*s++)) {
        h = h * 33 ^ (uint32_t)c;
    }
    return h;
}

static bool dedup_should_send(const char *msg)
{
    // Strip "X (timestamp) " prefix so the same error at different times hashes identically
    const char *content = strchr(msg, ')');
    if (content && *(content + 1) == ' ') {
        content += 2;
    } else {
        content = msg;
    }
    uint32_t h = djb2(content);
    int64_t now_ms = esp_timer_get_time() / 1000;
    int oldest_idx = 0;

    for (int i = 0; i < 8; i++) {
        if (s_dedup[i].hash == h) {
            if ((now_ms - s_dedup[i].last_sent_ms) < ((int64_t)NOTIFY_ERROR_COOLDOWN_S * 1000)) {
                return false;
            }
            s_dedup[i].last_sent_ms = now_ms;
            return true;
        }
        if (s_dedup[i].last_sent_ms < s_dedup[oldest_idx].last_sent_ms) {
            oldest_idx = i;
        }
    }
    s_dedup[oldest_idx].hash = h;
    s_dedup[oldest_idx].last_sent_ms = now_ms;
    return true;
}

static void send_notification_to(const char *topic, const char *title, const char *message)
{
    if (topic == NULL || topic[0] == '\0') return;
    char url[64];
    snprintf(url, sizeof(url), "https://ntfy.sh/%s", topic);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGW(TAG, "Failed to init HTTP client");
        return;
    }
    esp_http_client_set_header(client, "Title", title);
    esp_http_client_set_post_field(client, message, (int)strlen(message));
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Notification failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

static void error_notify_task(void *pvParameters)
{
    char msg[ERROR_MSG_SIZE];
    while (1) {
        if (xQueueReceive(s_error_queue, msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!wifi_is_connected()) {
            continue;
        }
        if (!dedup_should_send(msg)) {
            continue;
        }
        send_notification_to(NTFY_ERROR_TOPIC, "Device error", msg);
    }
}

void notify_init(void)
{
    if (NTFY_ERROR_TOPIC[0] == '\0') return;
    s_error_queue = xQueueCreate(NOTIFY_ERROR_QUEUE_SIZE, ERROR_MSG_SIZE);
    if (s_error_queue == NULL) {
        ESP_LOGW(TAG, "Failed to create error notification queue");
        return;
    }
    xTaskCreate(error_notify_task, "error_notify", 4096, NULL, 5, NULL);
}

void notify_error_suppress(bool suppress)
{
    s_error_suppressed = suppress;
}

void notify_queue_error(const char *msg)
{
    if (s_error_queue == NULL) return;
    if (s_error_suppressed) return;
    char buf[ERROR_MSG_SIZE];
    snprintf(buf, sizeof(buf), "%s", msg);
    xQueueSend(s_error_queue, buf, 0);
}

void notify_pump_started(float temp)
{
    char msg[48];
    snprintf(msg, sizeof(msg), "Temperature: %.1f\xc2\xb0""C", temp);
    send_notification_to(NTFY_TOPIC, "Pump started", msg);
}

void notify_pump_stopped(float temp)
{
    char msg[48];
    snprintf(msg, sizeof(msg), "Temperature: %.1f\xc2\xb0""C", temp);
    send_notification_to(NTFY_TOPIC, "Pump stopped", msg);
}

void notify_device_started(void)
{
    send_notification_to(NTFY_TOPIC, "Device started", "Floor heating controller started");
}

void notify_device_ready(float temp, bool pump_running)
{
    char msg[64];
    snprintf(msg, sizeof(msg), "Temperature: %.1f\xc2\xb0""C\nPump: %s",
             temp, pump_running ? "ON" : "OFF");
    send_notification_to(NTFY_TOPIC, "Device ready", msg);
}

void notify_maintenance_started(void)
{
    send_notification_to(NTFY_TOPIC, "Maintenance run", "Weekly pump exercise run started");
}
