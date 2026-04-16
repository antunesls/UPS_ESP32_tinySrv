# Manual de Leitura de Dados — UPS Ragtech

Análise baseada na engenharia reversa de `supsvc.exe`, `monit.dll`, `config.dll` e `device.dll` (Ragtech).

---

## 1. Arquitetura do Sistema

```
Seu Projeto
    │
    ├─── Opção A: API HTTP local (serviço ainda rodando)
    │        └─── GET http://localhost:<porta>/api/...
    │
    └─── Opção B: Protocolo serial proprietário direto (sem o serviço)
             └─── COM x → Protocolo Ragtech por registradores
```

O sistema é dividido em **quatro camadas** (confirmado pela análise de `monit.dll.c`):

| Componente | Função |
|---|---|
| `supsvc.exe` | Gerenciador de serviço Windows + servidor HTTP JSON |
| `libmonit.dll` | Supervisão/monitoramento, banco SQLite, eventos e alarmes |
| `libconfig.dll` | Leitura do `devices.xml` — mapeamento de registradores por modelo |
| `libdevice.dll` | Comunicação serial/HID/CDC direta com o UPS (protocolo real) |

O protocolo serial está em **`libdevice.dll`** — a camada mais interna.

### Famílias de UPS suportadas

| Código family | Nome | Tipo de registradores |
|---|---|---|
| 0 | MICRON/INFINIUM (antigo) | Sinais RS-232 (DTR/CTS/DSR/RI/CD) |
| 1 | SENIUM | Registradores 0x0080–0x00CB |
| 2 | SENIUM (2200+) | Registradores 0x0080–0x00C9 |
| 3 | MICRON II / IF HOME | Registradores 0x0000–0x0018 |
| 4 | INFINIUM DIGITAL | Registradores 0x0080–0x00C9 |
| 5 | INFINIUM DIGITAL (2200+) | Registradores 0x0080–0x00C9 |
| 6 | PRECISION | Registradores 0x0080–0x0095 + 0x00D0 |
| 7 | SENIUM/ONEUP (com RGB LED) | Registradores 0x0080–0x0095 + 0x00C9 + 0x00B8 + 0x004B–0x004E |
| 8 | SENIUM (800-BA, TI, M2) | Registradores 0x0080–0x0097 + 0x00C9 + 0x00B8 |
| 9 | SENIUM ATM | Registradores 0x0080–0x0097 + 0x00DA |
| 10 | NEP/TORO/INNERGIE/ONEUP | — |
| 11 | FAMILIA 11 | — |
| 12 | SAVE USB | — |
| 13 | SAVE DIGITAL | — |
| 14 | NEW/QUADRI | — |
| 15 | EASY JET | — |

### Tipos de interface

| Código | Tipo | Identificação |
|---|---|---|
| 0 | Unknown | — |
| 1 | RS232 (serial) | Porta COM — baud 2400/2560/2048 |
| 2 | HID (USB) | VID=0x0425 PID=0x0301 |
| 3 | CDC (USB-serial) | VID=0x04D8 PID=0x000A |

### Arquivo de configuração: `devices.xml`

Localizado em `C:\Users\...\Desktop\Supervise\devices.xml` (ou no PATH do registro `HKLM\Software\Ragtech\SUPERVISE\PATH`).

Este arquivo XML define, para cada família/modelo de UPS:
- **Endereços** de registradores (`<alias name="X" addr="0086"/>`)
- **Fórmulas** de conversão (`vInput = V_VINPUT` ; `vOutput = V_VOUTPUT * 0.6090`)
- **Expressões** para flags (`opBattery = F_OPBATTERY`)
- **Ações** de comando (`shutdown`, `wakeUp`, etc.)
- **Capabilities** do modelo (quais variáveis existem)

---

## 2. Opção A — API HTTP do Serviço (mais simples)

Se o serviço `supsvc.exe` já está rodando na máquina, ele expõe um servidor HTTP embutido (Mongoose) com API JSON. **Não é preciso acessar a serial diretamente.**

### 2.1 Localizar a porta

A porta HTTP é configurável e fica armazenada no registro:

```
HKEY_LOCAL_MACHINE\Software\Ragtech
```

