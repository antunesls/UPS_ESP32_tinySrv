#include "ups.h"
#include "ups_mqtt.h"
#include "led_status.h"
#include "nvs_flash.h"
#include "nvs.h"

// Fallback para quando o sdkconfig ainda não gerou os símbolos (análise de IDE)
#ifndef CONFIG_UPS_IOUT_CALIB
#define CONFIG_UPS_IOUT_CALIB   16
#endif
#ifndef CONFIG_UPS_POLL_INTERVAL_MS
#define CONFIG_UPS_POLL_INTERVAL_MS 5000
#endif

//====

#define EXAMPLE_USB_HOST_PRIORITY (10)
#define EXAMPLE_TX_TIMEOUT_MS (300)

//==============

static const char *TAG = "UPS-Srv";

static SemaphoreHandle_t device_disconnected_sem;
static SemaphoreHandle_t rx_sem;
static portMUX_TYPE rx_mux = portMUX_INITIALIZER_UNLOCKED;

// ─────────────────────────────────────────────────────────────────────────────
// Protocolo serial Ragtech — extraído do Supervise (device.dll, Ghidra 12.1.4)
//
// Vtable da classe de dispositivo @ 0x6f55ef48. Todo quadro enviado tem 6 bytes
// (slot 0x30, FUN_6f4e2680):
//     [hdr, cmd, addr_hi, addr_lo, arg, chk]
// hdr = 0xA0 | ID (ID = nibble baixo devolvido na identificação).
//
//  - slot 0x34 (FUN_6f4e0800): chk = soma dos 5 bytes            → protocolo 3
//  - slot 0x3c (FUN_6f4e08b0): chk = addr_hi + addr_lo + arg      → protocolo 4
//
// Comandos (cmd):
//  0x04 = ler 'arg' registradores a partir de addr (slot 0x98, FUN_6f4e1dd0)
//  0x01 = escrever byte, 0x02 = AND (limpa bits), 0x03 = OR (liga bits)
//  0xFE = identificação (enviado com hdr 0xAF, broadcast)
//
// Resposta de leitura (slot 0x48, FUN_6f4e27e0): espera 50 ms, lê arg+1 bytes;
// o 1º byte deve ter nibble alto 0xA, seguido de 'arg' bytes de dados.
// Não há checksum na resposta dos protocolos 3/4 — o "Checksum error" do
// device.dll pertence ao protocolo 2 (quadros "55 55 Ax ... ck_hi ck_lo").
//
// Sequência de detecção (FUN_6f4e1700 → 0x58 / 0x68):
//  1. AF FE 8E 01 00 3C           → resposta 0xAx: ID = x (FUN_6f4e0a30)
//  2. hdr 04 00 85 01 86 (slot 0x40) → resposta 0xAx: protocolo 4
//     senão hdr 04 00 85 01 sum (slot 0x38) → protocolo 3   (FUN_6f4e0f40)
// ─────────────────────────────────────────────────────────────────────────────

#define UPS_CMD_READ      0x04
#define UPS_CMD_IDENTIFY  0xFE
#define UPS_HDR_BROADCAST 0xAF
#define UPS_PROTO_SUM     3   // checksum = soma dos 5 bytes
#define UPS_PROTO_ADDR    4   // checksum = addr_hi + addr_lo + arg
#define UPS_ID_FALLBACK   0x0A
#define UPS_RX_TIMEOUT_MS 300

// Faixas de registradores lidas pelo Supervise para a família 10
// (devices.xml: <ranges> do device family="10")
typedef struct {
    uint16_t start;
    uint8_t  size;
} ups_range_t;

static const ups_range_t s_ranges[] = {
    {0x0080, 30},
    {0x00F3, 1},
    {0x0136, 1},
    {0x0171, 4},
    {0x0202, 2},
};
#define UPS_RANGE_COUNT (sizeof(s_ranges) / sizeof(s_ranges[0]))

