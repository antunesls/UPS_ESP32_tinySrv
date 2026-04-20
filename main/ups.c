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

// Família 10 — NEP/TORO/INNERGIE/ONEUP (Easy Pro 1200 TI, modelo 6)
// Pacote: 1 byte SOF (0xAA) + 30 bytes de dados = 31 bytes.
// Range solicitado: AA 04 00 80 1E 9E  →  reg 0x0080..0x009D (30 registradores)
// Fórmula de índice: buf[N] = reg(0x80 + N - 1), N ∈ [1..30]
//
// Mapeamento confirmado com pacote real e cross-ref com devices.xml família 10:
//
//  buf[ 8] = reg 0x0087 → V_CBATTERY   × 0.3930  → % bateria   (0xFF×0.393≈100%)
//  buf[11] = reg 0x008A → V_VBATTERY   × 0.0670  → V bateria   (0xC7×0.067≈13.3V)
//  buf[12] = reg 0x008B → V_VINPUT     × 1.0600  → V entrada   (0xC7×1.06≈210.9V)
//  buf[13] = reg 0x008C → V_IOUTPUT    × 6.3700  → A saída     (÷V_IOUTCALIB≈16)
//  buf[14] = reg 0x008D → V_POUTPUT    direto    → % carga
//  buf[15] = reg 0x008E → V_TEMPER     direto    → °C
//  buf[24] = reg 0x0097 → V_FOUTPUT    (OSC, não disponível neste range → 60Hz fixo)
//  buf[30] = reg 0x009D → V_VOUTPUT    × 0.5550  → V saída     (0xC9×0.555≈111.6V)
//  buf[27] = reg 0x009A → V_MODEL      direto    → 6 = EASY 1200 TI ✓
//
//  Flags (SOF=1 byte, dados a partir de buf[1]):
//  buf[17] = reg 0x0090 → F_NOBAT[0], F_OLDBAT[1], F_OPCHECKUP[2], F_NOVINPUT[3],
//                          F_LOVINPUT[4], F_HIVINPUT[5], F_OPBATTERY[6], F_HIPOUTPUT[7]
//  buf[18] = reg 0x0091 → F_LOBATTERY[0], F_FOVERTEMP[1], F_FENDBATTERY[2],
//                          F_FOVERLOAD[3], F_FABNORMALVOUT[4], F_FABNORMALVBAT[5],
//                          F_FINVERTER[6], F_FSHORTCIRCUIT[7]
//  buf[19] = reg 0x0092 → F_SYNCIN[0], F_SUPERVON[1], F_MOREBAT[2],
//                          F_LESSBAT[3], F_RCTRLON[5]
//
//  V_IOUTCALIB (reg 0x00F3, range separado) — ajustável via menuconfig.
#define UPS_MIN_PACKET_SIZE 31

static const uint8_t cmd_req1[] = {0xFF, 0xFE, 0x00, 0x8E, 0x01, 0x8F};
// Lê 30 registradores a partir de 0x80 (família 10: range 0x0080 size=30)
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
// Mapeamento extraido atraves de engenharia reversa do software Supsvc.exe / device.dll
// Considera pacote serial completo onde os 6 primeiros bytes representam o cabecalho.
// O endereco base 0x80 da familia mapeia exatamente em buffer[6].
// Logo, buffer_index = 6 + (ragtech_address - 0x80)

