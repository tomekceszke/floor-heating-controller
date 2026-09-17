idf_component_register(
    SRCS "main.c"
         "control_logic.c"
         "control.c"
         "pump.c"
         "temp_sensor.c"
         "settings.c"
         "events.c"
         "history.c"
         "api.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_gpio esp_wifi esp_timer esp_app_format nvs_flash json esp_http_server
    EMBED_TXTFILES ${project_dir}/certs/ota_server_cert_15.pem ${project_dir}/web/manifest.webmanifest
    EMBED_FILES ${project_dir}/web/apple-touch-icon.png)

home_idf_login_page(${COMPONENT_LIB} NAME "fh-controller" ACCENT "#f08a4b")
home_idf_app_page(${COMPONENT_LIB} SRC ${project_dir}/web/app.html NAME "fh-controller")
