# Manual de Comunicação — UPS Ragtech

Documentação do protocolo de comunicação USB-CDC dos nobreaks Ragtech e mapeamento de registradores utilizado neste firmware.

---

## 1. Interface USB

Os nobreaks Ragtech suportados usam interface **USB CDC/ACM** (emulação de porta serial):

| Parâmetro | Valor |
|---|---|
| VID (USB Vendor ID) | `0x04D8` |
| PID (USB Product ID) | `0x000A` |
| Baud rate | **2560 bps** (padrão) / 2400 / 2048 |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |
| Timeout | 100 ms |

O firmware testa as baud rates por ordem de prioridade; 2560 é o padrão para a maioria dos modelos.

---

## 2. Famílias de hardware

| Família | Modelos | Baud | Registrador temperatura |
|---|---|---|---|
| INFINIUM DIGITAL (padrão) | Easy Pro 2200+, Infinity | 2560 | `0x0090` |
| SENIUM | Easy Pro 1000–1600 VA | 2560 | `0x0097` |

A família é selecionada em `idf.py menuconfig → UPS Configuration → UPS hardware family`.

---

## 3. Protocolo de leitura

O firmware envia dois frames na inicialização e depois um frame de polling periódico:

| Frame | Bytes | Propósito |
|---|---|---|
| Inicialização 1 | `FF FE 00 8E 01 8F` | Identificação/wake-up |
| Inicialização 2 / Poll | `AA 04 00 80 1E 9E` | Leitura de 30 registradores a partir de 0x0080 |

O frame de poll é repetido a cada `CONFIG_UPS_POLL_INTERVAL_MS` (padrão: 5000 ms).

### Estrutura da resposta

A resposta possui **31 bytes** totais (1 byte SOF `0xAA` + 5 bytes header + 25 bytes de dados), verificado empiricamente. O mapeamento de offsets foi determinado por análise do buffer real do dispositivo, não por especificação formal.

```
[SOF: 1 byte][Header: 5 bytes][Dados: 25 bytes]
Total: 31 bytes
```

> O firmware processa o buffer quando recebe **≥ 31 bytes** (`UPS_MIN_PACKET_SIZE = 31`).

---

## 4. Mapa de registradores analógicos

> **Mapeamento empírico** — verificado com buffer hex real: `AA 09 00 0C 4E 00 ... C6`  
> Valores confirmados na interface web: `vIn=196V`, `pPct=47%`, `temp=1°C`, `cBat=72%`.

| Offset no buffer | Byte hex | Valor raw | Fórmula | Resultado | Campo JSON |
|---|---|---|---|---|---|
| `buf[0x08]` | `FF` | 255 | ×0.3920 | ~100 % | `battery_state` (%) |
| `buf[0x0B]` | `C8` | 200 | ×0.0671 | ~13.4 V | `battery_voltage` (V) |
| `buf[0x0C]` | `C4` | 196 | direto | 196 V | `voltage_in` (V) |
| `buf[0x0D]` | `09` | 9 | ×0.1152 (INF) / ×0.0510 (SEN) | ~1.0 A | `current_out` (A) |
| `buf[0x0F]` | `2F` | 47 | direto | 47 °C | `temperature` (°C) — consistente com alarme Sobretemperatura |
| `buf[0x10]` | `67` | 103 | ×0.5825 | ~60 Hz | `frequency` (Hz) |
| `buf[0x18]` | `48` | 72 | direto | 72 % | `power_out_percent` (%) |
| `buf[0x1E]` | `C6` | 198 | ×0.5550 (INF) / ×0.6000 (SEN) | ~110 V | `voltage_out` (V) |

> `power_out` (W) e `power_in` (W) são calculados: `current_out × voltage_out` e `current_out × voltage_in`.  
> `energy_out` / `energy_in` (kWh) são acumulados desde o boot.

---

## 5. Mapa de registradores de flags (status / alarmes)

### `buf[0x13]` — reg `0x008D` — Falhas de hardware

