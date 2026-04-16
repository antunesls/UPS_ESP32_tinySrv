#include "ups.h"
#include "ups_mqtt.h"
#include "led_status.h"
#include "nvs_flash.h"
#include "nvs.h"

// Fallback para quando o sdkconfig ainda não gerou os símbolos (análise de IDE)
#ifndef CONFIG_UPS_VA_RATING
#define CONFIG_UPS_VA_RATING    1200
#endif
#ifndef CONFIG_UPS_POWER_FACTOR
#define CONFIG_UPS_POWER_FACTOR 70
#endif

//====

#define EXAMPLE_USB_HOST_PRIORITY (10)
#define EXAMPLE_TX_TIMEOUT_MS (300)

//==============

// static const char *TAG = "USB-CDC";
static const char *TAG = "UPS-Srv";

static SemaphoreHandle_t device_disconnected_sem;

// Resposta real: 1 byte SOF (0xAA) + 5 bytes header + 25 bytes dados = 31 bytes.
// Valores verificados empiricamente com buffer hex real do dispositivo:
//
//  buffer[0x0B] = 200  V_VBATTERY x0.0671 → 13.4 V  (bat. 12V)
//  buffer[0x0C] = 196  V_VINPUT   direto  → 196 V
//  buffer[0x0D] =   9  V_IOUTPUT  x0.1152 →  1.0 A   (INF) / x0.0510 (SEN)
//  buffer[0x0F] =  47  V_POUTPUT  direto  →  47 %
//  buffer[0x10] = 103  V_FOUTPUT  x0.5825 →  60 Hz
//  buffer[0x16] =   1  V_TEMPER   direto  →   1 °C   (INFINIUM)
//  buffer[0x18] =  72  V_CBATTERY direto  →  72 %
//  buffer[0x1E] = 198  V_VOUTPUT  x0.5550 → 110 V   (INF) / x0.6000 (SEN)
//
// ATENÇÃO: buffer[0x1E] = índice 30; o pacote mínimo deve ser 31 bytes.
#define UPS_MIN_PACKET_SIZE 31

static const uint8_t cmd_req1[] = {0xFF, 0xFE, 0x00, 0x8E, 0x01, 0x8F};
// cmd_req2 e cmd_get_data são o mesmo frame; cmd_get_data é reutilizado na inicialização
static const uint8_t cmd_get_data[] = {0xAA, 0x04, 0x00, 0x80, 0x1E, 0x9E};

static const usb_device_desc_t *dev_desc;

// Globais acessíveis pelo servidor web e MQTT
volatile ups_metricts_t g_ups_metrics = {0};
volatile ups_status_t   g_ups_status  = {0};
volatile bool           g_ups_connected = false;

uint8_t  g_ups_raw_buf[UPS_RAW_BUF_SIZE] = {0};
size_t   g_ups_raw_len = 0;

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

#define NVS_NAMESPACE   "ups_energy"
#define NVS_KEY_KWH_OUT "kwh_out"
#define NVS_KEY_KWH_IN  "kwh_in"
// Salva a cada 60 s para não desgastar a flash
#define ENERGY_SAVE_INTERVAL_S  60

static void energy_load(float *kwh_out, float *kwh_in)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    uint32_t raw;
    if (nvs_get_u32(h, NVS_KEY_KWH_OUT, &raw) == ESP_OK)
        memcpy(kwh_out, &raw, sizeof(float));
    if (nvs_get_u32(h, NVS_KEY_KWH_IN, &raw) == ESP_OK)
        memcpy(kwh_in, &raw, sizeof(float));
    nvs_close(h);
}

static void energy_save(float kwh_out, float kwh_in)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    uint32_t raw;
    memcpy(&raw, &kwh_out, sizeof(float));
    nvs_set_u32(h, NVS_KEY_KWH_OUT, raw);
    memcpy(&raw, &kwh_in, sizeof(float));
    nvs_set_u32(h, NVS_KEY_KWH_IN, raw);
    nvs_commit(h);
    nvs_close(h);
}

