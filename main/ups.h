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

// Status/flags lidos dos registradores 0x008D–0x0095
typedef struct {
    // reg 0x008D — Falhas de hardware
    bool fail_overtemp;
    bool fail_internal;
    bool fail_shortcircuit;      // bit2 INF / bit4 SEN
    bool fail_overload;
    bool fail_end_battery;       // bit4 INF / bit2 SEN
    bool fail_abnormal_vout;
    bool fail_abnormal_vbat;
    bool fail_inverter;
    // reg 0x008E — Status da rede
    bool lo_f_input;
    bool hi_f_input;
    bool no_sync_input;
    bool lo_v_input;
    bool hi_v_input;
    bool no_v_input;
    bool lo_battery;
    bool noise_input;
    // reg 0x008F — Status operacional
    bool op_battery;
    bool op_stand_by;
    bool op_warning;
    bool op_startup;
    bool op_checkup;
    // reg 0x0091 — Timers / bateria
    bool sync_input;
    bool max_battery;
    bool shutdown_timer_active;
    // reg 0x0095 — Controle remoto / tensão nominal
    bool remote_control_active;
    bool vin_sel_220v;
} ups_status_t;

// Métricas e status globais acessíveis pelo servidor web
extern volatile ups_metricts_t g_ups_metrics;
extern volatile ups_status_t   g_ups_status;
extern volatile bool           g_ups_connected;

// Último buffer raw recebido do UPS (para diagnóstico via /api/raw)
#define UPS_RAW_BUF_SIZE 64
extern uint8_t  g_ups_raw_buf[UPS_RAW_BUF_SIZE];
extern size_t   g_ups_raw_len;

void parse_status_flags(const uint8_t *buf, ups_status_t *s);

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
void ups_start();

#endif // UPS_H
