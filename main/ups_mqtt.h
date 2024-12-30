#ifndef UPS_MQTT_H
#define UPS_MQTT_H

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "ups.h"

extern esp_mqtt_client_handle_t client;

// Declarações de funções
void mqtt_app_start(void);
//void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void publish_metrics(const ups_metricts_t *metrics);

#endif // MQTT_MANAGER_H
