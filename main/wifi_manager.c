#include "wifi_manager.h"
#include "wifi_config_ui.h"
#include "led_status.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_http_server.h"

static const char *TAG = "WIFI-MGR";

#define NVS_NAMESPACE       "wifi_mgr"
#define NVS_KEY_SSID        "ssid"
#define NVS_KEY_PASS        "pass"
#define BOOT_GPIO           GPIO_NUM_0
#define BOOT_HOLD_MS        3000
#define STA_MAX_RETRY       5
#define AP_MAX_CONN         4
#define AP_CHANNEL          1

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_count = 0;

/* ------------------------------------------------------------------ */
/* NVS helpers                                                          */
/* ------------------------------------------------------------------ */

static bool load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    bool ok = (nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len) == ESP_OK) &&
              (nvs_get_str(h, NVS_KEY_PASS, pass, &pass_len) == ESP_OK);
    nvs_close(h);
    return ok && (ssid[0] != '\0');
}

static void save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_set_str(h, NVS_KEY_SSID, ssid));
    ESP_ERROR_CHECK(nvs_set_str(h, NVS_KEY_PASS, pass));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);
    ESP_LOGI(TAG, "Credenciais salvas. SSID: %s", ssid);
}

static void clear_credentials(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Credenciais WiFi apagadas do NVS.");
}

/* ------------------------------------------------------------------ */
/* BOOT button: segure >= BOOT_HOLD_MS para resetar credenciais        */
/* ------------------------------------------------------------------ */

static bool check_boot_reset(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOOT_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    if (gpio_get_level(BOOT_GPIO) != 0) {
        return false; // botão não pressionado
    }

    ESP_LOGI(TAG, "Botão BOOT pressionado. Aguardando %d ms para confirmar reset...", BOOT_HOLD_MS);
    vTaskDelay(pdMS_TO_TICKS(BOOT_HOLD_MS));

    if (gpio_get_level(BOOT_GPIO) != 0) {
        return false; // solto antes do tempo
    }

    ESP_LOGW(TAG, "Reset de credenciais WiFi confirmado.");
    clear_credentials();
    return true;
}

/* ------------------------------------------------------------------ */
/* STA: conectar com credenciais                                        */
/* ------------------------------------------------------------------ */

static void sta_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < STA_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "Tentando reconectar (%d/%d)...", s_retry_count, STA_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP obtido: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static bool connect_sta(const char *ssid, const char *pass)
{
    s_retry_count = 0;

    esp_event_handler_instance_t h_any, h_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        sta_event_handler, NULL, &h_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        sta_event_handler, NULL, &h_ip));

    wifi_config_t cfg = {0};
    strlcpy((char *)cfg.sta.ssid,     ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Conectando ao WiFi: %s", ssid);
    led_status_set(LED_STATE_WIFI_CONNECTING);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           portMAX_DELAY);

    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, h_any);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, h_ip);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Conectado ao WiFi: %s", ssid);
        led_status_set(LED_STATE_MQTT_CONNECTING);
        return true;
    }

    ESP_LOGE(TAG, "Falha ao conectar ao WiFi: %s", ssid);
    esp_wifi_stop();
    return false;
}

/* ------------------------------------------------------------------ */
/* AP Portal: handlers HTTP                                             */
/* ------------------------------------------------------------------ */

// Decodifica %XX e converte '+' em ' ' in place. Retorna tamanho final.
static int url_decode(char *dst, const char *src, int dst_len)
{
    int i = 0, j = 0;
    while (src[i] && j < dst_len - 1) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = { src[i+1], src[i+2], '\0' };
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
    return j;
}

// Extrai valor de chave em "key=value&..." (URL-encoded) para buf
static bool parse_form_field(const char *body, const char *key, char *buf, int buf_len)
{
    char search[48];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) { buf[0] = '\0'; return false; }
    p += strlen(search);
    const char *end = strchr(p, '&');
    int raw_len = end ? (int)(end - p) : (int)strlen(p);
    char raw[256] = {0};
    if (raw_len >= (int)sizeof(raw)) raw_len = (int)sizeof(raw) - 1;
    memcpy(raw, p, raw_len);
    url_decode(buf, raw, buf_len);
    return true;
}

static esp_err_t handle_get_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, WIFI_CONFIG_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t handle_post_configure(httpd_req_t *req)
{
    char body[512] = {0};
    int received = 0;
    int remaining = req->content_len;
    if (remaining <= 0 || remaining >= (int)sizeof(body)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Corpo inválido");
        return ESP_FAIL;
    }
    while (remaining > 0) {
        int r = httpd_req_recv(req, body + received, remaining);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erro de leitura");
            return ESP_FAIL;
        }
        received += r;
        remaining -= r;
    }
    body[received] = '\0';

    char ssid[33] = {0};
    char pass[65] = {0};
    if (!parse_form_field(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID obrigatório");
        return ESP_FAIL;
    }
    parse_form_field(body, "password", pass, sizeof(pass));

    save_credentials(ssid, pass);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK - Reiniciando...");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static void start_ap_portal(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);

    char ssid[32];
    snprintf(ssid, sizeof(ssid), "UPS-ESP32-%02X%02X%02X", mac[3], mac[4], mac[5]);

    wifi_config_t ap_cfg = {
        .ap = {
            .channel        = AP_CHANNEL,
            .max_connection = AP_MAX_CONN,
            .authmode       = WIFI_AUTH_OPEN,
        },
    };
    strlcpy((char *)ap_cfg.ap.ssid, ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = (uint8_t)strlen(ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Portal AP iniciado. SSID: %s  IP: 192.168.4.1", ssid);
    led_status_set(LED_STATE_AP_MODE);

    httpd_handle_t server = NULL;
    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.server_port = 80;

    if (httpd_start(&server, &http_cfg) == ESP_OK) {
        httpd_uri_t get_root = {
            .uri     = "/",
            .method  = HTTP_GET,
            .handler = handle_get_root,
        };
        httpd_uri_t post_cfg = {
            .uri     = "/configure",
            .method  = HTTP_POST,
            .handler = handle_post_configure,
        };
        httpd_register_uri_handler(server, &get_root);
        httpd_register_uri_handler(server, &post_cfg);
        ESP_LOGI(TAG, "Servidor HTTP do portal iniciado na porta 80.");
    } else {
        ESP_LOGE(TAG, "Falha ao iniciar servidor HTTP do portal.");
    }

    // Fica aqui para sempre — o restart ocorrerá dentro do handle_post_configure
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ------------------------------------------------------------------ */
/* API pública                                                          */
/* ------------------------------------------------------------------ */

bool wifi_manager_start(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    // Verifica botão BOOT para reset de credenciais
    check_boot_reset();

    char ssid[33] = {0};
    char pass[65] = {0};

    if (!load_credentials(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "Sem credenciais salvas. Iniciando portal AP...");
        start_ap_portal(); // bloqueante
    }

    ESP_LOGI(TAG, "Credenciais encontradas. Tentando conectar ao WiFi: %s", ssid);
    if (connect_sta(ssid, pass)) {
        return true;
    }

    // Falhou — apaga credenciais possivelmente erradas e inicia portal
    ESP_LOGW(TAG, "Conexão falhou. Apagando credenciais e iniciando portal AP...");
    clear_credentials();
    start_ap_portal(); // bloqueante

    return false; // nunca alcançado
}
