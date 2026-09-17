#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"

#include "hi_auth.h"
#include "hi_httpd.h"

#include "api.h"
#include "config/config.h"
#include "control.h"
#include "events.h"
#include "history.h"
#include "settings.h"

static const char *TAG = "API";

extern const uint8_t login_html_gz_start[] asm("_binary_login_html_gz_start");
extern const uint8_t login_html_gz_end[] asm("_binary_login_html_gz_end");
extern const uint8_t app_html_gz_start[] asm("_binary_app_html_gz_start");
extern const uint8_t app_html_gz_end[] asm("_binary_app_html_gz_end");
extern const uint8_t icon_png_start[] asm("_binary_apple_touch_icon_png_start");
extern const uint8_t icon_png_end[] asm("_binary_apple_touch_icon_png_end");
extern const char manifest_start[] asm("_binary_manifest_webmanifest_start");

static double celsius(int16_t x10)
{
    return x10 / 10.0;
}

static const char *manual_name(ctl_manual_t m)
{
    return m == CTL_MANUAL_START ? "start" : m == CTL_MANUAL_STOP ? "stop" : "none";
}

static cJSON *status_json(void)
{
    control_status_t c;
    control_status(&c);
    const ctl_state_t *st = &c.state;
    settings_t s;
    settings_get(&s);
    cJSON *root = cJSON_CreateObject();

    cJSON *pump = cJSON_AddObjectToObject(root, "pump");
    cJSON_AddBoolToObject(pump, "on", st->on);
    cJSON_AddStringToObject(pump, "reason", ctl_reason_name(st->reason));
    cJSON_AddNumberToObject(pump, "since", (double) events_unix_time(st->changed_s));
    cJSON_AddNumberToObject(pump, "state_s", (double) (c.now_mono_s - st->changed_s));
    cJSON_AddStringToObject(pump, "manual", manual_name(st->manual));
    cJSON_AddNumberToObject(pump, "manual_left_s", st->manual == CTL_MANUAL_NONE ? 0
                                                   : (double) (st->manual_until_s - c.now_mono_s));
    cJSON_AddBoolToObject(pump, "maintenance", st->maintenance);
    cJSON_AddNumberToObject(pump, "idle_s", st->on ? 0 : (double) (c.now_mono_s - st->last_on_s));
    cJSON_AddBoolToObject(pump, "control_alive", c.now_mono_s - c.heartbeat_mono_s <= 3 * CONTROL_READ_PERIOD_S);

    cJSON *temp = cJSON_AddObjectToObject(root, "temp");
    if (st->have_temp) cJSON_AddNumberToObject(temp, "c", celsius(st->temp_x10));
    else cJSON_AddNullToObject(temp, "c");
    cJSON_AddNumberToObject(temp, "age_s", st->have_temp ? (double) (c.now_mono_s - st->temp_s) : -1);
    cJSON_AddNumberToObject(temp, "failures", st->failures);
    cJSON_AddBoolToObject(temp, "sensor_failed", st->sensor_failed);
    cJSON_AddBoolToObject(temp, "overheat", st->overheat);

    cJSON *today = cJSON_AddObjectToObject(root, "today");
    cJSON_AddNumberToObject(today, "runtime_s", c.runtime_today_s);
    cJSON_AddNumberToObject(today, "starts", c.starts_today);
    cJSON *boot = cJSON_AddObjectToObject(root, "since_boot");
    cJSON_AddNumberToObject(boot, "runtime_s", c.runtime_boot_s);
    cJSON_AddNumberToObject(boot, "starts", c.starts_boot);

    cJSON *set = cJSON_AddObjectToObject(root, "settings");
    cJSON_AddNumberToObject(set, "start_c", celsius(s.start_x10));
    cJSON_AddNumberToObject(set, "stop_c", celsius(s.stop_x10));
    cJSON_AddNumberToObject(set, "critical_c", celsius(s.critical_x10));
    cJSON_AddNumberToObject(set, "start_min_c", celsius(START_MIN_X10));
    cJSON_AddNumberToObject(set, "start_max_c", celsius(START_MAX_X10));
    cJSON_AddNumberToObject(set, "stop_min_c", celsius(STOP_MIN_X10));
    cJSON_AddNumberToObject(set, "hysteresis_min_c", celsius(HYSTERESIS_MIN_X10));
    cJSON_AddNumberToObject(set, "critical_margin_min_c", celsius(CRITICAL_MARGIN_MIN_X10));
    cJSON_AddNumberToObject(set, "critical_max_c", celsius(CRITICAL_MAX_X10));
    cJSON_AddNumberToObject(set, "maintenance_interval_s", MAINTENANCE_INTERVAL_S);
    cJSON_AddNumberToObject(set, "maintenance_run_s", MAINTENANCE_RUN_S);
    cJSON_AddNumberToObject(set, "manual_max_s", MANUAL_MAX_S);

    hi_httpd_add_system_status(cJSON_AddObjectToObject(root, "system"));
    return root;
}

