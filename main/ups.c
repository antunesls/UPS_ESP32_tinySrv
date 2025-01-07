#include "ups.h"
#include "ups_mqtt.h"

//====

#define EXAMPLE_USB_HOST_PRIORITY (10)
#define EXAMPLE_TX_TIMEOUT_MS (300)

//==============

// static const char *TAG = "USB-CDC";
static const char *TAG = "UPS-Srv";

static SemaphoreHandle_t device_disconnected_sem;

static const uint8_t cmd_req1[] = {0xFF, 0xFE, 0x00, 0x8E, 0x01, 0x8F};
static const uint8_t cmd_req2[] = {0xAA, 0x04, 0x00, 0x80, 0x1E, 0x9E};
static const uint8_t cmd_get_data[] = {0xAA, 0x04, 0x00, 0x80, 0x1E, 0x9E};

static const usb_device_desc_t *dev_desc;

static bool lConected = false;

static uint8_t xBuffer[512]; // Adjust size according to your needs
static size_t xBufferIndex = 0;

// Função linear: equivalente ao linear() em Python
float linear(uint8_t val, float a, float b)
{
    float result = b + val * a;
    float factor = pow(10, 2);
    return roundf(result * factor) / factor;
}

// Processamento de potência: equivalente ao processPower() em Python
void processPower(float wOut, float wIn, float *power_current_kwh_out, float *power_current_kwh_in, int64_t *power_last_update)
{
    int64_t curr = esp_log_timestamp() / 1000; // Obtendo o tempo atual em segundos
    float diff = (curr - *power_last_update);  // em segundos
    *power_last_update = curr;

    *power_current_kwh_out += (wOut / 1000.0) * (diff / 3600.0);
    *power_current_kwh_in += (wIn / 1000.0) * (diff / 3600.0);
}

// Construir payload: equivalente ao build_payload() em Python

ups_metricts_t build_payload(const uint8_t *buffer, char *output_json, size_t output_size)
{
    static float power_current_kwh_out = 0.0;
    static float power_current_kwh_in = 0.0;
    static int64_t power_last_update = 0;

    // Inicializar power_last_update no app_main
    if (power_last_update == 0)
    {
        power_last_update = esp_log_timestamp() / 1000; // Obtendo o tempo inicial
    };

    float iOut = linear(buffer[0x0D], 0.1152, 0);
    float vOut = linear(buffer[0x1E], 0.555, 0);
    float wOut = iOut * vOut;

    float vIn = linear(buffer[0x0C], 1.06, 0);
    float wIn = iOut * vIn; // Assumindo que iIn é igual a iOut

    processPower(wOut, wIn, &power_current_kwh_out, &power_current_kwh_in, &power_last_update);

    // Preparar payload JSON
    snprintf(output_json, output_size,
             "{"
             "\"Power_Out_Percent\":{\"value\":%.1f,\"unit\":\"%%\"},"
             "\"Current_Out\":{\"value\":%.2f,\"unit\":\"A\"},"
             "\"Voltage_Out\":{\"value\":%.1f,\"unit\":\"V\"},"
             "\"Voltage_In\":{\"value\":%.1f,\"unit\":\"V\"},"
             "\"Power_Out\":{\"value\":%.1f,\"unit\":\"W\"},"
             "\"Power_In\":{\"value\":%.1f,\"unit\":\"W\"},"
             "\"Energy_Out\":{\"value\":%.2f,\"unit\":\"kWh\"},"
             "\"Energy_In\":{\"value\":%.2f,\"unit\":\"kWh\"},"
             "\"Temperature\":{\"value\":%.1f,\"unit\":\"C\"},"
             "\"Battery_State\":{\"value\":%.1f,\"unit\":\"%%\"},"
             "\"Battery_Voltage\":{\"value\":%.2f,\"unit\":\"V\"},"
             "\"Frequency\":{\"value\":%.1f,\"unit\":\"Hz\"}"
             "}",
             linear(buffer[0x0E], 1, 0), // Power Out
             iOut,
             vOut,
             vIn,
             wOut,
             wIn,
             power_current_kwh_out,
             power_current_kwh_in,
             linear(buffer[0x0F], 1, 0),
             linear(buffer[0x08], 0.392, 0),
             linear(buffer[0x0B], 0.0671, 0),
             linear(buffer[0x18], -0.1152, 65));

    // Atribuicao de dados
    ups_metricts_t metrics = {
        .power_out_percent = linear(buffer[0x0E], 1, 0),
        .current_out = iOut,
        .voltage_out = vOut,
        .voltage_in = vIn,
        .power_out = wOut,
        .power_in = wIn,
        .energy_out = power_current_kwh_out,
        .energy_in = power_current_kwh_in,
        .temperature = linear(buffer[0x0F], 1, 0),
        .battery_state = linear(buffer[0x08], 0.392, 0),
        .battery_voltage = linear(buffer[0x0B], 0.0671, 0),
        .frequency = linear(buffer[0x18], -0.1152, 65)};

    publish_metrics(&metrics);

    return metrics;
}