// Processamento de potência: acumula energia e persiste no NVS periodicamente.
void processPower(float wOut, float wIn, float *power_current_kwh_out, float *power_current_kwh_in, int64_t *power_last_update)
{
    int64_t curr = esp_log_timestamp() / 1000; // segundos desde boot
    float diff = (float)(curr - *power_last_update);
    *power_last_update = curr;

    *power_current_kwh_out += (wOut / 1000.0f) * (diff / 3600.0f);
    *power_current_kwh_in  += (wIn  / 1000.0f) * (diff / 3600.0f);

    // Persistência periódica no NVS
    static int64_t last_save = 0;
    if (curr - last_save >= ENERGY_SAVE_INTERVAL_S) {
        last_save = curr;
        energy_save(*power_current_kwh_out, *power_current_kwh_in);
    }
}

// Decodifica o buffer de resposta do UPS.
// Mapeamento EMPÍRICO verificado com buffer hex real (31 bytes totais).
// Correções confirmadas por consistência cruzada com alarmes de status:
//
//  buffer[0x08]  V_CBATTERY  x0.3920  (— raw 0xFF = 100 %) consistente com max_battery
//  buffer[0x0B]  V_VBATTERY  x0.0671  (V)   bateria ~12 V
//  buffer[0x0C]  V_VINPUT    direto   (V)
//  buffer[0x0D]  V_IOUTPUT   x0.1152  (INF) / x0.0510 (SEN)
//  buffer[0x0F]  V_TEMPER    direto   (°C)  consistente com alarme Sobretemperatura
//  buffer[0x10]  V_FOUTPUT   x0.5825  (Hz)
//  buffer[0x13]  flags falha (bits 0-7)
//  buffer[0x14]  flags entrada (bits 0-7)  — confirmado via cmd_req1 (0x008E)
//  buffer[0x15]  flags operacional (bits 3-7)
//  buffer[0x17]  flags bateria/timers (bits 0,1,4)
//  buffer[0x18]  V_POUTPUT   direto   (%)   carga da saída
//  buffer[0x1B]  flags controle (bits 3,5)
//  buffer[0x1E]  V_VOUTPUT   x0.5550  (INF) / x0.6000 (SEN)