// Mapa de registradores (endereços até 0x0203)
#define UPS_REG_SPACE 0x0204
static uint8_t s_regs[UPS_REG_SPACE];
static bool    s_range_ok[UPS_RANGE_COUNT];
#define REG(a) (s_regs[(a)])
#define REG_BIT(a, b) ((s_regs[(a)] >> (b)) & 1)

// Aliases da família 10 (devices.xml <map>)
#define V_CBATTERY  0x0087
#define V_VBATTERY  0x008A
#define V_VINPUT    0x008B
#define V_IOUTPUT   0x008C
#define V_POUTPUT   0x008D
#define V_TEMPER    0x008E
#define V_FOUTPUT   0x0097
#define V_MODEL     0x009A
#define V_VERSION   0x009B
#define V_VOUTPUT   0x009D
#define V_IOUTCALIB 0x00F3
#define V_OSC53     0x0202
#define V_OSC57     0x0203

// Modelos da família 10 (devices.xml <model>), já com herança "from" resolvida.
// TI = saída 115 V (vOut×0.555); M2 = saída 220 V (vOut×1.03).
// Modelos ≥ 8 usam banco de 24 V (vBat×0.134).
typedef struct {
    const char *name;
    uint16_t    p_nominal;  // C_POUTNOMINAL
    float       i_factor;   // iOutput = V_IOUTPUT × i_factor / V_IOUTCALIB
    bool        m2;
    bool        bat24;
} ups_model_t;

static const ups_model_t s_models[] = {
    {"EASY 600 TI",   600,  3.20f, false, false},
    {"EASY 600 M2",   600,  1.70f, true,  false},
    {"EASY 700 TI",   700,  3.20f, false, false},
    {"EASY 700 M2",   700,  2.30f, true,  false},
    {"EASY 900 TI",   900,  4.94f, false, false},
    {"EASY 900 M2",   900,  2.64f, true,  false},
    {"EASY 1200 TI", 1200,  6.37f, false, false},
    {"EASY 1200 M2", 1200,  3.41f, true,  false},
    {"EASY 1300 TI", 1300,  9.56f, false, true},
    {"EASY 1300 M2", 1300,  5.11f, true,  true},
    {"EASY 1400 TI", 1400, 11.15f, false, true},
    {"EASY 1400 M2", 1400,  5.96f, true,  true},
    {"EASY 1600 TI", 1600, 12.80f, false, true},
    {"EASY 1600 M2", 1600,  6.80f, true,  true},
    {"EASY 1800 TI", 1800, 14.40f, false, true},
    {"EASY 1800 M2", 1800,  7.65f, true,  true},
    {"EASY 2000 TI", 2000, 15.93f, false, true},
    {"EASY 2000 M2", 2000,  8.52f, true,  true},
    {"EASY 2200 TI", 2200, 17.52f, false, true},
    {"EASY 2200 M2", 2200,  9.37f, true,  true},
};
// <default> da família quando o modelo é desconhecido
static const ups_model_t s_model_default = {"NEP/TORO/INNERGIE/ONEUP", 0, 4.30f, false, false};

static const usb_device_desc_t *dev_desc;

// Globais acessíveis pelo servidor web e MQTT
volatile ups_metricts_t g_ups_metrics = {0};
volatile ups_status_t   g_ups_status  = {0};
volatile ups_info_t     g_ups_info    = {0};
volatile bool           g_ups_connected = false;

uint8_t  g_ups_raw_buf[UPS_RAW_BUF_SIZE] = {0};
size_t   g_ups_raw_len = 0;

static bool lConected = false;

static uint8_t xBuffer[512];
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

static const ups_model_t *current_model(void)
{
    uint8_t m = REG(V_MODEL);
    return (m < sizeof(s_models) / sizeof(s_models[0])) ? &s_models[m] : &s_model_default;
}