Procure a chave de configuração. Portas comuns: **8080**, **80**, **3000**.

Ou verifique com:

```powershell
# Listar portas TCP abertas pelo serviço
Get-NetTCPConnection -State Listen | Where-Object { (Get-Process -Id $_.OwningProcess).Name -like "*svc*" }
```

### 2.2 Endpoints da API

Baseado na engenharia reversa, o serviço responde com JSON contendo os seguintes campos:

#### Dados do UPS

```
GET http://localhost:<porta>/api/devices
GET http://localhost:<porta>/api/devices/<id>/vars
GET http://localhost:<porta>/api/devices/<id>/status
```

#### Exemplo de resposta JSON (vars)

```json
{
  "vInput":     127.3,
  "vOutput":    115.0,
  "iOutput":    1.5,
  "pOutput":    35.0,
  "fOutput":    60.0,
  "vBattery":   27.2,
  "cBattery":   85.0,
  "temperature": 26.5,
  "icBattery":  2,
  "lineSense":  1,
  "shutdownTimer": 0,
  "wakeupTimer": 0,
  "nominalVInput":  127.0,
  "nominalVOutput": 115.0,
  "nominalPOutput": 600.0,
  "nominalFOutput":  60.0,
  "nominalVBattery": 24.0
}
```

#### Exemplo de resposta JSON (status)

```json
{
  "connected":   true,
  "inShutdown":  false,
  "opStartup":   false,
  "opBattery":   false,
  "opStandBy":   false,
  "opCheckup":   false,
  "opWarning":   false,
  "noVInput":    false,
  "loVInput":    false,
  "hiVInput":    false,
  "loFInput":    false
}
```

### 2.3 Exemplo Python — API HTTP

```python
import requests

BASE_URL = "http://localhost:8080"  # ajuste a porta

def get_ups_vars(device_id=0):
    r = requests.get(f"{BASE_URL}/api/devices/{device_id}/vars", timeout=5)
    r.raise_for_status()
    return r.json()

def get_ups_status(device_id=0):
    r = requests.get(f"{BASE_URL}/api/devices/{device_id}/status", timeout=5)
    r.raise_for_status()
    return r.json()

if __name__ == "__main__":
    vars_  = get_ups_vars()
    status = get_ups_status()

    print(f"Tensão entrada : {vars_['vInput']} V")
    print(f"Tensão saída   : {vars_['vOutput']} V")
    print(f"Carga bateria  : {vars_['cBattery']} %")
    print(f"Tensão bateria : {vars_['vBattery']} V")
    print(f"Potência saída : {vars_['pOutput']} %")
    print(f"Frequência     : {vars_['fOutput']} Hz")
    print(f"No-break ativo : {status['opBattery']}")
    print(f"Sem rede       : {status['noVInput']}")
```

---

## 3. Opção B — Protocolo Serial/HID Proprietário Ragtech (sem o serviço)

> **⚠️ IMPORTANTE:** O protocolo Ragtech é **proprietário baseado em registradores**, **NÃO é o protocolo Megatec/Q1**. A hipótese inicial foi corrigida após análise completa do `device.dll.c` e do arquivo `devices.xml`.

> **ATENÇÃO:** Pare o serviço `supsvc.exe` antes de acessar a porta serial diretamente — dois processos não podem abrir a mesma porta COM simultaneamente.
>
> ```powershell
> Stop-Service -Name "supsvc" -Force
> ```

### 3.1 Configuração da porta serial

Parâmetros definidos no `devices.xml` (seção `<ports>`):

| Parâmetro | Opção 1 | Opção 2 | Opção 3 |
|---|---|---|---|
| Baud rate | **2400** bps | **2560** bps | **2048** bps |
| Data bits | 8 | 8 | 8 |
| Stop bits | 1 | 1 | 1 |
| Parity | None (0) | None (0) | None (0) |
| Timeout | 100 ms | 100 ms | 120 ms |

> O software testa cada configuração serial por detecção automática.

### 3.2 Protocolo de Registradores (Ragtech Proprietário)

