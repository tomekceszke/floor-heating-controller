#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_log.h"

#include "hi_auth.h"
#include "hi_health.h"
#include "hi_log.h"
#include "hi_notify.h"
#include "hi_ntp.h"
#include "hi_ota.h"
#include "hi_secret.h"
#include "hi_system.h"
#include "hi_wifi.h"

#include "api.h"
#include "config/config.h"
#include "config/credentials.h"
#include "control.h"
#include "events.h"
#include "settings.h"

static const char *TAG = "MAIN";

extern const char ota_cert_pem_start[] asm("_binary_ota_server_cert_15_pem_start");

/* credentials.h values may be obfuscated ("obf1:...", home-idf tools/obfuscate.py) */
static char s_wifi_pass[65];
static char s_admin_header[128];
static char s_ntfy_topic[65];
static char s_ntfy_error_topic[65];

static void reveal_credentials(void)
{
    bool ok = hi_secret_reveal(WIFI_PASS, s_wifi_pass, sizeof(s_wifi_pass))
              & hi_secret_reveal(HEADER_AUTHORIZATION_VALUE, s_admin_header, sizeof(s_admin_header))
              & hi_secret_reveal(NTFY_TOPIC, s_ntfy_topic, sizeof(s_ntfy_topic))
              & hi_secret_reveal(NTFY_ERROR_TOPIC, s_ntfy_error_topic, sizeof(s_ntfy_error_topic));
    if (!ok) ESP_LOGE(TAG, "A credential in credentials.h is malformed");
}

static bool healthy(void)
{
    return control_alive() && hi_wifi_is_connected();
}

static void log_stats(void)
{
    control_status_t c;
    control_status(&c);
    ESP_LOGI(TAG, "heap %u KB (min %u) | rssi %d | pump %s (%s) | temp %d | runtime today %lu s",
             (unsigned) (esp_get_free_heap_size() / 1024), (unsigned) (esp_get_minimum_free_heap_size() / 1024),
             hi_wifi_rssi(), c.state.on ? "on" : "off", ctl_reason_name(c.state.reason),
             c.state.have_temp ? c.state.temp_x10 : -9999, (unsigned long) c.runtime_today_s);
}

void app_main(void)
{
    /* ---- pump control first: nothing below this block may delay or prevent it ---- */
    bool nvs_erased = false;
    hi_nvs_init(&nvs_erased);
    settings_init();
    events_init();
    control_start();

    hi_health_early_boot(HEALTH_MAX_UNVERIFIED_BOOTS);

    /* ---- everything else is optional ---- */
    hi_log_init(&(hi_log_config_t) {.udp_ip = LOG_UDP_IP, .udp_port = LOG_UDP_PORT, .on_error_line = hi_notify_error});
    ESP_LOGW(TAG, "(not error) floor-heating-controller %s, reset: %s", esp_app_get_description()->version,
             hi_reset_reason());
    if (nvs_erased) ESP_LOGE(TAG, "NVS was erased: thresholds reset to defaults");

    reveal_credentials();
    hi_wifi_start(&(hi_wifi_config_t) {.ssid = WIFI_SSID, .password = s_wifi_pass, .hostname = DEVICE_HOSTNAME});
    hi_notify_init(&(hi_notify_config_t) {
        .topic = s_ntfy_topic,
        .error_topic = s_ntfy_error_topic,
        .error_title = "Floor heating error",
        .click_url = APP_URL,
        .error_cooldown_s = NOTIFY_ERROR_COOLDOWN_S,
    });
    hi_ntp_start(&(hi_ntp_config_t) {.servers = {"0.pl.pool.ntp.org", "1.pl.pool.ntp.org", "pool.ntp.org"}});
    hi_ota_init(&(hi_ota_config_t) {.url = OTA_URL, .cert_pem = ota_cert_pem_start, .delete_after = true});
    hi_auth_init(&(hi_auth_config_t) {
        .password_iterations = AUTH_PASSWORD_ITERATIONS,
        .password_salt_hex = AUTH_PASSWORD_SALT_HEX,
        .password_hash_hex = AUTH_PASSWORD_HASH_HEX,
        .admin_header_value = s_admin_header,
    });
    api_start();
    hi_health_start(&(hi_health_config_t) {
        .is_healthy = healthy,
        .log_stats = log_stats,
        .verify_timeout_s = HEALTH_VERIFY_TIMEOUT_S,
        .stats_period_s = HEALTH_STATS_LOG_PERIOD_S,
    });

    if (hi_wifi_wait_connected(60000)) {
        hi_ota_start_background();
    } else {
        ESP_LOGW(TAG, "No WiFi after 60 s: OTA check skipped (pump control running)");
    }
    hi_notify_event_ex("Floor heating", "Started", HI_NOTIFY_PRIO_MIN, NULL);
}
