/*

Project UPS ESP32/S3 Tiny Server for UPS (ragtech)

This project is base on the need to monitoring the UPS Ragtech Easy Pro 1200VA (https://ragtech.com.br/produtos/easy-pro-600va-1200va/)


 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

//====

#include "wifi.h"
#include "ups_mqtt.h"
#include "ups.h"

static const char *TAG = "UPS-Srv";

static void setup()
{
}

// Aplicação principal
void app_main(void)
{

    //=================WIFI======================================
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");

    if (lConnectWIFI())
    {
        mqtt_app_start();

        TaskHandle_t taks1Handle = NULL;
        xTaskCreate(ups_start, "Start_UPS", 4096, NULL, 10, &taks1Handle);

        while (1)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
}
