#ifndef UPS_MQTT_H
#define UPS_MQTT_H

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "ups.h"
#include "esp_system.h"

#define MAX_IDENTIFIERS 10
#define MAX_STRING_LENGTH 256
#define MAX_AVAILABILITY 10

// Define topic to setup mqtt discovery

#define TOPIC_SETUP "homeassistant/sensor/0x"
#define TOPIC_CONFIG "config"

// Define a struct for type and unit
typedef struct
{
    char *type;
    char *unit;
} TypeInfo;

// Category os UPS

#define TYPE_POWER_OUT_PERCENT "power_out_percent"
#define TYPE_CURRENT_OUT "current_out"
#define TYPE_VOLTAGE_OUT "voltage_out"
#define TYPE_VOLTAGE_IN "voltage_in"
#define TYPE_POWER_OUT "power_out"
#define TYPE_POWER_IN "power_in"
#define TYPE_TEMPERATURE "temperature"
#define TYPE_BATTERY_STATE "battery_state"
#define TYPE_BATTERY_VOLTAGE "battery_voltage"
#define TYPE_FREQUENCY "frequency"

// Fixed values from the JSON MQTT Setup
#define TOPIC_0 "zigbee2mqtt/bridge/state"
#define VALUE_TEMPLATE_0 "{{ value_json.state }}"
#define TOPIC_1 "zigbee2mqtt/Sensor_Termometro_Banheiro/availability"
#define VALUE_TEMPLATE_1 "{{ value_json.state }}"
#define AVAILABILITY_MODE "all"
#define IDENTIFIER_0 "zigbee2mqtt_0xa4c138be5fc1e488"
#define MANUFACTURER "Tuya"
#define MODEL "Temperature & humidity sensor (WSD500A)"
#define NAME "Sensor_Termometro_Banheiro"
#define VIA_DEVICE "zigbee2mqtt_bridge_0x00124b002b486c01"
#define DEVICE_CLASS "temperature"
#define OBJECT_ID "sensor_termometro_banheiro_temperature"
#define ORIGIN_NAME "Zigbee2MQTT"
#define ORIGIN_SW "1.42.0"
#define ORIGIN_URL "https://www.zigbee2mqtt.io"
#define STATE_CLASS "measurement"
#define STATE_TOPIC "zigbee2mqtt/Sensor_Termometro_Banheiro"
#define UNIQUE_ID "0xa4c138be5fc1e488_temperature_zigbee2mqtt"
#define UNIT_OF_MEASUREMENT "\u00b0C"
#define VALUE_TEMPLATE "{{ value_json.temperature }}"

typedef struct
{
    char topic[MAX_STRING_LENGTH];
    char value_template[MAX_STRING_LENGTH];
} Availability;

typedef struct
{
    char identifiers[MAX_IDENTIFIERS][MAX_STRING_LENGTH];
    char manufacturer[MAX_STRING_LENGTH];
    char model[MAX_STRING_LENGTH];
    char name[MAX_STRING_LENGTH];
    char via_device[MAX_STRING_LENGTH];
} Device;

typedef struct
{
    char name[MAX_STRING_LENGTH];
    char sw[MAX_STRING_LENGTH];
    char url[MAX_STRING_LENGTH];
} Origin;

typedef struct
{
    Availability availability[MAX_AVAILABILITY];
    int availability_count; // Number of availability entries
    char availability_mode[MAX_STRING_LENGTH];
    Device device;
    char device_class[MAX_STRING_LENGTH];
    bool enabled_by_default;
    char object_id[MAX_STRING_LENGTH];
    Origin origin;
    char state_class[MAX_STRING_LENGTH];
    char state_topic[MAX_STRING_LENGTH];
    char unique_id[MAX_STRING_LENGTH];
    char unit_of_measurement[MAX_STRING_LENGTH];
    char value_template[MAX_STRING_LENGTH];
} SensorData;

extern esp_mqtt_client_handle_t client;

// Declarações de funções
void mqtt_app_start(void);
void publish_metrics(const ups_metricts_t *metrics);

#endif // MQTT_MANAGER_H
