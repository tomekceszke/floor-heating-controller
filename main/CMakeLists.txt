idf_component_register(SRCS "main.c"
        "wifi.c"
        "log_dispatch.c"
        "log_udp.c"
        "ota.c"
        "ntp.c"
        "web.c"
        "pump.c"
        "temp_sensor.c"
        "notify.c"
        PRIV_REQUIRES spi_flash
        mbedtls
        esp_wifi
        esp_http_client
        esp_http_server
        esp_https_ota
        nvs_flash
        esp_driver_gpio
        esp_timer
        ds18x20
        INCLUDE_DIRS "include"
        "config"
        EMBED_TXTFILES ${project_dir}/certs/ota_server_cert_15.pem
)
