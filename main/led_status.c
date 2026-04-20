#include "led_status.h"

#include "led_strip.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LED";

#define LED_GPIO CONFIG_LED_GPIO

// Intensidade dos pixels (0-255). Reduz para não cegar.
#define LED_BRIGHTNESS 30

static led_strip_handle_t s_strip = NULL;
static volatile led_state_t s_state = LED_STATE_BOOTING;
static TaskHandle_t s_task_handle = NULL;

// Cores RGB
#define COLOR_OFF     0,   0,   0
#define COLOR_GREEN   0,   LED_BRIGHTNESS, 0
#define COLOR_BLUE    0,   0,   LED_BRIGHTNESS
#define COLOR_RED     LED_BRIGHTNESS, 0, 0
#define COLOR_YELLOW  LED_BRIGHTNESS, LED_BRIGHTNESS, 0
#define COLOR_ORANGE  LED_BRIGHTNESS, LED_BRIGHTNESS / 3, 0
#define COLOR_CYAN    0, LED_BRIGHTNESS, LED_BRIGHTNESS

static void led_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_strip == NULL) return;
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

static void led_off(void)
{
    if (s_strip == NULL) return;
    led_strip_clear(s_strip);
}

static void led_task(void *arg)
{
    ESP_LOGI(TAG, "Task LED rodando (GPIO %d)", LED_GPIO);
    while (1)
    {
        led_state_t state = s_state;

        switch (state)
        {
        case LED_STATE_BOOTING:
            // Amarelo piscando rápido (200ms)
            led_set(COLOR_YELLOW);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_off();
            vTaskDelay(pdMS_TO_TICKS(200));
            break;

        case LED_STATE_WIFI_CONNECTING:
            // Azul piscando rápido (200ms)
            led_set(COLOR_BLUE);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_off();
            vTaskDelay(pdMS_TO_TICKS(200));
            break;

        case LED_STATE_MQTT_CONNECTING:
            // Azul pulsando lento (1s)
            led_set(COLOR_BLUE);
            vTaskDelay(pdMS_TO_TICKS(1000));
            led_off();
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;

        case LED_STATE_UPS_DISCONNECTED:
            // Laranja piscando lento (1s)
            led_set(COLOR_ORANGE);
            vTaskDelay(pdMS_TO_TICKS(1000));
            led_off();
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;

        case LED_STATE_ALL_OK:
            // Verde fixo
            led_set(COLOR_GREEN);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case LED_STATE_WIFI_FAILED:
            // Vermelho piscando rápido (200ms)
            led_set(COLOR_RED);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_off();
            vTaskDelay(pdMS_TO_TICKS(200));
            break;

        case LED_STATE_OTA_UPDATING:
            // Ciano piscando rápido (150ms)
            led_set(COLOR_CYAN);
            vTaskDelay(pdMS_TO_TICKS(150));
            led_off();
            vTaskDelay(pdMS_TO_TICKS(150));
            break;

        case LED_STATE_AP_MODE:
            // Magenta pulsando lento (1s) — portal de configuração WiFi ativo
            led_set(LED_BRIGHTNESS, 0, LED_BRIGHTNESS);
            vTaskDelay(pdMS_TO_TICKS(1000));
            led_off();
            vTaskDelay(pdMS_TO_TICKS(1000));
            break;

        default:
            vTaskDelay(pdMS_TO_TICKS(500));
            break;
        }
    }
}

void led_status_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10 MHz
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        },
    };

    esp_err_t ret = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar LED strip (GPIO %d): %s", LED_GPIO, esp_err_to_name(ret));
        s_strip = NULL;
    } else {
        ESP_LOGI(TAG, "LED strip inicializado no GPIO %d", LED_GPIO);
        // Teste de hardware: pisca branco 3x para confirmar que o LED funciona
        for (int i = 0; i < 3; i++) {
            led_strip_set_pixel(s_strip, 0, 50, 50, 50);
            led_strip_refresh(s_strip);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_strip_clear(s_strip);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    xTaskCreate(led_task, "led_task", 3072, NULL, 3, &s_task_handle);
    ESP_LOGI(TAG, "Task LED iniciada");
}

void led_status_set(led_state_t state)
{
    s_state = state;
}