// Callback para processar dados recebidos e montar o payload
bool handle_rx(const uint8_t *data, size_t data_len, void *arg)
{
    ESP_LOGI(TAG, "Dados recebidos");
    ESP_LOGI(TAG, "Tamanho recebidos %u", data_len);
    ESP_LOG_BUFFER_HEXDUMP(TAG, data, data_len, ESP_LOG_VERBOSE);

    if (xBufferIndex + data_len < sizeof(xBuffer))
    {
        memcpy(xBuffer + xBufferIndex, data, data_len);
        xBufferIndex += data_len;

        ESP_LOGI(TAG, "Dados buffer %.*s", (int)xBufferIndex, xBuffer);
        ESP_LOGI(TAG, "Tamanho buffer %u", xBufferIndex);

        // Verifica se recebeu dados suficientes
        if (xBufferIndex >= 31)
        {
            char json_payload[512];
            build_payload(xBuffer, json_payload, sizeof(json_payload));
            ESP_LOGI(TAG, "Payload: %s", json_payload);
            xBufferIndex = 0; // Reset buffer for next use
        }
    }
    else
    {
        ESP_LOGE(TAG, "Buffer overflow");
        xBufferIndex = 0; // Reset buffer to avoid undefined behavior
    }

    return true;
}

// Callback para processar novo dispositovs
void handle_newdev(usb_device_handle_t usb_dev)
{

    // Aloca memória para o descritor do dispositivo
    dev_desc = (usb_device_desc_t *)malloc(sizeof(usb_device_desc_t));

    if (dev_desc == NULL)
    {
        ESP_LOGE(TAG, "Falha ao alocar memória para o descritor do dispositivo");
        return;
    }

    // Obtém o descritor do dispositivo
    esp_err_t err = usb_host_get_device_descriptor(usb_dev, &dev_desc);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao obter descritor do dispositivo: %s", esp_err_to_name(err));
        dev_desc = NULL;
        return;
    }

    ESP_LOGI(TAG, "Dispositivo conectado: VID=0x%04X, PID=0x%04X", dev_desc->idVendor, dev_desc->idProduct);

    lConected = true;
}

// Callback de eventos do dispositivo
void handle_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx)
{

    ESP_LOGI(TAG, "Buscando1");

    switch (event->type)
    {
    case CDC_ACM_HOST_ERROR:
        ESP_LOGE(TAG, "Erro no CDC-ACM, err_no = %i", event->data.error);
        break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        ESP_LOGI(TAG, "Dispositivo desconectado");
        ESP_ERROR_CHECK(cdc_acm_host_close(event->data.cdc_hdl));
        xSemaphoreGive(device_disconnected_sem);
        break;
    case CDC_ACM_HOST_SERIAL_STATE:
        ESP_LOGI(TAG, "Notificação de estado serial 0x%04X", event->data.serial_state.val);
        break;
    case CDC_ACM_HOST_NETWORK_CONNECTION:
    default:
        ESP_LOGW(TAG, "Evento CDC não suportado: %i", event->type);
        break;
    }
}

// Tarefa para manipular eventos da biblioteca USB Host
void usb_lib_task(void *arg)
{
    while (1)
    {

        // Inicia a manipulação de eventos do sistema
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
        {
            ESP_ERROR_CHECK(usb_host_device_free_all());
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)
        {
            ESP_LOGI(TAG, "USB: Todos os dispositivos liberados");
            lConected = false;
            // Continua manipulando eventos USB para permitir a reconexão do dispositivo
        }
    }
}

