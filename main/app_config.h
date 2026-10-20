#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define APP_CFG_URL_MAX   128
#define APP_CFG_USER_MAX  64
#define APP_CFG_PASS_MAX  64

// Configuração do app editável pela página web.
// Persistida no NVS; na ausência de valor salvo usa os defaults do menuconfig.
typedef struct {
    char     mqtt_url[APP_CFG_URL_MAX];
    uint16_t mqtt_port;
    char     mqtt_user[APP_CFG_USER_MAX];
    char     mqtt_pass[APP_CFG_PASS_MAX];
    uint32_t poll_interval_ms;   // intervalo de leitura do nobreak
    uint8_t  iout_calib;         // fallback de V_IOUTCALIB (reg 0x00F3)
} app_config_t;

/**
 * Carrega a configuração do NVS (deve ser chamada após nvs_flash_init).
 */
void app_config_load(void);

/**
 * Retorna a configuração ativa.
 */
const app_config_t *app_config_get(void);

/**
 * Valida e grava a configuração no NVS. Passa a valer após reiniciar.
 * @param cfg Nova configuração.
 * @return ESP_OK, ESP_ERR_INVALID_ARG se algum campo for inválido, ou erro do NVS.
 */
esp_err_t app_config_save(const app_config_t *cfg);

#endif // APP_CONFIG_H
