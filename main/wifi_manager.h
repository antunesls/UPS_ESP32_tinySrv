#ifndef UPS_WIFI_MANAGER_H
#define UPS_WIFI_MANAGER_H

#include <stdbool.h>

/**
 * @brief Inicializa o gerenciador de WiFi.
 *
 * Sequência:
 *  1. Verifica se o botão BOOT (GPIO0) está pressionado >= 3s → apaga credenciais salvas.
 *  2. Tenta carregar credenciais do NVS.
 *  3. Se não houver credenciais → inicia portal AP de configuração (bloqueante).
 *  4. Se houver credenciais → tenta conectar como STA.
 *  5. Se a conexão STA falhar → inicia portal AP de configuração (bloqueante).
 *
 * @return true  Conectado ao WiFi com sucesso.
 * @return false Nunca retorna false — em caso de falha o dispositivo vai reiniciar
 *               ou ficar no portal AP indefinidamente.
 */
bool wifi_manager_start(void);

#endif // UPS_WIFI_MANAGER_H
