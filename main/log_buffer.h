#ifndef UPS_LOG_BUFFER_H
#define UPS_LOG_BUFFER_H

#include <stddef.h>

/**
 * @brief Inicializa o buffer circular de logs e registra o hook no esp_log.
 *        Deve ser chamada ANTES de qualquer ESP_LOG* para capturar todos os logs.
 */
void log_buffer_init(void);

/**
 * @brief Serializa o conteúdo do buffer em JSON.
 *        Retorna um buffer alocado via malloc — o chamador deve chamar free().
 *        Formato: {"logs":["<entrada1>","<entrada2>", ...]}
 *        Entradas mais antigas primeiro (ordem cronológica).
 */
char *log_buffer_to_json(void);

#endif // UPS_LOG_BUFFER_H
