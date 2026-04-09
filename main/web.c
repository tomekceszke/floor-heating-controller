#include <esp_log.h>
#include <esp_system.h>
#include "esp_http_server.h"
#include "config.h"
#include "credentials.h"

static const char *HEADER_AUTHORIZATION_KEY = "Authorization";
static const char *TAG = "WEB";

extern char boot_time[64];
//extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
//extern const uint8_t index_html_gz_end[]   asm("_binary_index_html_gz_end");

void ota();

float read_temp();

bool is_pump_running();

esp_err_t pump_toggle();


httpd_handle_t web_httpd = NULL;

char *bool2string(_Bool b) { return b ? "true" : "false"; }

void check_active_clients(void) {
    int clients[CONFIG_LWIP_MAX_SOCKETS];
    size_t client_count = CONFIG_LWIP_MAX_SOCKETS;
    httpd_get_client_list(web_httpd, &client_count, clients);
    ESP_LOGI(TAG, "Active connections: %d", (int)client_count);
    for (size_t i = 0; i < client_count; i++) {
        ESP_LOGD(TAG, "Client id: %d, socket: %d", (int)i, clients[i]);
    }
}


static bool auth(httpd_req_t *req) {
    const size_t auth_data_len = httpd_req_get_hdr_value_len(req, HEADER_AUTHORIZATION_KEY) + 1;

    if (auth_data_len > 1) {
        char *auth_data = malloc(auth_data_len);
        if (auth_data == NULL) return false;
        const esp_err_t auth_data_header_err = httpd_req_get_hdr_value_str(
            req, HEADER_AUTHORIZATION_KEY, auth_data, auth_data_len);
        if (auth_data_header_err == ESP_OK) {
            if (strcmp(auth_data, HEADER_AUTHORIZATION_VALUE) == 0) {
                free(auth_data);
                ESP_LOGD(TAG, "Authorization successful!");
                return true;
            } else {
                ESP_LOGE(TAG, "Authorization invalid!");
            }
        } else if (auth_data_header_err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Authorization invalid! ESP_ERR_NOT_FOUND");
        } else if (auth_data_header_err == ESP_ERR_INVALID_ARG) {
            ESP_LOGE(TAG, "Authorization invalid! ESP_ERR_INVALID_ARG");
        } else if (auth_data_header_err == ESP_ERR_HTTPD_INVALID_REQ) {
            ESP_LOGE(TAG, "Authorization invalid! ESP_ERR_HTTPD_INVALID_REQ");
        } else if (auth_data_header_err == ESP_ERR_HTTPD_RESULT_TRUNC) {
            ESP_LOGE(TAG, "Authorization invalid! ESP_ERR_HTTPD_RESULT_TRUNC");
        } else {
            ESP_LOGE(TAG, "Authorization invalid!");
        }
        free(auth_data);
    } else {
        ESP_LOGW(TAG, "Authorization header not found");
    }
    return false;
}

static esp_err_t doAuth(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Secure\"");
    httpd_resp_set_status(req, "401 Unauthorized");
    return httpd_resp_send(req, "", 0);
}

//static esp_err_t main_handler(httpd_req_t *req) {
//    if (!auth(req)) {
//        return doAuth(req);
//    }
//    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
//    httpd_resp_set_type(req, "text/html");
//
//    //const char resp[] = "<html><body><h1>Water-controller</h1></body></html>";
//    //return httpd_resp_send(req, resp, strlen(resp));
//    return httpd_resp_send(req, (const char *) index_html_gz_start, (index_html_gz_end - index_html_gz_start));
//
//}

static esp_err_t su_handler(httpd_req_t *req) {
    if (!auth(req)) {
        return doAuth(req);
    }
    const char resp[] = "Upgrade in progress...";
    ota();

    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_send(req, resp, strlen(resp));
}

static esp_err_t reboot_handler(httpd_req_t *req) {
    if (!auth(req)) {
        return doAuth(req);
    }
    ESP_LOGE(TAG, "(not error) Rebooting!");
    esp_restart();
    return ESP_OK;
}


static esp_err_t is_pump_running_handler(httpd_req_t *req) {
    const bool state = is_pump_running();
    const char *state_str = state ? "1" : "0";
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_send(req, state_str, 1);
}

static esp_err_t toggle_pump_handler(httpd_req_t *req) {
    if (!auth(req)) {
        return doAuth(req);
    }

    pump_toggle();
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_send(req, "", strlen(""));
}

static esp_err_t get_hw_status_handler(httpd_req_t *req) {
    size_t free_bytes = esp_get_free_heap_size();
    char data[200];
    snprintf(data, sizeof(data),
            "{\n"
            "   \"up_since\":\"%s\",\n"
            "   \"free_mem_kb\":\"%u\"\n"
            "}",
            boot_time,
            (unsigned int)(free_bytes / 1024));
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, data, strlen(data));
}

static esp_err_t get_status_handler(httpd_req_t *req) {
    char data[200];
    snprintf(data, sizeof(data),
            "{\n"
            "   \"current_temp\": %0.2f,\n"
            "   \"is_pump_running\":%s,\n"
            "   \"pump_start_temp\": %0.2f,\n"
            "   \"pump_stop_temp\": %0.2f\n"
            "}",
            read_temp(),
            bool2string(is_pump_running()),
            PUMP_START_TEMP,
            PUMP_STOP_TEMP
    );
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, data, strlen(data));
}

void web() {
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    //http_config.server_port = HTTPD_PORT;
    // http_config.ctrl_port = HTTPD_PORT;
    http_config.lru_purge_enable = true;
    // http_config.max_uri_handlers = 15;
    http_config.recv_wait_timeout = 5;
    http_config.send_wait_timeout = 5;
    //http_config.stack_size = 16384;

    // httpd_uri_t main_uri = {
    //         .uri = "/",
    //         .method = HTTP_GET,
    //         .handler = main_handler,
    //         .user_ctx = NULL};

    httpd_uri_t su_uri = {
        .uri = "/admin/su",
        .method = HTTP_POST,
        .handler = su_handler,
        .user_ctx = NULL
    };

    httpd_uri_t reboot_uri = {
        .uri = "/admin/reboot",
        .method = HTTP_POST,
        .handler = reboot_handler,
        .user_ctx = NULL
    };

    httpd_uri_t get_hw_status_uri = {
        .uri = "/admin/hw-status",
        .method = HTTP_GET,
        .handler = get_hw_status_handler,
        .user_ctx = NULL
    };

    httpd_uri_t toggle_pump_uri = {
        .uri = "/api/toggle-pump",
        .method = HTTP_POST,
        .handler = toggle_pump_handler,
        .user_ctx = NULL
    };

    httpd_uri_t is_pump_running_uri = {
        .uri = "/api/is-pump-running",
        .method = HTTP_GET,
        .handler = is_pump_running_handler,
        .user_ctx = NULL
    };

    httpd_uri_t get_status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = get_status_handler,
        .user_ctx = NULL
    };


    ESP_LOGI(TAG, "Web server started on port: %d", http_config.server_port);

    if (httpd_start(&web_httpd, &http_config) == ESP_OK) {
        // httpd_register_uri_handler(web_httpd, &main_uri);
        httpd_register_uri_handler(web_httpd, &su_uri);
        httpd_register_uri_handler(web_httpd, &reboot_uri);
        httpd_register_uri_handler(web_httpd, &toggle_pump_uri);
        httpd_register_uri_handler(web_httpd, &is_pump_running_uri);
        httpd_register_uri_handler(web_httpd, &get_hw_status_uri);
        httpd_register_uri_handler(web_httpd, &get_status_uri);
    }
}
