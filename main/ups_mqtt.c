#include "ups_mqtt.h"
#include "ups.h"
#include "wifi.h"

static const char *TAG = "UPS-MQTT";

esp_mqtt_client_handle_t client;

// Função para publicar os dados via MQTT em tópicos separados
void publish_metrics(const ups_metricts_t *metrics)
{
    if (client == NULL || metrics == NULL)
    {
        ESP_LOGE("MQTT", "Client or metrics is NULL");
        return;
    }

    // Publicar cada métrica em seu próprio tópico
    char topic[64];
    char payload[32];

    uint8_t mac[6];
    char macAddr[13];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(macAddr, sizeof(macAddr),
             "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    snprintf(topic, sizeof(topic), "UPS_ESP32_tinySrv/%s/availability", macAddr);
    //    snprintf(payload, sizeof(payload), "%.2f", metrics->power_out_percent);
    esp_mqtt_client_publish(client, topic, "{\"state\":\"online\"}", 0, 1, 1);

    snprintf(topic, sizeof(topic), "UPS_ESP32_tinySrv/Sensor_power_out_percent");
    snprintf(payload, sizeof(payload), "{\"value\":%.2f}", metrics->power_out_percent);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);

    snprintf(topic, sizeof(topic), "UPS_ESP32_tinySrv/Sensor_current_out");
    snprintf(payload, sizeof(payload), "{\"value\":%.2f}", metrics->current_out);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);

    snprintf(topic, sizeof(topic), "UPS_ESP32_tinySrv/Sensor_voltage_out");
    snprintf(payload, sizeof(payload), "{\"value\":%.2f}", metrics->voltage_out);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);

    snprintf(topic, sizeof(topic), "UPS_ESP32_tinySrv/Sensor_voltage_in");
    snprintf(payload, sizeof(payload), "{\"value\":%.2f}", metrics->voltage_in);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);

    snprintf(topic, sizeof(topic), "UPS_ESP32_tinySrv/Sensor_power_out");
    snprintf(payload, sizeof(payload), "{\"value\":%.2f}", metrics->power_out);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);

    snprintf(topic, sizeof(topic), "UPS_ESP32_tinySrv/Sensor_power_in");
    snprintf(payload, sizeof(payload), "{\"value\":%.2f}", metrics->power_in);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);

    snprintf(topic, sizeof(topic), "UPS_ESP32_tinySrv/Sensor_energy_out");
    snprintf(payload, sizeof(payload), "{\"value\":%.2f}", metrics->energy_out);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);

    snprintf(topic, sizeof(topic), "UPS_ESP32_tinySrv/Sensor_energy_in");
    snprintf(payload, sizeof(payload), "{\"value\":%.2f}", metrics->energy_in);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);

    snprintf(topic, sizeof(topic), "UPS_ESP32_tinySrv/Sensor_temperature");
    snprintf(payload, sizeof(payload), "{\"value\":%.2f}", metrics->temperature);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);

    snprintf(topic, sizeof(topic), "UPS_ESP32_tinySrv/Sensor_battery_state");
    snprintf(payload, sizeof(payload), "{\"value\":%.2f}", metrics->battery_state);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);

    snprintf(topic, sizeof(topic), "UPS_ESP32_tinySrv/Sensor_battery_voltage");
    snprintf(payload, sizeof(payload), "{\"value\":%.2f}", metrics->battery_voltage);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);

    snprintf(topic, sizeof(topic), "UPS_ESP32_tinySrv/Sensor_frequency");
    snprintf(payload, sizeof(payload), "{\"value\":%.2f}", metrics->frequency);
    esp_mqtt_client_publish(client, topic, payload, 0, 1, 1);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    //  ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;

    // int msg_id;
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

