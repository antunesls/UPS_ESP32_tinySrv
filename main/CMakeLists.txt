idf_component_register(SRCS "main.c" "ups_mqtt.c" "ups.c" "wifi.c" "wifi_manager.c" "led_status.c" "web_server.c" "log_buffer.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES usb esp_wifi nvs_flash mqtt led_strip esp_http_server app_update esp_timer driver
                    )