ups_metricts_t build_payload(const uint8_t *buffer, char *output_json, size_t output_size)
{
    static float   power_current_kwh_out = 0.0f;
    static float   power_current_kwh_in  = 0.0f;
    static int64_t power_last_update     = 0;

    // Família 10 — Easy Pro 1200 TI (modelo 6) ————————————————————————————
    // SOF = buf[0] = 0xAA; dados em buf[1..30] = reg 0x80..0x9D
    // Fórmulas de devices.xml família 10, offsets validados com pacote real.

    float cBat = (float)buffer[ 8] * 0.3930f;   // reg 0x87 V_CBATTERY ×0.393 → %
    float vBat = (float)buffer[11] * 0.0670f;   // reg 0x8A V_VBATTERY ×0.067 → V
    float vIn  = (float)buffer[12] * 1.0600f;   // reg 0x8B V_VINPUT   ×1.06  → V
    float calib = (float)CONFIG_UPS_IOUT_CALIB;  // reg 0xF3 V_IOUTCALIB (range separado)
    float iOut = (float)buffer[13] * 6.3700f / ((calib > 0.0f) ? calib : 1.0f); // reg 0x8C
    float pPct = (float)buffer[14];             // reg 0x8D V_POUTPUT  direto → %
    float temp = (float)buffer[15];             // reg 0x8E V_TEMPER   direto → °C
    float freq = 60.0f;                         // reg 0x97 V_FOUTPUT OSC (range separado) → 60Hz
    float vOut = (float)buffer[30] * 0.5550f;   // reg 0x9D V_VOUTPUT  ×0.555 → V

    // Carga de potência (W)
    float wOut = iOut * vOut;
    float wIn  = iOut * vIn;

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
// Família 10 — Easy Pro 1200 TI (devices.xml família 10)
void parse_status_flags(const uint8_t *buf, ups_status_t *s)
{
    // buf[17] = reg 0x0090 — status operacional e entrada
    // buf[18] = reg 0x0091 — falhas de hardware
    // buf[19] = reg 0x0092 — flags complementares
    uint8_t op   = buf[17];  // reg 0x0090
    uint8_t fail = buf[18];  // reg 0x0091
    uint8_t misc = buf[19];  // reg 0x0092

    // reg 0x0090
    s->no_battery            = (op >> 0) & 1;  // F_NOBAT
    s->old_battery           = (op >> 1) & 1;  // F_OLDBAT
    s->op_checkup            = (op >> 2) & 1;  // F_OPCHECKUP
    s->no_v_input            = (op >> 3) & 1;  // F_NOVINPUT
    s->lo_v_input            = (op >> 4) & 1;  // F_LOVINPUT
    s->hi_v_input            = (op >> 5) & 1;  // F_HIVINPUT
    s->op_battery            = (op >> 6) & 1;  // F_OPBATTERY
    s->hi_p_output           = (op >> 7) & 1;  // F_HIPOUTPUT

    // reg 0x0091
    s->lo_battery            = (fail >> 0) & 1;  // F_LOBATTERY
    s->fail_overtemp         = (fail >> 1) & 1;  // F_FOVERTEMP
    s->fail_end_battery      = (fail >> 2) & 1;  // F_FENDBATTERY
    s->fail_overload         = (fail >> 3) & 1;  // F_FOVERLOAD
    s->fail_abnormal_vout    = (fail >> 4) & 1;  // F_FABNORMALVOUT
    s->fail_abnormal_vbat    = (fail >> 5) & 1;  // F_FABNORMALVBAT
    s->fail_inverter         = (fail >> 6) & 1;  // F_FINVERTER
    s->fail_shortcircuit     = (fail >> 7) & 1;  // F_FSHORTCIRCUIT

    // reg 0x0092
    s->sync_input            = (misc >> 0) & 1;  // F_SYNCIN
    s->more_battery          = (misc >> 2) & 1;  // F_MOREBAT
    s->less_battery          = (misc >> 3) & 1;  // F_LESSBAT
    s->remote_control_active = (misc >> 5) & 1;  // F_RCTRLON
}

// Callback de dados recebidos
bool handle_rx(const uint8_t *data, size_t data_len, void *user_arg)
{
    if (xBufferIndex + data_len < sizeof(xBuffer))
    {
        memcpy(xBuffer + xBufferIndex, data, data_len);
        xBufferIndex += data_len;

        ESP_LOGI(TAG, "Dados buffer %.*s", (int)xBufferIndex, xBuffer);
        ESP_LOGI(TAG, "Tamanho buffer %u", xBufferIndex);

        // Sincroniza no SOF (0xAA) — descarta bytes iniciais se desalinhado
        while (xBufferIndex > 0 && xBuffer[0] != 0xAA) {
            memmove(xBuffer, xBuffer + 1, xBufferIndex - 1);
            xBufferIndex--;
        }

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