void generateJSON_mqtt_setup(SensorData *sensor, char *output, size_t max_length)
{

    ESP_LOGI(TAG, "Gerando o JSON MQTT");

    printf("availability_mode: '%s'\n", sensor->availability_mode);

    snprintf(output, max_length,
             "{\n"
             "  \"availability\": [\n"
             "    {\"topic\": \"%s\", \"value_template\": \"%s\"}\n"
             "  ],\n"
             "  \"availability_mode\": \"%s\",\n"
             "  \"device\": {\n"
             "    \"identifiers\": [\"%s\"],\n"
             "    \"manufacturer\": \"%s\",\n"
             "    \"model\": \"%s\",\n"
             "    \"name\": \"%s\",\n"
             "    \"via_device\": \"%s\"\n"
             "  },\n"
             "  \"device_class\": \"%s\",\n"
             "  \"enabled_by_default\": %s,\n"
             "  \"object_id\": \"%s\",\n"
             "  \"origin\": {\n"
             "    \"name\": \"%s\",\n"
             "    \"sw\": \"%s\",\n"
             "    \"url\": \"%s\"\n"
             "  },\n"
             "  \"name\": \"%s\",\n"
             "  \"state_class\": \"%s\",\n"
             "  \"state_topic\": \"%s\",\n"
             "  \"unique_id\": \"%s\",\n"
             "  \"unit_of_measurement\": \"%s\",\n"
             "  \"value_template\": \"%s\"\n"
             "}",
             sensor->availability[0].topic, sensor->availability[0].value_template,
             sensor->availability_mode,
             sensor->device.identifiers[0], sensor->device.manufacturer, sensor->device.model,
             sensor->device.name, sensor->device.via_device,
             sensor->device_class,
             sensor->enabled_by_default ? "true" : "false",
             sensor->object_id,
             sensor->origin.name, sensor->origin.sw, sensor->origin.url,
             sensor->name,
             sensor->state_class, sensor->state_topic, sensor->unique_id,
             sensor->unit_of_measurement, sensor->value_template);
}

void SensorSetup(SensorData *sensor, TypeInfo *type_sensor, char *macaddress)
{
    char aux[64];

    ESP_LOGI(TAG, "Preenchendo o Struct do Sensor");

    // Fill availability
    sensor->availability_count = 1;

    snprintf(aux, sizeof(aux), TOPIC_0, macaddress);
    strcpy(sensor->availability[0].topic, aux);

    strcpy(sensor->availability[0].value_template, VALUE_TEMPLATE_0);

    // Set availability mode
    strcpy(sensor->availability_mode, AVAILABILITY_MODE);

    // Fill device information
    snprintf(aux, sizeof(aux), IDENTIFIER_0, macaddress);
    strcpy(sensor->device.identifiers[0], aux);

    strcpy(sensor->device.manufacturer, MANUFACTURER);
    strcpy(sensor->device.model, MODEL);

    snprintf(aux, sizeof(aux), NAME, macaddress);
    strcpy(sensor->device.name, aux);

    snprintf(aux, sizeof(aux), VIA_DEVICE, macaddress);
    strcpy(sensor->device.via_device, aux);

    // Set device class
    snprintf(aux, sizeof(aux), DEVICE_CLASS, type_sensor->device);
    strcpy(sensor->device_class, aux);

    // Set enabled_by_default
    sensor->enabled_by_default = true;

    // Set object_id
    snprintf(aux, sizeof(aux), OBJECT_ID, type_sensor->type);
    strcpy(sensor->object_id, aux);

    // Name
    strcpy(sensor->name, type_sensor->name);

    // Fill origin information
    strcpy(sensor->origin.name, ORIGIN_NAME);
    strcpy(sensor->origin.sw, ORIGIN_SW);
    strcpy(sensor->origin.url, ORIGIN_URL);

    // Set state_class
    strcpy(sensor->state_class, STATE_CLASS);

    // Set state_topic
    snprintf(aux, sizeof(aux), STATE_TOPIC, type_sensor->type);
    strcpy(sensor->state_topic, aux);

    // Set unique_id
    snprintf(aux, sizeof(aux), UNIQUE_ID, macaddress, type_sensor->type);
    strcpy(sensor->unique_id, aux);

    // Set unit_of_measurement
    snprintf(aux, sizeof(aux), UNIT_OF_MEASUREMENT, type_sensor->unit);
    strcpy(sensor->unit_of_measurement, aux);

    // Set value_template
    snprintf(aux, sizeof(aux), VALUE_TEMPLATE, "value");
    strcpy(sensor->value_template, aux);
}