O protocolo usa **leitura/escrita de registradores por endereço** (byte único por registrador). O `device.dll` usa `WriteFile`/`ReadFile` com I/O assíncrono (`FILE_FLAG_OVERLAPPED`).

O formato exato do pacote serial não foi determinado pela análise estática do decompilado (o código está em vtable), mas o mecanismo é:
- **Leitura em bloco** (`<range start="addr" size="n"/>`) — lê N registradores contíguos
- **Escrita por endereço** — ações escrevem registradores específicos

### 3.3 Mapa de registradores por família (do `devices.xml`)

#### Família 1 — SENIUM (1000–1600 VA)

Ranges lidos: `0x0080–0x0097` + `0x00CB`

| Endereço | Alias | Fórmula | Campo final |
|---|---|---|---|
| `0x0085` | `V_MODEL` | `V_MODEL` | `model` |
| `0x0086` | `V_VINPUT` | `V_VINPUT` | `vInput` (V) |
| `0x0087` | `V_VOUTPUT` | `× 0.6000` | `vOutput` (V) |
| `0x0088` | `V_IOUTPUT` | `× 0.0510` | `iOutput` (A) |
| `0x0089` | `V_POUTPUT` | `V_POUTPUT` | `pOutput` (%) |
| `0x008A` | `V_FOUTPUT` | `× 0.2500` | `fOutput` (Hz) |
| `0x008B` | `V_VBATTERY` | `× 0.1250` | `vBattery` (V) |
| `0x0092` | `V_CBATTERY` | `V_CBATTERY` | `cBattery` (%) |
| `0x0093` | `V_VERSION` | `× 0.1000` | `version` |
| `0x0097` | `V_TEMPER` | `V_TEMPER` | `temperature` (°C) |
| `0x008D` | flags de falha | bits 0–7 | `failOvertemp` etc. |
| `0x008E` | flags de entrada | bits 0–7 | `loFInput`, `loVInput` etc. |
| `0x008F` | flags operacionais | bits 3–7 | `opBattery`, `opStandBy` etc. |
| `0x0091` | flags bateria/timer | bits 0,1,4 | `maxBattery`, `shutdownTimerActive` |
| `0x0095` | flags remotos | bits 3,5 | `remoteControlActive`, `vinSel220` |
| `0x00CB` | `V_SHUTDOWNTIMER` | `V_SHUTDOWNTIMER` | `shutdownTimer` |

#### Família 3 — MICRON II / IF HOME

Ranges lidos: `0x0000–0x0018`

| Endereço | Alias | Fórmula | Campo final |
|---|---|---|---|
| `0x0000` | `V_MODEL` | `V_MODEL` | `model` |
| `0x0001` | `V_VERSION` | `× 0.1000` | `version` |
| `0x0011` | `V_VINPUT` | fórmula complexa com calibração | `vInput` (V) |
| `0x0012` | `V_VOUTPUT` | fórmula com tap e calibração | `vOutput` (V) |
| `0x0014` | `V_VBATTERY` | `/ V_VBATCALIB × (Vnom×13.8/12)` | `vBattery` (V) |
| `0x0016` | `V_FOUTPUT` | `(V_FOUTCALIB - V_FOUTPUT) / 720 + 1) × 60` | `fOutput` (Hz) |
| `0x0015` | `V_SHUTDOWNTIMER` | condic. | `shutdownTimer` |
| `0x000B` | flags bateria/timer | bits 0,4,7 | `syncInput`, `shutdownTimerActive`, `maxBattery` |
| `0x000C` | `F_NOVINPUT` | bit 0 | `noVInput` |
| `0x000D` | flags de falha | bits 1–7 | `failEndBattery` etc. |
| `0x000E` | flags de entrada | bits 1–7 | `loFInput`, `loVInput` etc. |
| `0x000F` | flags operacionais | bits 3–6 | `opBattery`, `opStandBy` etc. |

#### Família 4 e 5 — INFINIUM DIGITAL

Ranges lidos: `0x0080–0x0096` + `0x00C9`

