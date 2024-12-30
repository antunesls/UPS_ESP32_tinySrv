#ifndef UPS_H
#define UPS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

// Struct para armazenar métricas do UPS
typedef struct
{
    float power_out_percent;
    float current_out;
    float voltage_out;
    float voltage_in;
    float power_out;
    float power_in;
    float energy_out;
    float energy_in;
    float temperature;
    float battery_state;
    float battery_voltage;
    float frequency;
} ups_metricts_t;

// Funções declaradas no arquivo .c

/**
 * Função para calcular valores lineares.
 * @param val Valor base.
 * @param a Fator multiplicador.
 * @param b Offset.
 * @return Valor calculado.
 */
float linear(uint8_t val, float a, float b);

/**
 * Processa os dados de potência para calcular energia consumida e gerada.
 * @param wOut Potência de saída em watts.
 * @param wIn Potência de entrada em watts.
 * @param power_current_kwh_out Ponteiro para armazenar energia de saída.
 * @param power_current_kwh_in Ponteiro para armazenar energia de entrada.
 * @param power_last_update Ponteiro para armazenar o último timestamp.
 */
void processPower(float wOut, float wIn, float *power_current_kwh_out, float *power_current_kwh_in, int64_t *power_last_update);

/**
 * Constroi o payload JSON baseado nos dados do buffer.
 * @param buffer Dados recebidos.
 * @param output_json Ponteiro para a string JSON gerada.
 * @param output_size Tamanho do buffer JSON.
 * @return Estrutura ups_metricts_t contendo os dados.
 */
ups_metricts_t build_payload(const uint8_t *buffer, char *output_json, size_t output_size);

/**
 * Callback para processar dados recebidos.
 * @param data Dados recebidos.
 * @param data_len Tamanho dos dados recebidos.
 * @param arg Contexto adicional.
 * @return True se os dados forem processados com sucesso.
 */
bool handle_rx(const uint8_t *data, size_t data_len, void *arg);

/**
 * Callback para lidar com um novo dispositivo USB.
 * @param usb_dev Handle do dispositivo USB.
 */
void handle_newdev(usb_device_handle_t usb_dev);

/**
 * Callback para lidar com eventos do dispositivo.
 * @param event Dados do evento.
 * @param user_ctx Contexto do usuário.
 */
void handle_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx);

/**
 * Tarefa para manipular eventos da biblioteca USB Host.
 * @param arg Argumento da tarefa.
 */
void usb_lib_task(void *arg);

/**
 * Inicializa a aplicação UPS.
 */
void ups_start(void);

#endif // UPS_H