ups_metricts_t build_payload(const uint8_t *buffer, char *output_json, size_t output_size)
{
    static float   power_current_kwh_out = 0.0f;
    static float   power_current_kwh_in  = 0.0f;
    static int64_t power_last_update     = 0;

    if (power_last_update == 0) {
        energy_load(&power_current_kwh_out, &power_current_kwh_in);
        power_last_update = esp_log_timestamp() / 1000;
    }

    // ── Leituras analógicas ───────────────────────────────────────────────────
    float vIn  = (float)buffer[0x0C];               // V_VINPUT  — direto (V)

#if defined(CONFIG_UPS_FAMILY_SENIUM)
    float vOut = buffer[0x1E] * 0.6000f;            // V_VOUTPUT SENIUM
    float iOut = buffer[0x0D] * 0.0510f;            // V_IOUTPUT SENIUM
#else   // INFINIUM (padrão)
    float vOut = buffer[0x1E] * 0.5550f;            // V_VOUTPUT INFINIUM [buf[30]=198 → 110 V]
    float iOut = buffer[0x0D] * 0.1152f;            // V_IOUTPUT INFINIUM [buf[13]=9  →  1.0 A]
#endif

    float temp = (float)buffer[0x0F];               // V_TEMPER  — direto (°C) [buf[15]=47 → 47°C]
    float freq = buffer[0x10] * 0.5825f;            // V_FOUTPUT — Hz         [buf[16]=101 → 58.8 Hz]
    float vBat = buffer[0x0B] * 0.0671f;            // V_VBATTERY — V         [buf[11]=199 → 13.4 V]
    float cBat = buffer[0x08] * 0.3920f;            // V_CBATTERY — x0.392    [buf[8]=255  → 100%]

    // Carga real calculada a partir da potência medida vs potência nominal configurada.
    // pPct = (iOut × vOut) / (VA × PF) × 100
    const float rated_w = (float)CONFIG_UPS_VA_RATING * ((float)CONFIG_UPS_POWER_FACTOR / 100.0f);
    float wOut = iOut * vOut;
    float wIn  = iOut * vIn;
    float pPct = (rated_w > 0.0f) ? (wOut / rated_w * 100.0f) : 0.0f;
    if (pPct > 100.0f) pPct = 100.0f;

    processPower(wOut, wIn, &power_current_kwh_out, &power_current_kwh_in, &power_last_update);

    snprintf(output_json, output_size,
        "{"
        "\"Power_Out_Percent\":{\"value\":%.1f,\"unit\":\"%%\"},"
        "\"Current_Out\":{\"value\":%.3f,\"unit\":\"A\"},"
        "\"Voltage_Out\":{\"value\":%.1f,\"unit\":\"V\"},"
        "\"Voltage_In\":{\"value\":%.1f,\"unit\":\"V\"},"
        "\"Power_Out\":{\"value\":%.1f,\"unit\":\"W\"},"
        "\"Power_In\":{\"value\":%.1f,\"unit\":\"W\"},"
        "\"Energy_Out\":{\"value\":%.3f,\"unit\":\"kWh\"},"
        "\"Energy_In\":{\"value\":%.3f,\"unit\":\"kWh\"},"
        "\"Temperature\":{\"value\":%.1f,\"unit\":\"C\"},"
        "\"Battery_State\":{\"value\":%.1f,\"unit\":\"%%\"},"
        "\"Battery_Voltage\":{\"value\":%.2f,\"unit\":\"V\"},"
        "\"Frequency\":{\"value\":%.2f,\"unit\":\"Hz\"}"
        "}",
        pPct, iOut, vOut, vIn, wOut, wIn,
        power_current_kwh_out, power_current_kwh_in,
        temp, cBat, vBat, freq);

    ups_metricts_t metrics = {
        .power_out_percent = pPct,
        .current_out       = iOut,
        .voltage_out       = vOut,
        .voltage_in        = vIn,
        .power_out         = wOut,
        .power_in          = wIn,
        .energy_out        = power_current_kwh_out,
        .energy_in         = power_current_kwh_in,
        .temperature       = temp,
        .battery_state     = cBat,
        .battery_voltage   = vBat,
        .frequency         = freq,
    };
    return metrics;
}