// Calcula as métricas a partir do mapa de registradores.
// Fórmulas de devices.xml família 10 (<vars> do <default> + override do <model>).
ups_metricts_t build_payload(char *output_json, size_t output_size)
{
    static float   power_current_kwh_out = 0.0f;
    static float   power_current_kwh_in  = 0.0f;
    static int64_t power_last_update     = 0;
    static bool    energy_loaded         = false;

    if (!energy_loaded) {
        energy_loaded = true;
        energy_load(&power_current_kwh_out, &power_current_kwh_in);
        power_last_update = esp_log_timestamp() / 1000;
    }

    const ups_model_t *mdl = current_model();

    // V_IOUTCALIB vem do range 0x00F3; o Kconfig só é usado se esse range falhar
    float calib = s_range_ok[1] ? (float)REG(V_IOUTCALIB) : (float)CONFIG_UPS_IOUT_CALIB;

    float cBat = REG(V_CBATTERY) * 0.3930f;                              // cBattery
    float vBat = REG(V_VBATTERY) * (mdl->bat24 ? 0.1340f : 0.0670f);     // vBattery
    float vIn  = REG(V_VINPUT) * 1.0600f;                                // vInput
    float vOut = REG(V_VOUTPUT) * (mdl->m2 ? 1.0300f : 0.5550f);         // vOutput
    float iOut = REG(V_IOUTPUT) * mdl->i_factor / ((calib > 0.0f) ? calib : 1.0f); // iOutput
    float pPct = REG(V_POUTPUT);                                         // pOutput
    float temp = REG(V_TEMPER);                                          // temperature

    // fOutput = (V_OSC57-V_OSC53)?((53+4*(V_FOUTPUT-V_OSC53)/(V_OSC57-V_OSC53))*0.9806+1.46):0
    float freq = 0.0f;
    int osc_span = (int)REG(V_OSC57) - (int)REG(V_OSC53);
    if (s_range_ok[4] && osc_span != 0) {
        freq = (53.0f + 4.0f * ((int)REG(V_FOUTPUT) - (int)REG(V_OSC53)) / (float)osc_span) * 0.9806f + 1.46f;
    }

    // O Supervise não expõe potência em W; estimativa mantida para energia/MQTT
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

// Decodifica as flags a partir do mapa de registradores (devices.xml família 10 <flags>)
void parse_status_flags(ups_status_t *s)
{
    // reg 0x0090
    s->no_battery            = REG_BIT(0x90, 0);  // F_NOBAT
    s->old_battery           = REG_BIT(0x90, 1);  // F_OLDBAT
    s->op_checkup            = REG_BIT(0x90, 2);  // F_OPCHECKUP
    s->no_v_input            = REG_BIT(0x90, 3);  // F_NOVINPUT
    s->lo_v_input            = s->no_v_input ? 0 : REG_BIT(0x90, 4);  // (F_NOVINPUT)?0:F_LOVINPUT
    s->hi_v_input            = REG_BIT(0x90, 5);  // F_HIVINPUT
    s->op_battery            = REG_BIT(0x90, 6);  // F_OPBATTERY
    s->hi_p_output           = REG_BIT(0x90, 7);  // F_HIPOUTPUT

    // reg 0x0091
    s->lo_battery            = REG_BIT(0x91, 0);  // F_LOBATTERY
    s->fail_overtemp         = REG_BIT(0x91, 1);  // F_FOVERTEMP
    s->fail_end_battery      = REG_BIT(0x91, 2);  // F_FENDBATTERY
    s->fail_overload         = REG_BIT(0x91, 3);  // F_FOVERLOAD
    s->fail_abnormal_vout    = REG_BIT(0x91, 4);  // F_FABNORMALVOUT
    s->fail_abnormal_vbat    = REG_BIT(0x91, 5);  // F_FABNORMALVBAT
    s->fail_inverter         = REG_BIT(0x91, 6);  // F_FINVERTER
    s->fail_shortcircuit     = REG_BIT(0x91, 7);  // F_FSHORTCIRCUIT

    // reg 0x0092
    s->sync_input            = REG_BIT(0x92, 0);  // F_SYNCIN
    s->supervise_on          = REG_BIT(0x92, 1);  // F_SUPERVON
    s->more_battery          = REG_BIT(0x92, 2);  // F_MOREBAT
    s->less_battery          = REG_BIT(0x92, 3);  // F_LESSBAT
    s->remote_control_active = REG_BIT(0x92, 5);  // F_RCTRLON
    s->powerlook_on          = REG_BIT(0x92, 6);  // F_POWERLOON
    s->nightoff_on           = REG_BIT(0x92, 7);  // F_NIGHTOFFON
}

// Callback de dados recebidos: apenas acumula; o parse é feito por ups_transact()
bool handle_rx(const uint8_t *data, size_t data_len, void *user_arg)
{
    portENTER_CRITICAL(&rx_mux);
    if (xBufferIndex + data_len <= sizeof(xBuffer)) {
        memcpy(xBuffer + xBufferIndex, data, data_len);
        xBufferIndex += data_len;
    } else {
        xBufferIndex = 0; // descarta para evitar overflow
    }
    portEXIT_CRITICAL(&rx_mux);

    xSemaphoreGive(rx_sem);
    return true;
}

static void build_frame(uint8_t *f, uint8_t proto, uint8_t hdr, uint8_t cmd, uint16_t addr, uint8_t arg)
{
    f[0] = hdr;
    f[1] = cmd;
    f[2] = (uint8_t)(addr >> 8);
    f[3] = (uint8_t)(addr & 0xFF);
    f[4] = arg;
    if (proto == UPS_PROTO_SUM)
        f[5] = (uint8_t)(f[0] + f[1] + f[2] + f[3] + f[4]);   // slot 0x34
    else
        f[5] = (uint8_t)(f[2] + f[3] + f[4]);                 // slot 0x3c
}

// Trava de segurança: o firmware é somente leitura. Só passam pedidos de leitura
// (cmd 0x04) e o quadro exato de identificação do Supervise. Qualquer outro quadro
// (escrita 0x01/0x02/0x03, shutdown, comandos desconhecidos) é bloqueado aqui, para
// que o dispositivo nunca altere o estado do nobreak nem o desligue.
static bool ups_frame_allowed(const uint8_t *f)
{
    static const uint8_t ident[6] = {UPS_HDR_BROADCAST, UPS_CMD_IDENTIFY, 0x8E, 0x01, 0x00, 0x3C};
    if (memcmp(f, ident, sizeof(ident)) == 0)
        return true;
    return (f[0] & 0xF0) == 0xA0 && f[0] != UPS_HDR_BROADCAST && f[1] == UPS_CMD_READ;
}

// Envia um quadro e aguarda 'expect' bytes de resposta.
// Retorna a quantidade recebida (cópia em 'resp') ou -1 em erro de envio.
static int ups_transact(cdc_acm_dev_hdl_t dev, const uint8_t *frame, uint8_t *resp, size_t expect)
{
    if (!ups_frame_allowed(frame)) {
        ESP_LOGE(TAG, "Quadro bloqueado (firmware somente leitura): %02X %02X %02X %02X %02X %02X",
                 frame[0], frame[1], frame[2], frame[3], frame[4], frame[5]);
        return -1;
    }

    portENTER_CRITICAL(&rx_mux);
    xBufferIndex = 0;
    portEXIT_CRITICAL(&rx_mux);
    xSemaphoreTake(rx_sem, 0);

    ESP_LOG_BUFFER_HEXDUMP(TAG, frame, 6, ESP_LOG_DEBUG);
    if (cdc_acm_host_data_tx_blocking(dev, frame, 6, EXAMPLE_TX_TIMEOUT_MS) != ESP_OK) {
        return -1;
    }

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(UPS_RX_TIMEOUT_MS);
    size_t got = 0;
    for (;;) {
        portENTER_CRITICAL(&rx_mux);
        got = xBufferIndex;
        portEXIT_CRITICAL(&rx_mux);
        if (got >= expect) break;
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline || !lConected) break;
        xSemaphoreTake(rx_sem, deadline - now);
    }

    portENTER_CRITICAL(&rx_mux);
    got = xBufferIndex < expect ? xBufferIndex : expect;
    memcpy(resp, xBuffer, got);
    portEXIT_CRITICAL(&rx_mux);
    ESP_LOG_BUFFER_HEXDUMP(TAG, resp, got, ESP_LOG_DEBUG);
    return (int)got;
}

