#include "log_buffer.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ── Configuração ──────────────────────────────────────────────────────────────
// Número máximo de entradas no buffer circular
#define LOG_ENTRIES     150
// Largura máxima de cada linha de log (truncada se maior)
#define LOG_LINE_LEN    160

// ── Buffer circular ───────────────────────────────────────────────────────────
static char     s_buf[LOG_ENTRIES][LOG_LINE_LEN];
static int      s_head = 0;      // próxima posição de escrita
static int      s_count = 0;     // quantas entradas válidas existem
static SemaphoreHandle_t s_lock = NULL;

// Função original do esp_log (serial)
static vprintf_like_t s_orig_vprintf = NULL;

// ── Hook de log ───────────────────────────────────────────────────────────────

static int log_hook(const char *fmt, va_list args)
{
    // 1. Escreve no UART original (sem interromper o monitor serial)
    int ret = 0;
    if (s_orig_vprintf) {
        va_list args2;
        va_copy(args2, args);
        ret = s_orig_vprintf(fmt, args2);
        va_end(args2);
    }

    // 2. Formata para o buffer (sem alinhamento de tempo extra — o esp_log já insere)
    char line[LOG_LINE_LEN];
    vsnprintf(line, sizeof(line), fmt, args);

    // Remove newline final para não duplicar no HTML
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
        line[--len] = '\0';
    }
    if (len == 0) return ret;

    if (s_lock && xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
        strncpy(s_buf[s_head], line, LOG_LINE_LEN - 1);
        s_buf[s_head][LOG_LINE_LEN - 1] = '\0';
        s_head = (s_head + 1) % LOG_ENTRIES;
        if (s_count < LOG_ENTRIES) s_count++;
        xSemaphoreGive(s_lock);
    }

    return ret;
}

// ── API pública ───────────────────────────────────────────────────────────────

void log_buffer_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_orig_vprintf = esp_log_set_vprintf(log_hook);
}

char *log_buffer_to_json(void)
{
    // Estimativa de tamanho: cada entrada até LOG_LINE_LEN chars + escaping + overhead JSON
    // Usamos 2× LOG_LINE_LEN para acomodar JSON escaping em casos patológicos
    size_t alloc = (size_t)LOG_ENTRIES * (LOG_LINE_LEN * 2 + 8) + 32;
    char *out = malloc(alloc);
    if (!out) return NULL;

    int pos = 0;
    pos += snprintf(out + pos, alloc - pos, "{\"logs\":[");

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        int total = s_count;
        // Índice da entrada mais antiga
        int start = (s_count < LOG_ENTRIES) ? 0 : s_head;

        for (int i = 0; i < total && pos < (int)alloc - 4; i++) {
            int idx = (start + i) % LOG_ENTRIES;
            if (i > 0) {
                out[pos++] = ',';
            }
            out[pos++] = '"';
            // Escapa caracteres especiais JSON
            const char *src = s_buf[idx];
            while (*src && pos < (int)alloc - 4) {
                char c = *src++;
                if (c == '"')       { out[pos++] = '\\'; out[pos++] = '"'; }
                else if (c == '\\') { out[pos++] = '\\'; out[pos++] = '\\'; }
                else if (c == '\n') { out[pos++] = '\\'; out[pos++] = 'n'; }
                else if (c == '\r') { out[pos++] = '\\'; out[pos++] = 'r'; }
                else if (c == '\t') { out[pos++] = '\\'; out[pos++] = 't'; }
                else if ((unsigned char)c < 0x20) { /* ignora outros ctrl */ }
                else                { out[pos++] = c; }
            }
            out[pos++] = '"';
        }
        xSemaphoreGive(s_lock);
    }

    pos += snprintf(out + pos, alloc - pos, "]}");
    return out;
}