// Decodifica os bytes de status/flags do buffer de resposta.
void parse_status_flags(const uint8_t *buf, ups_status_t *s)
{
    uint8_t fail = buf[0x13];   // reg 0x008D — falhas
    uint8_t inp  = buf[0x14];   // reg 0x008E — entrada
    uint8_t op   = buf[0x15];   // reg 0x008F — operacional
    uint8_t bat  = buf[0x17];   // reg 0x0091 — bateria/timers
    uint8_t ctrl = buf[0x1B];   // reg 0x0095 — controle

    s->fail_overtemp      = (fail >> 0) & 1;
    s->fail_internal      = (fail >> 1) & 1;
    s->fail_overload      = (fail >> 3) & 1;
    s->fail_abnormal_vout = (fail >> 5) & 1;
    s->fail_abnormal_vbat = (fail >> 6) & 1;
    s->fail_inverter      = (fail >> 7) & 1;

#if defined(CONFIG_UPS_FAMILY_SENIUM)
    s->fail_end_battery   = (fail >> 2) & 1;
    s->fail_shortcircuit  = (fail >> 4) & 1;
#else   // INFINIUM
    s->fail_shortcircuit  = (fail >> 2) & 1;
    s->fail_end_battery   = (fail >> 4) & 1;
#endif

    s->lo_f_input    = (inp >> 0) & 1;
    s->hi_f_input    = (inp >> 1) & 1;
    s->no_sync_input = (inp >> 2) & 1;
    s->lo_v_input    = (inp >> 3) & 1;
    s->hi_v_input    = (inp >> 4) & 1;
    s->no_v_input    = (inp >> 5) & 1;
    s->lo_battery    = (inp >> 6) & 1;
    s->noise_input   = (inp >> 7) & 1;

    s->op_battery  = (op >> 3) & 1;
    s->op_stand_by = (op >> 4) & 1;
    s->op_warning  = (op >> 5) & 1;
    s->op_startup  = (op >> 6) & 1;
    s->op_checkup  = (op >> 7) & 1;

    s->sync_input            = (bat >> 0) & 1;
    s->max_battery           = (bat >> 1) & 1;
    s->shutdown_timer_active = (bat >> 4) & 1;

    s->remote_control_active = (ctrl >> 3) & 1;
    s->vin_sel_220v          = (ctrl >> 5) & 1;
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
        if (xBufferIndex >= UPS_MIN_PACKET_SIZE)
        {
            // Salva buffer raw para diagnóstico via /api/raw
            size_t copy_len = xBufferIndex < UPS_RAW_BUF_SIZE ? xBufferIndex : UPS_RAW_BUF_SIZE;
            memcpy(g_ups_raw_buf, xBuffer, copy_len);
            g_ups_raw_len = copy_len;

            char json_payload[512];
            ups_metricts_t metrics = build_payload(xBuffer, json_payload, sizeof(json_payload));
            parse_status_flags(xBuffer, (ups_status_t *)&g_ups_status);
            g_ups_metrics = metrics;
            publish_metrics(&metrics);
            publish_status((const ups_status_t *)&g_ups_status);
            ESP_LOGI(TAG, "Payload: %s", json_payload);
            xBufferIndex = 0;
        }
    }
    else
    {
        ESP_LOGE(TAG, "Buffer overflow");
        xBufferIndex = 0; // Reset buffer to avoid undefined behavior
    }

    return true;
}

// Callback para processar novo dispositivo
void handle_newdev(usb_device_handle_t usb_dev)
{
    // O descritor é gerenciado internamente pelo USB Host; não aloca memória própria
    esp_err_t err = usb_host_get_device_descriptor(usb_dev, &dev_desc);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Falha ao obter descritor do dispositivo: %s", esp_err_to_name(err));
        dev_desc = NULL;
        return;
    }

    ESP_LOGI(TAG, "Dispositivo conectado: VID=0x%04X, PID=0x%04X", dev_desc->idVendor, dev_desc->idProduct);

    lConected = true;
    g_ups_connected = true;
    led_status_set(LED_STATE_ALL_OK);
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
        g_ups_connected = false;
        led_status_set(LED_STATE_UPS_DISCONNECTED);
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
                // Garante DTR=0 e RTS=0 imediatamente após a abertura para não
                // sinalizar desligamento ao nobreak via linhas de controle serial
                ESP_ERROR_CHECK(cdc_acm_host_set_control_line_state(cdc_dev, false, false));
                vTaskDelay(pdMS_TO_TICKS(200));

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
                vTaskDelay(pdMS_TO_TICKS(500));

                // Limpa buffer antes de enviar comandos (descarta dados da enumeração USB)
                xBufferIndex = 0;

                // Envia comandos e solicita dados
                ESP_LOGI(TAG, "Send CMD1...");
                ESP_LOG_BUFFER_HEXDUMP(TAG, cmd_req1, sizeof(cmd_req1), ESP_LOG_VERBOSE);
                if (lConected)
                {
                    ESP_ERROR_CHECK(cdc_acm_host_data_tx_blocking(cdc_dev, cmd_req1, sizeof(cmd_req1), EXAMPLE_TX_TIMEOUT_MS));
                };

                vTaskDelay(pdMS_TO_TICKS(100));

                ESP_LOGI(TAG, "Send CMD2...");
                ESP_LOG_BUFFER_HEXDUMP(TAG, cmd_get_data, sizeof(cmd_get_data), ESP_LOG_VERBOSE);

                if (lConected)
                {
                    ESP_ERROR_CHECK(cdc_acm_host_data_tx_blocking(cdc_dev, cmd_get_data, sizeof(cmd_get_data), EXAMPLE_TX_TIMEOUT_MS));
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