static bool is_ack(uint8_t b)
{
    return (b & 0xF0) == 0xA0;
}

// Identificação + detecção do protocolo, como FUN_6f4e0a30 / FUN_6f4e0f40
static bool ups_detect(cdc_acm_dev_hdl_t dev, uint8_t *hdr, uint8_t *proto)
{
    uint8_t frame[6];
    uint8_t resp[16];

    // 1) Identificação: AF FE 8E 01 00 3C → resposta de 8+1 bytes, ID no nibble baixo
    build_frame(frame, UPS_PROTO_SUM, UPS_HDR_BROADCAST, UPS_CMD_IDENTIFY, 0x8E01, 0x00);
    int n = ups_transact(dev, frame, resp, 9);
    uint8_t id;
    if (n > 0 && is_ack(resp[0]) && (resp[0] & 0x0F) != 0) {
        id = resp[0] & 0x0F;
        ESP_LOGI(TAG, "Identificação: resposta 0x%02X → ID %u", resp[0], id);
    } else {
        id = UPS_ID_FALLBACK;
        ESP_LOGW(TAG, "Identificação sem ID válido (n=%d, 1º byte=0x%02X); usando ID padrão 0x%X",
                 n, n > 0 ? resp[0] : 0, id);
    }
    *hdr = 0xA0 | id;

    // 2) Protocolo 4: ler 1 registrador em 0x0085, checksum só de endereço/tamanho
    build_frame(frame, UPS_PROTO_ADDR, *hdr, UPS_CMD_READ, 0x0085, 1);
    n = ups_transact(dev, frame, resp, 2);
    if (n > 0 && is_ack(resp[0])) {
        *proto = UPS_PROTO_ADDR;
        return true;
    }

    // 3) Protocolo 3: mesmo pedido com checksum somando os 5 bytes
    build_frame(frame, UPS_PROTO_SUM, *hdr, UPS_CMD_READ, 0x0085, 1);
    n = ups_transact(dev, frame, resp, 2);
    if (n > 0 && is_ack(resp[0])) {
        *proto = UPS_PROTO_SUM;
        return true;
    }
    return false;
}

