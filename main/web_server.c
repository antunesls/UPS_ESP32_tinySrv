#include "web_server.h"
#include "web_ui.h"
#include "ups.h"
#include "led_status.h"
#include "log_buffer.h"

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

// ── GET /api/status ──────────────────────────────────────────────────────────

static esp_err_t handler_status(httpd_req_t *req)
{
    ups_status_t s = (ups_status_t)g_ups_status;
    char buf[768];
    snprintf(buf, sizeof(buf),
        "{"
        "\"op_battery\":%s,"
        "\"op_stand_by\":%s,"
        "\"op_warning\":%s,"
        "\"op_startup\":%s,"
        "\"op_checkup\":%s,"
        "\"no_v_input\":%s,"
        "\"lo_v_input\":%s,"
        "\"hi_v_input\":%s,"
        "\"lo_f_input\":%s,"
        "\"hi_f_input\":%s,"
        "\"no_sync_input\":%s,"
        "\"lo_battery\":%s,"
        "\"noise_input\":%s,"
        "\"max_battery\":%s,"
        "\"sync_input\":%s,"
        "\"shutdown_timer_active\":%s,"
        "\"remote_control_active\":%s,"
        "\"vin_sel_220v\":%s,"
        "\"fail_overtemp\":%s,"
        "\"fail_internal\":%s,"
        "\"fail_overload\":%s,"
        "\"fail_shortcircuit\":%s,"
        "\"fail_end_battery\":%s,"
        "\"fail_abnormal_vout\":%s,"
        "\"fail_abnormal_vbat\":%s,"
        "\"fail_inverter\":%s"
        "}",
        s.op_battery            ? "true" : "false",
        s.op_stand_by           ? "true" : "false",
        s.op_warning            ? "true" : "false",
        s.op_startup            ? "true" : "false",
        s.op_checkup            ? "true" : "false",
        s.no_v_input            ? "true" : "false",
        s.lo_v_input            ? "true" : "false",
        s.hi_v_input            ? "true" : "false",
        s.lo_f_input            ? "true" : "false",
        s.hi_f_input            ? "true" : "false",
        s.no_sync_input         ? "true" : "false",
        s.lo_battery            ? "true" : "false",
        s.noise_input           ? "true" : "false",
        s.max_battery           ? "true" : "false",
        s.sync_input            ? "true" : "false",
        s.shutdown_timer_active ? "true" : "false",
        s.remote_control_active ? "true" : "false",
        s.vin_sel_220v          ? "true" : "false",
        s.fail_overtemp         ? "true" : "false",
        s.fail_internal         ? "true" : "false",
        s.fail_overload         ? "true" : "false",
        s.fail_shortcircuit     ? "true" : "false",
        s.fail_end_battery      ? "true" : "false",
        s.fail_abnormal_vout    ? "true" : "false",
        s.fail_abnormal_vbat    ? "true" : "false",
        s.fail_inverter         ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ── GET /api/info ────────────────────────────────────────────────────────────
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

// ── GET /api/logs ───────────────────────────────────────────────────────

static esp_err_t handler_logs(httpd_req_t *req)
{
    char *json = log_buffer_to_json();
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

// ── GET /api/raw ─────────────────────────────────────────────────────────────
// Retorna o último buffer recebido do UPS em hex, para diagnóstico de offsets.

static esp_err_t handler_raw(httpd_req_t *req)
{
    // cada byte = "XX " (3 chars) + newlines de agrupamento + terminador
    char buf[UPS_RAW_BUF_SIZE * 4 + 256];
    size_t pos = 0;
    size_t len = g_ups_raw_len;

    pos += snprintf(buf + pos, sizeof(buf) - pos, "{\"len\":%u,\"hex\":\"", (unsigned)len);
    for (size_t i = 0; i < len && pos + 4 < sizeof(buf); i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X ", g_ups_raw_buf[i]);
    }
    if (pos > 0 && buf[pos - 1] == ' ') pos--; // remove trailing space
    pos += snprintf(buf + pos, sizeof(buf) - pos, "\",\"bytes\":[");
    for (size_t i = 0; i < len && pos + 8 < sizeof(buf); i++) {
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%u%s",
                        g_ups_raw_buf[i], i + 1 < len ? "," : "");
    }
    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");

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
    static const httpd_uri_t uri_status = {
        .uri = "/api/status", .method = HTTP_GET, .handler = handler_status
    };
    static const httpd_uri_t uri_info = {
        .uri = "/api/info", .method = HTTP_GET, .handler = handler_info
    };
    static const httpd_uri_t uri_logs = {
        .uri = "/api/logs", .method = HTTP_GET, .handler = handler_logs
    };
    static const httpd_uri_t uri_raw = {
        .uri = "/api/raw", .method = HTTP_GET, .handler = handler_raw
    };
    static const httpd_uri_t uri_ota = {
        .uri = "/ota", .method = HTTP_POST, .handler = handler_ota
    };

    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_metrics);
    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_info);
    httpd_register_uri_handler(s_server, &uri_logs);
    httpd_register_uri_handler(s_server, &uri_raw);
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
