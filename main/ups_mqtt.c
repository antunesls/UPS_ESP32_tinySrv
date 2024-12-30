#include "ups_mqtt.h"
#include "ups.h"

static const char *TAG = "UPS-MQTT";

esp_mqtt_client_handle_t client;


// Função para publicar os dados via MQTT em tópicos separados
void publish_metrics(const ups_metricts_t *metrics) {
    if (client == NULL || metrics == NULL) {
        ESP_LOGE("MQTT", "Client or metrics is NULL");
        return;
    }

    // Publicar cada métrica em seu próprio tópico
    char topic[64];
    char payload[32];

    snprintf(topic, sizeof(topic), "/ups_esp32_srv/metrics/power_out_percent");
    snprintf(payload, sizeof(payload), "%.2f", metrics->power_out_percent);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);

    snprintf(topic, sizeof(topic), "/ups_esp32_srv/metrics/current_out");
    snprintf(payload, sizeof(payload), "%.2f", metrics->current_out);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);

    snprintf(topic, sizeof(topic), "/ups_esp32_srv/metrics/voltage_out");
    snprintf(payload, sizeof(payload), "%.2f", metrics->voltage_out);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);

    snprintf(topic, sizeof(topic), "/ups_esp32_srv/metrics/voltage_in");
    snprintf(payload, sizeof(payload), "%.2f", metrics->voltage_in);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);

    snprintf(topic, sizeof(topic), "/ups_esp32_srv/metrics/power_out");
    snprintf(payload, sizeof(payload), "%.2f", metrics->power_out);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);

    snprintf(topic, sizeof(topic), "/ups_esp32_srv/metrics/power_in");
    snprintf(payload, sizeof(payload), "%.2f", metrics->power_in);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);

    snprintf(topic, sizeof(topic), "/ups_esp32_srv/metrics/energy_out");
    snprintf(payload, sizeof(payload), "%.2f", metrics->energy_out);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);

    snprintf(topic, sizeof(topic), "/ups_esp32_srv/metrics/energy_in");
    snprintf(payload, sizeof(payload), "%.2f", metrics->energy_in);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);

    snprintf(topic, sizeof(topic), "/ups_esp32_srv/metrics/temperature");
    snprintf(payload, sizeof(payload), "%.2f", metrics->temperature);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);

    snprintf(topic, sizeof(topic), "/ups_esp32_srv/metrics/battery_state");
    snprintf(payload, sizeof(payload), "%.2f", metrics->battery_state);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);

    snprintf(topic, sizeof(topic), "/ups_esp32_srv/metrics/battery_voltage");
    snprintf(payload, sizeof(payload), "%.2f", metrics->battery_voltage);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);

    snprintf(topic, sizeof(topic), "/ups_esp32_srv/metrics/frequency");
    snprintf(payload, sizeof(payload), "%.2f", metrics->frequency);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 0);
}


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    //  ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    
    //int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        //.broker.address.uri = "mqtt://antunesls:wCdEv_u7XKo-yip-34vH@10.100.100.60",
        .broker.address.uri = "mqtt://test.mosquitto.org",
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}