void setup_mqtt()
{
    ESP_LOGI(TAG, "Memoria Livre: %lu ", esp_get_free_heap_size());

    static char topic[64];
    static char json_mqtt[2048];

    // Get MAC address and display as 6-byte hex value
    ESP_LOGI(TAG, "Obtem Mac Address");
    uint8_t mac[6];
    char macAddr[13];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(macAddr, sizeof(macAddr),
             "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "MAC address: %s", macAddr);

    ESP_LOGI(TAG, "Memoria Livre: %lu ", esp_get_free_heap_size());

    // Create an array of TypeInfo
    static TypeInfo typeInfoArray[] = {
        {TYPE_POWER_OUT_PERCENT, "%", DEVICE_POWER_OUT_PERCENT, NAME_POWER_OUT_PERCENT},
        {TYPE_CURRENT_OUT, "A", DEVICE_CURRENT_OUT, NAME_CURRENT_OUT},
        {TYPE_VOLTAGE_OUT, "V", DEVICE_VOLTAGE_OUT, NAME_VOLTAGE_OUT},
        {TYPE_VOLTAGE_IN, "V", DEVICE_VOLTAGE_IN, NAME_VOLTAGE_IN},
        {TYPE_POWER_OUT, "W", DEVICE_POWER_OUT, NAME_POWER_OUT},
        {TYPE_POWER_IN, "W", DEVICE_POWER_IN, NAME_POWER_IN},
        {TYPE_TEMPERATURE, "°C", DEVICE_TEMPERATURE, NAME_TEMPERATURE},
        {TYPE_BATTERY_STATE, "%", DEVICE_BATTERY_STATE, NAME_BATTERY_STATE},
        {TYPE_BATTERY_VOLTAGE, "V", DEVICE_BATTERY_VOLTAGE, NAME_BATTERY_VOLTAGE},
        {TYPE_FREQUENCY, "Hz", DEVICE_FREQUENCY, NAME_FREQUENCY}};

    ESP_LOGI(TAG, "Memoria Livre: %lu ", esp_get_free_heap_size());

    // Size of the array
    const size_t typeInfoArraySize = sizeof(typeInfoArray) / sizeof(typeInfoArray[0]);
    ESP_LOGI(TAG, "Memoria Livre: %lu ", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Gera Base do MQTT");

    static SensorData sensor;
    static TypeInfo arrayInfoDevice;

    // Iterando pelo array com um for
    for (int i = 0; i < typeInfoArraySize; i++)
    {

        arrayInfoDevice = typeInfoArray[i];

        ESP_LOGI(TAG, "Topico Inicial");
        snprintf(topic, sizeof(topic), "%s%s/%s/%s", TOPIC_SETUP, macAddr, arrayInfoDevice.type, TOPIC_CONFIG);

        ESP_LOGI(TAG, "Topico : %s", topic);

        ESP_LOGI(TAG, "Setup Sensor");

        ESP_LOGI(TAG, "Memoria Livre: %lu ", esp_get_free_heap_size());

        // Populate the structure using the function
        SensorSetup(&sensor, &arrayInfoDevice, macAddr);

        ESP_LOGI(TAG, "Gera JSON");
        // Gera o JSON a partir da estrutura preenchida
        generateJSON_mqtt_setup(&sensor, json_mqtt, sizeof(json_mqtt));

        // Exibe o JSON gerado
        printf("JSON Gerado:\n%s\n", json_mqtt);

        esp_mqtt_client_publish(client, topic, json_mqtt, 0, 1, 1);
    }
}

void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = URI_BROKER,
        //.broker.address.uri = "mqtt://test.mosquitto.org",
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    ESP_LOGI(TAG, "Setup MQTT");

    setup_mqtt();
}