void ups_start(void)
{
    //=================USB======================================

    device_disconnected_sem = xSemaphoreCreateBinary();
    assert(device_disconnected_sem);

    // Instala o driver USB Host
    ESP_LOGI(TAG, "Instalando USB Host");
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    ESP_ERROR_CHECK(usb_host_install(&host_config));

    // Cria uma tarefa para manipular eventos da biblioteca USB
    BaseType_t task_created = xTaskCreate(usb_lib_task, "usb_lib", 4096, xTaskGetCurrentTaskHandle(), EXAMPLE_USB_HOST_PRIORITY, NULL);
    assert(task_created == pdTRUE);

    ESP_LOGI(TAG, "Instalando driver CDC-ACM");

    static const cdc_acm_host_driver_config_t cdc_acm_driver_config_default = {
        .driver_task_stack_size = 4096,
        .driver_task_priority = 10,
        .xCoreID = 0,
        .new_dev_cb = handle_newdev,
    };

    ESP_LOGI(TAG, "Step 1");

    ESP_ERROR_CHECK(cdc_acm_host_install(&cdc_acm_driver_config_default));

    ESP_LOGI(TAG, "Step 2");

    const cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = 1000,
        .out_buffer_size = 4096,
        .in_buffer_size = 4096,
        .user_arg = NULL,
        .event_cb = handle_event,
        .data_cb = handle_rx};

    ESP_LOGI(TAG, "Step 3");

    while (true)
    {

        vTaskDelay(pdMS_TO_TICKS(1000));

        if (lConected)
        {
            cdc_acm_dev_hdl_t cdc_dev = NULL;

            while (!lConected)
            {
                ESP_LOGI(TAG, "Procurando dispositivos");
                vTaskDelay(pdMS_TO_TICKS(1000));
            }

            // Abre o dispositivo USB
            ESP_LOGI(TAG, "Abrindo dispositivo CDC ACM 0x%04X:0x%04X...", dev_desc->idVendor, dev_desc->idProduct);
            esp_err_t err = cdc_acm_host_open(dev_desc->idVendor, dev_desc->idProduct, 0, &dev_config, &cdc_dev);
            if (ESP_OK != err)
            {
                ESP_LOGI(TAG, "Falha ao abrir dispositivo");
                vTaskDelay(pdMS_TO_TICKS(10000));
                continue;
            }

            if (lConected)
            {

                ESP_LOGI(TAG, "Setting up line coding");

                cdc_acm_line_coding_t line_coding;

                line_coding.dwDTERate = 2560;
                line_coding.bDataBits = 8;
                line_coding.bParityType = 0;
                line_coding.bCharFormat = 0;
                ESP_ERROR_CHECK(cdc_acm_host_line_coding_set(cdc_dev, &line_coding));
                ESP_LOGI(TAG, "Line Set: Rate: %" PRIu32 ", Stop bits: %" PRIu8 ", Parity: %" PRIu8 ", Databits: %" PRIu8 "",
                         line_coding.dwDTERate, line_coding.bCharFormat, line_coding.bParityType, line_coding.bDataBits);

                // cdc_acm_host_desc_print(cdc_dev);
                vTaskDelay(pdMS_TO_TICKS(100));

                // Envia comandos e solicita dados
                ESP_LOGI(TAG, "Send CMD1...");
                ESP_LOG_BUFFER_HEXDUMP(TAG, cmd_req1, sizeof(cmd_req1), ESP_LOG_VERBOSE);
                if (lConected)
                {
                    ESP_ERROR_CHECK(cdc_acm_host_data_tx_blocking(cdc_dev, cmd_req1, sizeof(cmd_req1), EXAMPLE_TX_TIMEOUT_MS));
                };

                vTaskDelay(pdMS_TO_TICKS(100));

                ESP_LOGI(TAG, "Send CMD2...");
                ESP_LOG_BUFFER_HEXDUMP(TAG, cmd_req2, sizeof(cmd_req2), ESP_LOG_VERBOSE);

                if (lConected)
                {
                    ESP_ERROR_CHECK(cdc_acm_host_data_tx_blocking(cdc_dev, cmd_req2, sizeof(cmd_req2), EXAMPLE_TX_TIMEOUT_MS));
                };

                vTaskDelay(pdMS_TO_TICKS(100));

                while (1 && lConected)
                {
                    vTaskDelay(pdMS_TO_TICKS(5000)); // Permite que o watchdog seja alimentado

                    ESP_LOGI(TAG, "Solicitando dados...");
                    ESP_LOG_BUFFER_HEXDUMP(TAG, cmd_get_data, sizeof(cmd_get_data), ESP_LOG_VERBOSE);

                    if (lConected)
                    {
                        ESP_ERROR_CHECK(cdc_acm_host_data_tx_blocking(cdc_dev, cmd_get_data, sizeof(cmd_get_data), EXAMPLE_TX_TIMEOUT_MS));
                    };
                }
            }
            // Espera pela desconexão
            ESP_LOGI(TAG, "Aguardando desconexão");
            xSemaphoreTake(device_disconnected_sem, portMAX_DELAY);

            ESP_LOGI(TAG, "Dispositivo desconectado");
        }
    }
}