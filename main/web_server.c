#include "web_server.h"
#include "web_ui.h"
#include "ups.h"
#include "led_status.h"

#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "sdkconfig.h"

static const char *TAG = "WebSrv";
static httpd_handle_t s_server = NULL;

// ── GET / ────────────────────────────────────────────────────────────────────

static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=UTF-8");
    httpd_resp_send(req, WEB_UI_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ── GET /api/metrics ──────────────────────────────────────────────────────────

static esp_err_t handler_metrics(httpd_req_t *req)
{
    char buf[512];
    ups_metricts_t m = (ups_metricts_t)g_ups_metrics;  // cópia atômica local
    snprintf(buf, sizeof(buf),
        "{"
        "\"ups_connected\":%s,"
        "\"power_out_percent\":%.1f,"
        "\"current_out\":%.2f,"
        "\"voltage_out\":%.1f,"
        "\"voltage_in\":%.1f,"
        "\"power_out\":%.1f,"
        "\"power_in\":%.1f,"
        "\"energy_out\":%.2f,"
        "\"energy_in\":%.2f,"
        "\"temperature\":%.1f,"
        "\"battery_state\":%.1f,"
        "\"battery_voltage\":%.2f,"
        "\"frequency\":%.1f"
        "}",
        g_ups_connected ? "true" : "false",
        m.power_out_percent, m.current_out,
        m.voltage_out, m.voltage_in,
        m.power_out, m.power_in,
        m.energy_out, m.energy_in,
        m.temperature, m.battery_state,
        m.battery_voltage, m.frequency);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ── GET /api/info ─────────────────────────────────────────────────────────────

static esp_err_t handler_info(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();

    uint64_t uptime_s = esp_timer_get_time() / 1000000ULL;

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{"
        "\"version\":\"%s\","
        "\"project\":\"%s\","
        "\"partition\":\"%s\","
        "\"uptime_s\":%llu"
        "}",
        app->version,
        app->project_name,
        running ? running->label : "unknown",
        uptime_s);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ── POST /ota ─────────────────────────────────────────────────────────────────

static esp_err_t handler_ota(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA: iniciando upload (%d bytes)", req->content_len);

    led_status_set(LED_STATE_OTA_UPDATING);

    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (update_part == NULL) {
        ESP_LOGE(TAG, "OTA: nenhuma partição de atualização disponível");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        led_status_set(LED_STATE_ALL_OK);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA: gravando em partição '%s'", update_part->label);

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA: esp_ota_begin falhou: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        led_status_set(LED_STATE_ALL_OK);
        return ESP_FAIL;
    }

    static char ota_buf[1024];
    int remaining = req->content_len;
    int received;

    while (remaining > 0) {
        int to_read = (remaining < (int)sizeof(ota_buf)) ? remaining : (int)sizeof(ota_buf);
        received = httpd_req_recv(req, ota_buf, to_read);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "OTA: erro ao receber dados");
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            led_status_set(LED_STATE_ALL_OK);
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, ota_buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA: esp_ota_write falhou: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            led_status_set(LED_STATE_ALL_OK);
            return ESP_FAIL;
        }
        remaining -= received;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA: esp_ota_end falhou: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        led_status_set(LED_STATE_ALL_OK);
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA: set_boot_partition falhou: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        led_status_set(LED_STATE_ALL_OK);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA: concluído. Reiniciando...");
    httpd_resp_sendstr(req, "OK");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK;
}

// ── Inicialização ─────────────────────────────────────────────────────────────

void web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_WEB_SERVER_PORT;
    config.max_open_sockets = 4;
    config.stack_size = 8192;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar servidor HTTP");
        return;
    }

    static const httpd_uri_t uri_root = {
        .uri = "/", .method = HTTP_GET, .handler = handler_root
    };
    static const httpd_uri_t uri_metrics = {
        .uri = "/api/metrics", .method = HTTP_GET, .handler = handler_metrics
    };
    static const httpd_uri_t uri_info = {
        .uri = "/api/info", .method = HTTP_GET, .handler = handler_info
    };
    static const httpd_uri_t uri_ota = {
        .uri = "/ota", .method = HTTP_POST, .handler = handler_ota
    };

    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_metrics);
    httpd_register_uri_handler(s_server, &uri_info);
    httpd_register_uri_handler(s_server, &uri_ota);

    ESP_LOGI(TAG, "Servidor iniciado na porta %d", CONFIG_WEB_SERVER_PORT);
}

void web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