| Endereço | Alias | Fórmula | Campo final |
|---|---|---|---|
| `0x0085` | `V_MODEL` | `V_MODEL` | `model` |
| `0x0086` | `V_VINPUT` | `V_VINPUT` | `vInput` (V) |
| `0x0087` | `V_VOUTPUT` | `× 0.6090` | `vOutput` (V) |
| `0x0088` | `V_IOUTPUT` | `× 0.0625` | `iOutput` (A) |
| `0x0089` | `V_POUTPUT` | `V_POUTPUT` | `pOutput` (%) |
| `0x008A` | `V_FOUTPUT` | `× 0.2500` | `fOutput` (Hz) |
| `0x008B` | `V_VBATTERY` | `× 0.1250` | `vBattery` (V) |
| `0x0090` | `V_TEMPER` | `V_TEMPER` | `temperature` (°C) |
| `0x0092` | `V_CBATTERY` | `V_CBATTERY` | `cBattery` (%) |
| `0x0093` | `V_VERSION` | `× 0.1000` | `version` |
| `0x008D` | flags de falha | bits 0–7 | `failOvertemp` etc. |
| `0x008E` | flags de entrada | bits 0–7 | `loFInput`, `noVInput` etc. |
| `0x008F` | flags operacionais | bits 3–7 | `opBattery` etc. |
| `0x0091` | flags wakeup/timer | bits 0,1,4,5,7 | `nightOff`, `maxBattery`, `shutdownTimerActive` |
| `0x0095` | flags remotos | bits 3,5 | `remoteControlActive`, `vinSel220` |
| `0x00C9` | `V_SHUTDOWNTIMER` | condic. | `shutdownTimer` |

#### Família 0 — MICRON/INFINIUM antigo (sinais RS-232)

**Não usa registradores.** Monitora os sinais de controle da porta serial:

| Sinal | Alias | Flag mapeada |
|---|---|---|
| CTS | `F_CTS` | `remoteControlActive = !F_CTS` |
| DSR | `F_DSR` | `loBattery = !F_DSR` |
| CD | `F_CD` | `opBattery = !F_CD`, `noVInput = !F_CD` |
| DTR | `F_DTR` | ação de shutdown: `DTR=1; delay(16s); DTR=0` |

### 3.4 Flags de bit — mapa detalhado (families 1/2/4/5)

#### Byte `0x008D` — Falhas de hardware

| Bit | Alias | Flag final |
|---|---|---|
| 0 | `F_FOVERTEMP` | `failOvertemp` |
| 1 | `F_FINTERNAL` | `failInternal` |
| 2 | `F_FSHORTCIRCUIT` (fam 4/5) / `F_FENDBATTERY` (fam 1) | `failShortcircuit` / `failEndBattery` |
| 3 | `F_FOVERLOAD` | `failOverload` |
| 4 | `F_FENDBATTERY` (fam 4/5) / `F_FSHORTCIRCUIT` (fam 1) | `failEndBattery` / `failShortcircuit` |
| 5 | `F_FABNORMALVOUT` | `failAbnormalVOutput` |
| 6 | `F_FABNORMALVBAT` | `failAbnormalVBattery` |
| 7 | `F_FINVERTER` | `failInverter` |

#### Byte `0x008E` — Status da rede

| Bit | Alias | Flag final |
|---|---|---|
| 0 | `F_LOFINPUT` | `loFInput` |
| 1 | `F_HIFINPUT` (fam 1) / `F_FFAN` (fam 2/4/5) | `hiFInput` / `failFan` |
| 2 | `F_NOSYNCIN` | `noSyncInput` |
| 3 | `F_LOVINPUT` | `loVInput` |
| 4 | `F_HIVINPUT` | `hiVInput` |
| 5 | `F_NOVINPUT` | `noVInput` |
| 6 | `F_LOBATTERY` | `loBattery` |
| 7 | `F_NOISEIN` | `noiseInput` |

#### Byte `0x008F` — Status operacional

| Bit | Alias | Flag final |
|---|---|---|
| 3 | `F_OPBATTERY` | `opBattery` |
| 4 | `F_OPSTANDBY` | `opStandBy` |
| 5 | `F_OPWARNING` | `opWarning` |
| 6 | `F_OPSTARTUP` | `opStartup` |
| 7 | `F_OPCHECKUP` | `opCheckup` |

