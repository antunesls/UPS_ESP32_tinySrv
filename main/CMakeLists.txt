idf_component_register(SRCS "main.c" "ups_mqtt.c" "ups.c" "wifi.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES usb esp_wifi nvs_flash  mqtt
                    )