static bool admin_ok(httpd_req_t *req, esp_err_t *result)
{
    if (hi_auth_admin_header_valid(req)) return true;
    httpd_resp_set_status(req, "401 Unauthorized");
    *result = httpd_resp_send(req, "", 0);
    return false;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!hi_httpd_guard(req, HI_GUARD_SESSION, NULL, &result)) return result;
    return hi_httpd_send_json(req, "200 OK", status_json());
}

static esp_err_t admin_status_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!admin_ok(req, &result)) return result;
    return hi_httpd_send_json(req, "200 OK", status_json());
}

static esp_err_t events_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!hi_httpd_guard(req, HI_GUARD_SESSION, NULL, &result)) return result;
    static event_t list[EVENTS_RING_SIZE];      // httpd runs one handler at a time
    size_t n = events_recent(list, EVENTS_RING_SIZE);

    static const char *const TYPES[] = {"pump", "overheat", "sensor", "manual_end"};
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "events");
    for (size_t i = 0; i < n; i++) {
        const event_t *e = &list[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "ts", (double) events_unix_time(e->mono_s));
        cJSON_AddStringToObject(o, "type", TYPES[e->type]);
        cJSON_AddBoolToObject(o, "on", e->on);
        cJSON_AddStringToObject(o, "reason", ctl_reason_name((ctl_reason_t) e->reason));
        cJSON_AddStringToObject(o, "prev_reason", ctl_reason_name((ctl_reason_t) e->prev_reason));
        if (e->has_temp) cJSON_AddNumberToObject(o, "temp_c", celsius(e->temp_x10));
        else cJSON_AddNullToObject(o, "temp_c");
        cJSON_AddStringToObject(o, "detail", e->detail);
        cJSON_AddItemToArray(arr, o);
    }
    return hi_httpd_send_json(req, "200 OK", root);
}

/* {"period_s":60,"newest":<unix or 0>,"newest_age_s":12,"t":[312,null,...],"p":"0110..."}, oldest first, streamed. */
static esp_err_t history_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!hi_httpd_guard(req, HI_GUARD_SESSION, NULL, &result)) return result;
    static int16_t temps[HISTORY_SAMPLES];
    static bool pump[HISTORY_SAMPLES];
    int64_t newest_mono_s = 0;
    size_t n = history_copy(temps, pump, HISTORY_SAMPLES, &newest_mono_s);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char buf[512];
    int len = snprintf(buf, sizeof(buf), "{\"period_s\":%d,\"newest\":%lld,\"newest_age_s\":%lld,\"t\":[",
                       HISTORY_PERIOD_S, (long long) (n ? events_unix_time(newest_mono_s) : 0),
                       (long long) (n ? events_mono_s() - newest_mono_s : 0));
    for (size_t i = 0; i < n; i++) {
        if (len > (int) sizeof(buf) - 16) {
            if (httpd_resp_send_chunk(req, buf, len) != ESP_OK) return ESP_FAIL;
            len = 0;
        }
        const char *sep = i ? "," : "";
        len += temps[i] == HISTORY_NO_READING ? snprintf(buf + len, sizeof(buf) - len, "%snull", sep)
                                              : snprintf(buf + len, sizeof(buf) - len, "%s%d", sep, temps[i]);
    }
    len += snprintf(buf + len, sizeof(buf) - len, "],\"p\":\"");
    for (size_t i = 0; i < n; i++) {
        if (len > (int) sizeof(buf) - 8) {
            if (httpd_resp_send_chunk(req, buf, len) != ESP_OK) return ESP_FAIL;
            len = 0;
        }
        buf[len++] = pump[i] ? '1' : '0';
    }
    len += snprintf(buf + len, sizeof(buf) - len, "\"}");
    if (httpd_resp_send_chunk(req, buf, len) != ESP_OK) return ESP_FAIL;
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* {"state":"start"|"stop"|"auto","minutes":60} */
static esp_err_t pump_command(httpd_req_t *req, const char *who)
{
    esp_err_t result;
    cJSON *body = hi_httpd_read_json(req, &result);
    if (body == NULL) return result;
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(body, "state");
    const cJSON *minutes = cJSON_GetObjectItemCaseSensitive(body, "minutes");
    ctl_manual_t manual = CTL_MANUAL_NONE;
    bool ok = cJSON_IsString(state);
    if (ok && strcmp(state->valuestring, "start") == 0) manual = CTL_MANUAL_START;
    else if (ok && strcmp(state->valuestring, "stop") == 0) manual = CTL_MANUAL_STOP;
    else if (!ok || strcmp(state->valuestring, "auto") != 0) ok = false;
    bool minutes_ok = manual == CTL_MANUAL_NONE || (cJSON_IsNumber(minutes) && minutes->valuedouble > 0);
    uint32_t duration_s = 0;
    if (minutes_ok && manual != CTL_MANUAL_NONE) {
        duration_s = minutes->valuedouble * 60 > MANUAL_MAX_S ? MANUAL_MAX_S : (uint32_t) (minutes->valuedouble * 60);
    }
    cJSON_Delete(body);
    if (!ok) return hi_httpd_send_error(req, "400 Bad Request", "state");
    if (!minutes_ok) return hi_httpd_send_error(req, "400 Bad Request", "minutes");

    if (control_manual(manual, duration_s, who) == CONTROL_REFUSED_OVERHEAT) {
        return hi_httpd_send_error(req, "409 Conflict", "overheat");
    }
    return hi_httpd_send_json(req, "200 OK", status_json());
}