#### Byte `0x0091` — Timers/bateria (família 1)

| Bit | Alias | Flag final |
|---|---|---|
| 0 | `F_SYNCIN` | `syncInput` |
| 1 | `F_MAXBATTERY` | `maxBattery` |
| 4 | `F_SHUTDOWNTIMERON` | `shutdownTimerActive` |

#### Byte `0x0095` — Controle remoto / tensão (família 1)

| Bit | Alias | Flag final |
|---|---|---|
| 3 | `F_RCTRLON` | `remoteControlActive` |
| 5 | `F_VINSEL220` | nominalVInput = 220V se 1, 115V se 0 |

#### Família 6 — PRECISION

Ranges lidos: `0x0080–0x0095` + `0x00D0`

| Endereço | Alias | Fórmula | Campo final |
|---|---|---|---|
| `0x0083` | `V_VOUTPUT` | `× 0.6050` | `vOutput` (V) |
| `0x0085` | `V_MODEL` | direto | `model` |
| `0x0086` | `V_VINPUT` | condic. `× 1.148` (220V) ou `× 0.605` (115V) | `vInput` (V) |
| `0x0088` | `V_IINPUT` | — | raw |
| `0x0089` | `V_POUTPUT` | direto | `pOutput` (%) |
| `0x008A` | `V_FOUTPUT` | `× 0.2500` | `fOutput` (Hz) |
| `0x0090` | `V_TEMPER` | direto | `temperature` (°C) |
| `0x0093` | `V_VERSION` | `× 0.1000` | `version` |
| `0x00D0` | `V_SHUTDOWNTIMER` | condic. | `shutdownTimer` |
| `0x008D` | flags de falha | bits 1–7 | `failOvertemp`, `failLoVInput`, `failOverload`, etc. |
| `0x008E` | flags de rede | bits 0–7 | `loFInput`, `loVInput`, `hiVInput` etc. |
| `0x008F` | flags operacionais | bits 4,6 | `opStandBy`, `opStartup` |

#### Família 7 — SENIUM/ONEUP (com RGB LED)

Ranges lidos: `0x0080–0x0095` + `0x00C9` + `0x00B8` + `0x004B–0x004E`

Mesmo mapa de registradores das famílias 1/2/4/5 para as medições principais, mais:

| Endereço | Alias | Campo final |
|---|---|---|
| `0x004B` | `V_LEDRED` | `ledRed` (intensidade LED vermelho) |
| `0x004C` | `V_LEDGREEN` | `ledGreen` |
| `0x004D` | `V_LEDBLUE` | `ledBlue` |
| `0x004E` | `V_RANDOM` | `random` |
| `0x0084` bit 0 | `F_WAKEUPON` | wake-up timer ativo |
| `0x0084` bit 4 | `F_NIGHTOFFON` | night-off ativo |
| `0x0084` bit 6 | `F_SUPERVON` | supervisão ativa |

Ação extra: `changeColor` → escreve `V_LEDRED`, `V_LEDGREEN`, `V_LEDBLUE`, `V_RANDOM`.

### 3.5 Instalação das dependências Python

```bash
pip install pyserial requests
```

---

## 4. Diagnóstico — Identificar a porta COM

```powershell
# Listar portas seriais disponíveis no Windows
Get-WmiObject Win32_SerialPort | Select-Object Name, DeviceID, Description
```

Ou no Python:

```python
import serial.tools.list_ports

for porta in serial.tools.list_ports.comports():
    print(f"{porta.device} — {porta.description}")
```

Para identificar o UPS USB por VID/PID:

```python
import serial.tools.list_ports

VID_HID = 0x0425  # HID
PID_HID = 0x0301
VID_CDC = 0x04D8  # CDC/USB-serial
PID_CDC = 0x000A

for p in serial.tools.list_ports.comports():
    if p.vid in (VID_HID, VID_CDC):
        print(f"UPS Ragtech encontrado: {p.device} — {p.description} "
              f"(VID={p.vid:04X} PID={p.pid:04X})")
```

---

## 5. Captura/Sniffer do protocolo serial

Para descobrir o formato exato dos pacotes enviados/recebidos, recomenda-se usar um sniffer de porta serial:

