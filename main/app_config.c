#include "app_config.h"

#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "APP-CFG";

#define NVS_NAMESPACE   "app_cfg"
#define NVS_KEY_URL     "mqtt_url"
#define NVS_KEY_PORT    "mqtt_port"
#define NVS_KEY_USER    "mqtt_user"
#define NVS_KEY_PASS    "mqtt_pass"
#define NVS_KEY_POLL    "poll_ms"
#define NVS_KEY_CALIB   "iout_calib"

static app_config_t s_cfg;

static void load_defaults(app_config_t *c)
{
    memset(c, 0, sizeof(*c));
    strlcpy(c->mqtt_url,  CONFIG_BROKER_URL,      sizeof(c->mqtt_url));
    strlcpy(c->mqtt_user, CONFIG_BROKER_USERNAME, sizeof(c->mqtt_user));
    strlcpy(c->mqtt_pass, CONFIG_BROKER_PASSWORD, sizeof(c->mqtt_pass));
    c->mqtt_port        = CONFIG_BROKER_PORT;
    c->poll_interval_ms = CONFIG_UPS_POLL_INTERVAL_MS;
    c->iout_calib       = CONFIG_UPS_IOUT_CALIB;
}

static bool valid(const app_config_t *c)
{
    bool url_ok = strncmp(c->mqtt_url, "mqtt://", 7) == 0 || strncmp(c->mqtt_url, "mqtts://", 8) == 0 ||
                  strncmp(c->mqtt_url, "ws://", 5) == 0   || strncmp(c->mqtt_url, "wss://", 6) == 0;
    return url_ok &&
           c->mqtt_port > 0 &&
           c->poll_interval_ms >= 1000 && c->poll_interval_ms <= 60000 &&
           c->iout_calib >= 1;
}

void app_config_load(void)
{
    load_defaults(&s_cfg);

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "Sem configuração salva; usando valores do menuconfig");
        return;
    }

    app_config_t c = s_cfg;
    size_t len;
    len = sizeof(c.mqtt_url);  nvs_get_str(h, NVS_KEY_URL,  c.mqtt_url,  &len);
    len = sizeof(c.mqtt_user); nvs_get_str(h, NVS_KEY_USER, c.mqtt_user, &len);
    len = sizeof(c.mqtt_pass); nvs_get_str(h, NVS_KEY_PASS, c.mqtt_pass, &len);
    nvs_get_u16(h, NVS_KEY_PORT,  &c.mqtt_port);
    nvs_get_u32(h, NVS_KEY_POLL,  &c.poll_interval_ms);
    nvs_get_u8(h,  NVS_KEY_CALIB, &c.iout_calib);
    nvs_close(h);

    if (valid(&c)) {
        s_cfg = c;
        ESP_LOGI(TAG, "Configuração carregada do NVS (broker %s:%u)", s_cfg.mqtt_url, s_cfg.mqtt_port);
    } else {
        ESP_LOGW(TAG, "Configuração do NVS inválida; usando valores do menuconfig");
    }
}

const app_config_t *app_config_get(void)
{
    return &s_cfg;
}

esp_err_t app_config_save(const app_config_t *cfg)
{
    if (!valid(cfg)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, NVS_KEY_URL, cfg->mqtt_url);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_USER, cfg->mqtt_user);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_PASS, cfg->mqtt_pass);
    if (err == ESP_OK) err = nvs_set_u16(h, NVS_KEY_PORT, cfg->mqtt_port);
    if (err == ESP_OK) err = nvs_set_u32(h, NVS_KEY_POLL, cfg->poll_interval_ms);
    if (err == ESP_OK) err = nvs_set_u8(h,  NVS_KEY_CALIB, cfg->iout_calib);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Configuração salva (broker %s:%u)", cfg->mqtt_url, cfg->mqtt_port);
    }
    return err;
}