// Lê todas as faixas da família 10 para o mapa de registradores.
// Retorna false se a faixa principal (0x0080) falhar.
static bool ups_poll(cdc_acm_dev_hdl_t dev, uint8_t hdr, uint8_t proto)
{
    uint8_t frame[6];
    uint8_t resp[64];

    for (size_t i = 0; i < UPS_RANGE_COUNT && lConected; i++) {
        const ups_range_t *r = &s_ranges[i];
        build_frame(frame, proto, hdr, UPS_CMD_READ, r->start, r->size);
        int n = ups_transact(dev, frame, resp, r->size + 1);

        s_range_ok[i] = (n == r->size + 1) && is_ack(resp[0]);
        if (!s_range_ok[i]) {
            ESP_LOGW(TAG, "Falha lendo range 0x%04X/%u (n=%d, hdr=0x%02X)",
                     r->start, r->size, n, n > 0 ? resp[0] : 0);
            continue;
        }
        memcpy(&s_regs[r->start], &resp[1], r->size);

        // Faixa principal guardada para diagnóstico via /api/raw
        if (i == 0) {
            size_t copy_len = (size_t)n < UPS_RAW_BUF_SIZE ? (size_t)n : UPS_RAW_BUF_SIZE;
            memcpy(g_ups_raw_buf, resp, copy_len);
            g_ups_raw_len = copy_len;
        }
    }
    return s_range_ok[0];
}