- **com0com** + **HHD Free Serial Monitor** (Windows) — intercepta tráfego entre o serviço e o UPS
- **Wireshark USBPcap** — captura tráfego HID/CDC via USB
- **API Monitor** — intercepta chamadas Win32 `WriteFile`/`ReadFile` do `supsvc.exe`

### Exemplo com API Monitor

1. Inicie o API Monitor
2. Monitore `supsvc.exe`
3. Filtre por `WriteFile` e `ReadFile`
4. Cada chamada `WriteFile` = um pacote de request
5. A `ReadFile` seguinte = a resposta do UPS

O payload dos pacotes revelará o formato do protocolo de registradores.

---

## 6. Mapa completo de status flags (confirmado — monit.dll)

A análise de `monit.dll.c` revelou o mapa **completo** dos status flags — 4 bytes bitfield no struct interno:

### Byte +0x04 — Status operacional

| Bit | Máscara | Campo | Significado |
|---|---|---|---|
| 0 | `& 0x01` | `connected` | UPS conectado |
| 1 | `& 0x02` | `inShutdown` | Em processo de desligamento |
| 2 | `& 0x04` | `opStartup` | Inicializando |
| 3 | `& 0x08` | `opBattery` | **Operando na bateria** |
| 4 | `& 0x10` | `opStandBy` | Em modo stand-by |
| 5 | `& 0x20` | `opCheckup` | Em diagnóstico/checkup |
| 6 | `& 0x40` | `opWarning` | Aviso geral |
| 7 | `& 0x80` | `noVInput` | **Sem tensão de entrada** |

### Byte +0x05 — Status de entrada/saída

| Bit | Máscara | Campo | Significado |
|---|---|---|---|
| 0 | `& 0x01` | `loVInput` | Tensão de entrada baixa |
| 1 | `& 0x02` | `hiVInput` | Tensão de entrada alta |
| 2 | `& 0x04` | `loFInput` | Frequência de entrada baixa |
| 3 | `& 0x08` | `hiFInput` | Frequência de entrada alta |
| 4 | `& 0x10` | `noSyncInput` | Sem sincronismo na entrada |
| 5 | `& 0x20` | `syncInput` | Sincronismo na entrada |
| 6 | `& 0x40` | `noiseInput` | Ruído na entrada |
| 7 | `& 0x80` | `hiPOutput` | Potência de saída alta |

### Byte +0x06 — Status de bateria

| Bit | Máscara | Campo | Significado |
|---|---|---|---|
| 0 | `& 0x01` | `loBattery` | **Bateria baixa** |
| 1 | `& 0x02` | `maxBattery` | Bateria no máximo |
| 2 | `& 0x04` | `noBattery` | Sem bateria |
| 3 | `& 0x08` | `oldBattery` | Bateria velha/desgastada |
| 4 | `& 0x10` | `moreBattery` | Bateria acima do nível esperado |
| 5 | `& 0x20` | `lessBattery` | Bateria abaixo do nível esperado |
| 6 | `& 0x40` | `shutdownTimerActive` | Timer de desligamento ativo |
| 7 | `& 0x80` | `remoteControlActive` | Controle remoto ativo |

### Byte +0x07 — Flags adicionais

| Bit | Máscara | Campo | Significado |
|---|---|---|---|
| 0 | `& 0x01` | `noCalibrated` | UPS não calibrado |

### Struct de falhas (byte separado)

| Bit | Máscara | Campo | Significado |
|---|---|---|---|
| 0 | `& 0x01` | `overtemp` | Sobretemperatura |
| 1 | `& 0x02` | `internal` | Falha interna |
| 2 | `& 0x04` | `overload` | Sobrecarga |
| 3 | `& 0x08` | `shortcircuit` | Curto-circuito |
| 4 | `& 0x10` | `inverter` | Falha no inversor |
| 5 | `& 0x20` | `fan` | Falha no ventilador |
| 6 | `& 0x40` | `tiristor` | Falha no tiristor |
| 7 | `& 0x80` | `endBattery` | Bateria esgotada |

---

## 7. Banco de dados SQLite interno (monit.dll)