static esp_err_t pump_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!hi_httpd_guard(req, HI_GUARD_MUTATION, NULL, &result)) return result;
    char ip[16];
    hi_httpd_client_ip(req, ip, sizeof(ip));
    return pump_command(req, ip);
}

static esp_err_t admin_pump_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!admin_ok(req, &result)) return result;
    return pump_command(req, "admin");
}

static void json_celsius(const cJSON *obj, const char *key, int16_t *field)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item) && item->valuedouble > -100 && item->valuedouble < 200) {
        double v = item->valuedouble * 10;
        *field = (int16_t) (v < 0 ? v - 0.5 : v + 0.5);
    }
}

/* Partial {"start_c":30,"stop_c":25,"critical_c":45}, clamped. */
static esp_err_t settings_handler(httpd_req_t *req)
{
    esp_err_t result;
    if (!hi_httpd_guard(req, HI_GUARD_MUTATION, NULL, &result)) return result;
    cJSON *body = hi_httpd_read_json(req, &result);
    if (body == NULL) return result;
    settings_t s;
    settings_get(&s);
    json_celsius(body, "start_c", &s.start_x10);
    json_celsius(body, "stop_c", &s.stop_x10);
    json_celsius(body, "critical_c", &s.critical_x10);
    cJSON_Delete(body);

    esp_err_t err = settings_set(&s);
    control_settings_changed();
    ESP_LOGW(TAG, "(not error) Settings: start %d, stop %d, critical %d (0.1 C)", s.start_x10, s.stop_x10,
             s.critical_x10);
    if (err != ESP_OK) return hi_httpd_send_error(req, "500 Internal Server Error", "save_failed");
    return hi_httpd_send_json(req, "200 OK", status_json());
}

void api_start(void)
{
    static const char *const hosts[] = HTTPD_ALLOWED_HOSTS;
    static hi_httpd_ui_t ui;
    ui = (hi_httpd_ui_t) {
        .login_html_gz = {login_html_gz_start, login_html_gz_end},
        .app_html_gz = {app_html_gz_start, app_html_gz_end},
        .icon_png = {icon_png_start, icon_png_end},
        .manifest_json = manifest_start,
    };
    if (hi_httpd_start(&(hi_httpd_config_t) {
            .allowed_hosts = hosts,
            .allowed_hosts_count = sizeof(hosts) / sizeof(hosts[0]),
            .ui = &ui,
            .max_uri_handlers = 20,
        }) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server not started (pump control unaffected)");
        return;
    }
    const httpd_uri_t routes[] = {
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler},
        {.uri = "/api/events", .method = HTTP_GET, .handler = events_handler},
        {.uri = "/api/history", .method = HTTP_GET, .handler = history_handler},
        {.uri = "/api/pump", .method = HTTP_POST, .handler = pump_handler},
        {.uri = "/api/settings", .method = HTTP_POST, .handler = settings_handler},
        {.uri = "/admin/pump", .method = HTTP_POST, .handler = admin_pump_handler},
        {.uri = "/admin/status", .method = HTTP_GET, .handler = admin_status_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        hi_httpd_register(&routes[i]);
    }
}