| Bit | Campo | Descrição |
|---|---|---|
| 0 | `fail_overtemp` | Sobretemperatura |
| 1 | `fail_internal` | Falha interna |
| 2 | `fail_shortcircuit` (INF) / `fail_end_battery` (SEN) | Curto-circuito / Bateria esgotada |
| 3 | `fail_overload` | Sobrecarga |
| 4 | `fail_end_battery` (INF) / `fail_shortcircuit` (SEN) | Bateria esgotada / Curto-circuito |
| 5 | `fail_abnormal_vout` | Tensão de saída anormal |
| 6 | `fail_abnormal_vbat` | Tensão de bateria anormal |
| 7 | `fail_inverter` | Falha no inversor |

### `buf[0x14]` — reg `0x008E` — Status da rede elétrica

| Bit | Campo | Descrição |
|---|---|---|
| 0 | `lo_f_input` | Frequência de entrada baixa |
| 1 | `hi_f_input` | Frequência de entrada alta |
| 2 | `no_sync_input` | Sem sincronismo |
| 3 | `lo_v_input` | Tensão de entrada baixa |
| 4 | `hi_v_input` | Tensão de entrada alta |
| 5 | `no_v_input` | **Sem tensão de entrada (queda de energia)** |
| 6 | `lo_battery` | **Bateria baixa** |
| 7 | `noise_input` | Ruído na entrada |

### `buf[0x15]` — reg `0x008F` — Status operacional

| Bit | Campo | Descrição |
|---|---|---|
| 3 | `op_battery` | **Operando na bateria** |
| 4 | `op_stand_by` | Modo stand-by |
| 5 | `op_warning` | Aviso geral |
| 6 | `op_startup` | Inicializando |
| 7 | `op_checkup` | Em diagnóstico |

### `buf[0x17]` — reg `0x0091` — Timers e bateria

| Bit | Campo | Descrição |
|---|---|---|
| 0 | `sync_input` | Sincronismo na entrada |
| 1 | `max_battery` | Bateria completamente carregada |
| 4 | `shutdown_timer_active` | Timer de desligamento ativo |

### `buf[0x1B]` — reg `0x0095` — Controle remoto

| Bit | Campo | Descrição |
|---|---|---|
| 3 | `remote_control_active` | Controle remoto ativo |
| 5 | `vin_sel_220v` | Tensão nominal entrada = 220 V (se 0 = 115 V) |

---

## 6. Campos JSON publicados via MQTT

### Sensores analógicos (`sensor`)

| Campo JSON | Unidade | Descrição |
|---|---|---|
| `voltage_in` | V | Tensão de entrada |
| `voltage_out` | V | Tensão de saída |
| `current_out` | A | Corrente de saída |
| `power_out_percent` | % | Carga de saída |
| `power_out` | W | Potência de saída |
| `power_in` | W | Potência de entrada |
| `energy_out` | kWh | Energia de saída acumulada |
| `energy_in` | kWh | Energia de entrada acumulada |
| `frequency` | Hz | Frequência de saída |
| `battery_voltage` | V | Tensão da bateria |
| `battery_state` | % | Carga da bateria |
| `temperature` | °C | Temperatura interna |

### Flags de status (`binary_sensor`)

Publicados como `ON` / `OFF`:

`op_battery` · `no_v_input` · `lo_v_input` · `hi_v_input` · `lo_battery` · `max_battery` · `op_stand_by` · `op_warning` · `shutdown_timer_active` · `remote_control_active` · `fail_overtemp` · `fail_overload` · `fail_inverter` · `fail_end_battery` · `fail_shortcircuit`

---

## 7. Tópicos MQTT

```
UPS_ESP32_tinySrv/<MAC>/availability
UPS_ESP32_tinySrv/<MAC>/Sensor_<campo>
UPS_ESP32_tinySrv/<MAC>/status/<flag>

homeassistant/sensor/<MAC>/<campo>/config        ← auto-discovery HA
homeassistant/binary_sensor/<MAC>/<flag>/config  ← auto-discovery HA
```

`<MAC>` = endereço MAC do ESP32 em hexadecimal sem separadores (ex: `A1B2C3D4E5F6`).

