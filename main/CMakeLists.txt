idf_component_register(SRCS "web_monitor.c" "web_status.c" "main.c" "calibration.c"
                            "calibration_model.c" "calibration_protocol.c" "calibration_web.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio esp_driver_pcnt esp_driver_dac esp_timer
                                     esp_wifi esp_event esp_netif esp_http_server nvs_flash
                       EMBED_FILES "web/index.html" "web/dashboard.css" "web/dashboard.js"
                                   "web/calibration.html" "web/calibration.css" "web/calibration.js")

