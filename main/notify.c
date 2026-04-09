#include "notify.h"
#include "config/credentials.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "notify";

static void send_notification(const char *title, const char *message) {
    char url[64];
    snprintf(url, sizeof(url), "https://ntfy.sh/%s", NTFY_TOPIC);

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
    esp_http_client_set_post_field(client, message, (int) strlen(message));
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Notification failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

void notify_pump_started(float temp) {
    char msg[48];
    snprintf(msg, sizeof(msg), "Temperature: %.1f\xc2\xb0""C", temp);
    send_notification("Pump started", msg);
}

void notify_pump_stopped(float temp) {
    char msg[48];
    snprintf(msg, sizeof(msg), "Temperature: %.1f\xc2\xb0""C", temp);
    send_notification("Pump stopped", msg);
}

void notify_device_ready(void) {
    send_notification("Device ready", "Floor heating controller started");
}