static void publish_cycle(uint8_t hdr, uint8_t proto)
{
    char json_payload[512];
    ups_metricts_t metrics = build_payload(json_payload, sizeof(json_payload));
    parse_status_flags((ups_status_t *)&g_ups_status);
    g_ups_metrics = metrics;

    const ups_model_t *mdl = current_model();
    g_ups_info.id         = hdr & 0x0F;
    g_ups_info.protocol   = proto;
    g_ups_info.model      = REG(V_MODEL);
    g_ups_info.model_name = mdl->name;
    g_ups_info.p_nominal  = mdl->p_nominal;
    g_ups_info.version    = REG(V_VERSION) * 0.1f;

    publish_metrics(&metrics);
    publish_status((const ups_status_t *)&g_ups_status);
    ESP_LOGI(TAG, "Payload: %s", json_payload);
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
    switch (event->type)
    {
    case CDC_ACM_HOST_ERROR:
        ESP_LOGE(TAG, "Erro no CDC-ACM, err_no = %i", event->data.error);
        break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        ESP_LOGI(TAG, "Dispositivo desconectado");
        lConected = false;
        ESP_ERROR_CHECK(cdc_acm_host_close(event->data.cdc_hdl));
        g_ups_connected = false;
        led_status_set(LED_STATE_UPS_DISCONNECTED);
        xSemaphoreGive(rx_sem);
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

void ups_start(void *arg)
{
    //=================USB======================================

    device_disconnected_sem = xSemaphoreCreateBinary();
    assert(device_disconnected_sem);
    rx_sem = xSemaphoreCreateBinary();
    assert(rx_sem);

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

    ESP_ERROR_CHECK(cdc_acm_host_install(&cdc_acm_driver_config_default));

    const cdc_acm_host_device_config_t dev_config = {
        .connection_timeout_ms = 1000,
        .out_buffer_size = 4096,
        .in_buffer_size = 4096,
        .user_arg = NULL,
        .event_cb = handle_event,
        .data_cb = handle_rx};

    while (true)
    {

        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!lConected)
            continue;

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

        // Garante DTR=0 e RTS=0 imediatamente após a abertura para não
        // sinalizar desligamento ao nobreak via linhas de controle serial
        cdc_acm_host_set_control_line_state(cdc_dev, false, false);
        vTaskDelay(pdMS_TO_TICKS(200));

        // Primeira configuração serial de devices.xml: 2560 bps, 8N1, timeout 100 ms
        cdc_acm_line_coding_t line_coding = {
            .dwDTERate = 2560,
            .bDataBits = 8,
            .bParityType = 0,
            .bCharFormat = 0,
        };
        cdc_acm_host_line_coding_set(cdc_dev, &line_coding);
        ESP_LOGI(TAG, "Line Set: Rate: %" PRIu32 ", Stop bits: %" PRIu8 ", Parity: %" PRIu8 ", Databits: %" PRIu8 "",
                 line_coding.dwDTERate, line_coding.bCharFormat, line_coding.bParityType, line_coding.bDataBits);
        vTaskDelay(pdMS_TO_TICKS(500));

        uint8_t hdr = 0;
        uint8_t proto = 0;
        while (lConected)
        {
            if (proto == 0 && !ups_detect(cdc_dev, &hdr, &proto)) {
                ESP_LOGW(TAG, "Nobreak não respondeu à detecção de protocolo");
                proto = 0;
                vTaskDelay(pdMS_TO_TICKS(CONFIG_UPS_POLL_INTERVAL_MS));
                continue;
            }
            if (proto != 0 && hdr != g_ups_info.header) {
                g_ups_info.header = hdr;
                ESP_LOGI(TAG, "Protocolo %u, cabeçalho 0x%02X", proto, hdr);
            }

            if (ups_poll(cdc_dev, hdr, proto)) {
                publish_cycle(hdr, proto);
            } else {
                // Refaz a detecção no próximo ciclo
                proto = 0;
            }

            vTaskDelay(pdMS_TO_TICKS(CONFIG_UPS_POLL_INTERVAL_MS));
        }

        // Espera pela desconexão
        ESP_LOGI(TAG, "Aguardando desconexão");
        xSemaphoreTake(device_disconnected_sem, portMAX_DELAY);

        ESP_LOGI(TAG, "Dispositivo desconectado");
    }
}
