#ifndef LED_STATUS_H
#define LED_STATUS_H

typedef enum {
    LED_STATE_BOOTING,          // Amarelo piscando rápido
    LED_STATE_WIFI_CONNECTING,  // Azul piscando rápido
    LED_STATE_MQTT_CONNECTING,  // Azul pulsando lento
    LED_STATE_UPS_DISCONNECTED, // Laranja piscando lento (WiFi/MQTT OK, aguardando UPS)
    LED_STATE_ALL_OK,           // Verde fixo
    LED_STATE_WIFI_FAILED,      // Vermelho piscando rápido
    LED_STATE_OTA_UPDATING,     // Ciano piscando rápido
    LED_STATE_AP_MODE,           // Magenta pulsando lento (portal de configuração WiFi)
} led_state_t;

void led_status_init(void);
void led_status_set(led_state_t state);

#endif // LED_STATUS_H