O `monit.dll` mantém um banco SQLite local com histórico de eventos. A tabela principal de configuração de monitoramento contém:

```sql
CREATE TABLE <device_id> (
    id              TEXT PRIMARY KEY,
    enabled         INTEGER,
    counterManual   INTEGER,   -- contador desligamentos manuais
    counterOpBattery INTEGER,  -- contador operações na bateria
    counterLoBattery INTEGER,  -- contador eventos bateria baixa
    counterFailure  INTEGER,   -- contador falhas
    counterTimer    INTEGER,   -- contador por timer
    counterBatLevel INTEGER,   -- contador por nível de bateria
    remCtrlDelay    INTEGER,
    devShutEnabled  INTEGER,   -- desligamento do UPS habilitado
    devStandByKeep  INTEGER,
    devShutTimer    INTEGER,   -- timer de desligamento (segundos)
    cliShutEnabled  INTEGER,   -- desligamento do cliente habilitado
    cliHibernate    INTEGER,   -- hibernar ao desligar
    cliShutTimer    INTEGER,
    cliRun          TEXT       -- comando a executar antes de desligar
)
```

Também existe tabela de dispositivos com campos: `id, family, model, userName, userProd, version, idPc, last`.

---

## 8. Resumo dos campos mapeados

| Campo JSON | Descrição | Unidade |
|---|---|---|
| `vInput` | Tensão de entrada | V |
| `vOutput` | Tensão de saída | V |
| `iOutput` | Corrente de saída | A |
| `pOutput` | Carga de saída | % |
| `fOutput` | Frequência de saída | Hz |
| `vBattery` | Tensão da bateria | V |
| `cBattery` | Carga da bateria | % |
| `temperature` | Temperatura interna | °C |
| `icBattery` | Corrente da bateria | A |
| `nominalVInput` | Tensão nominal entrada | V |
| `nominalVOutput` | Tensão nominal saída | V |
| `nominalPOutput` | Potência nominal | VA/W |
| `nominalFOutput` | Frequência nominal | Hz |
| `nominalVBattery` | Tensão nominal bateria | V |

---

## 9. Ações de comando (escrita no UPS)

As ações abaixo são definidas no `devices.xml` por família. Elas escrevem registradores específicos:

| Ação | Descrição | Registradores afetados |
|---|---|---|
| `shutdown` | Desliga o UPS após timer | `V_SHUTDOWNTIMER=$n`, `F_SHUTDOWNTIMERON=1`, `F_SUPERVON=0` |
| `superviseOn` | Ativa supervisão | `F_SUPERVON=1` |
| `wakeUp` | Programa wake-up timer | `V_WAKEUPLOW/MID/HI`, `F_WAKEUPON=1` |
| `nightOff` | Programa desligamento noturno | similar ao wakeUp |
| `setLineSens` | Configura sensibilidade de linha | `V_LINESENS=$val` |
| `setICBattery` | Configura corrente de carga da bateria | `V_ICBATTERY=$val` |

### Fórmula do timer de wake-up

```
timer_raw = 0x1000000 - timer_seconds
V_WAKEUPLOW = timer_raw & 0xFF
V_WAKEUPMID = (timer_raw >> 8) & 0xFF
V_WAKEUPHI  = (timer_raw >> 16) & 0xFF
```

---

## 10. Notas importantes

- **Pare o serviço** antes de acessar a porta serial diretamente. Dois processos não abrem a mesma porta COM.
- Se usar a **API HTTP**, o serviço deve estar rodando — não precisa parar nada.
- O protocolo serial Ragtech é **proprietário** — **não é Megatec Q1** nem Modbus padrão.
- Baud rates confirmadas: **2400, 2560, 2048** bps (8N1). Não usar 9600.
- O software testa as baud rates por auto-detecção.
- O `devices.xml` é a fonte de verdade: está em `HKLM\Software\Ragtech\SUPERVISE\PATH` + `\devices.xml`.
- Para UPS com USB: HID (VID=0425 PID=0301) ou CDC/serial (VID=04D8 PID=000A).
- Para capturar o protocolo exato, use sniffer serial ou interceptação de `WriteFile`/`ReadFile` no `supsvc.exe`.
