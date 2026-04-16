<p align="center">
  <a href="" rel="noopener">
 <img width=200px height=200px src="images/logo_project.webp" alt="Project logo"></a>
</p>

<h3 align="center">UPS ESP32-S3 Server — MQTT / WiFi (Ragtech)</h3>

<div align="center">

[![Status](https://img.shields.io/badge/status-active-success.svg)]()
[![GitHub Issues](https://img.shields.io/github/issues/antunesls/UPS_ESP32_tinySrv.svg)](https://github.com/antunesls/UPS_ESP32_tinySrv/issues)
[![GitHub Pull Requests](https://img.shields.io/github/issues-pr/antunesls/UPS_ESP32_tinySrv.svg)](https://github.com/antunesls/UPS_ESP32_tinySrv/pulls)
[![Release](https://img.shields.io/github/v/release/antunesls/UPS_ESP32_tinySrv)](https://github.com/antunesls/UPS_ESP32_tinySrv/releases)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](/LICENSE)

</div>

---

<p align="center">
Monitora nobreaks Ragtech via USB-CDC no ESP32-S3, publica métricas e alarmes em MQTT e oferece painel web com OTA embutido.
</p>

---

## Sobre o projeto

O **UPS ESP32-S3 Server** conecta um ESP32-S3 a um nobreak Ragtech via USB (CDC/ACM) e:

- Lê os registradores proprietários Ragtech (tensão de entrada/saída, corrente, carga, bateria, frequência, temperatura e 22 flags de status)
- Publica os dados num broker MQTT com auto-discovery para o Home Assistant
- Exibe um painel web com métricas em tempo real, card de alarmes e atualização OTA

### Hardware suportado

| Dispositivo | Interface | VID | PID |
|---|---|---|---|
| Nobreak Ragtech (Easy Pro, Infinium, Senium) | USB CDC | `0x04D8` | `0x000A` |
| Nobreak Ragtech (HID) | USB HID | `0x0425` | `0x0301` |

**Placa testada:** YD-ESP32-S3 (VCC-GND Studio) — LED WS2812 no GPIO 48.

---

## Funcionalidades

- **Métricas analógicas:** tensão de entrada/saída, corrente de saída, potência, energia acumulada (kWh), frequência, temperatura, tensão e carga da bateria
- **22 flags de status:** opBattery, noVInput, loBattery, failOverload, failInverter etc. (decodificados dos registradores 0x008D–0x0095)
- **MQTT com auto-discovery:** sensores e binary sensors prontos para aparecer automaticamente no Home Assistant
- **Painel web:** métricas, alarmes com badges coloridos, logs do sistema e upload OTA pelo browser
- **Portal WiFi:** hotspot de configuração na primeira inicialização (SSID `UPS-ESP32-XXXXXX`); botão BOOT por 3s reseta as credenciais
- **OTA dual-partição:** flash de 8 MB com ota_0 e ota_1 (3,75 MB cada)
- **LED WS2812:** indica o estado do sistema (booting, WiFi, MQTT, UPS, OTA, portal AP)

---

## Primeiros passos

### Pré-requisitos

- [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/latest/)
- ESP32-S3 com 8 MB de flash
- Nobreak Ragtech com interface USB CDC (VID `0x04D8` / PID `0x000A`)

### Instalação a partir do binário (recomendado)

1. Baixe o arquivo `UPS_ESP32_tinySrv.bin` da [última release](https://github.com/antunesls/UPS_ESP32_tinySrv/releases/latest)
2. Grave no ESP32-S3:

```bash
esptool.py --chip esp32s3 --port /dev/ttyUSB0 \
  --baud 460800 write_flash 0x0 UPS_ESP32_tinySrv.bin
```

> O binário de release já inclui bootloader, partition table e firmware numa imagem unificada (`--flash_mode dio --flash_size 8MB`).

### Compilar a partir do código-fonte

```bash
git clone https://github.com/antunesls/UPS_ESP32_tinySrv.git
cd UPS_ESP32_tinySrv
cp sdkconfig.example sdkconfig   # configure MQTT, WiFi e família do UPS
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

#### Configurações relevantes (`idf.py menuconfig`)

| Menu | Opção | Descrição |
|---|---|---|
| UPS Configuration | UPS hardware family | **INFINIUM** (Easy Pro 2200+) ou **SENIUM** (Easy Pro 1000–1600 VA) |
| MQTT Configuration | Broker URL / Port / User / Password | Endereço e credenciais do broker |
| LED Configuration | LED GPIO pin | GPIO do WS2812 (padrão: 48) |
| Web Server Configuration | HTTP server port | Porta do painel web (padrão: 80) |

---

## Configuração inicial (portal WiFi)

1. Na primeira inicialização o ESP32 cria o hotspot **`UPS-ESP32-XXXXXX`**
2. Conecte-se a ele e acesse `http://192.168.4.1`
3. Informe SSID e senha da sua rede Wi-Fi e salve
4. O dispositivo reinicia e conecta à rede configurada

Para redefinir as credenciais: segure o botão **BOOT** por 3 segundos.

---

## Tópicos MQTT

Todos os tópicos usam o MAC Address como identificador único:

```
UPS_ESP32_tinySrv/<MAC>/availability          → {"state":"online"}
UPS_ESP32_tinySrv/<MAC>/Sensor_<campo>        → {"value":<float>}
UPS_ESP32_tinySrv/<MAC>/status/<flag>         → ON / OFF
homeassistant/sensor/<MAC>/<campo>/config     → discovery payload
homeassistant/binary_sensor/<MAC>/<flag>/config → discovery payload
```

### Campos analógicos publicados

`power_out_percent` · `current_out` · `voltage_out` · `voltage_in` · `power_out` · `power_in` · `energy_out` · `energy_in` · `temperature` · `battery_state` · `battery_voltage` · `frequency`

### Flags de status publicados

`op_battery` · `no_v_input` · `lo_v_input` · `hi_v_input` · `lo_battery` · `max_battery` · `op_stand_by` · `op_warning` · `shutdown_timer_active` · `remote_control_active` · `fail_overtemp` · `fail_overload` · `fail_inverter` · `fail_end_battery` · `fail_shortcircuit`

---

## LED — estados do sistema

| Cor / padrão | Estado |
|---|---|
| Amarelo piscando rápido | Inicializando |
| Azul piscando rápido | Conectando ao WiFi |
| Azul pulsando lento | Conectando ao MQTT |
| Laranja piscando lento | WiFi/MQTT OK — aguardando UPS |
| Verde fixo | Tudo OK |
| Vermelho piscando rápido | Falha de WiFi |
| Ciano piscando rápido | Atualizando firmware (OTA) |
| Magenta pulsando lento | Portal de configuração WiFi ativo |

---

## Painel Web

Acesse `http://<IP do ESP32>` no browser:

- **Métricas:** tensões, correntes, potência, bateria, frequência e temperatura (atualização a cada 5s)
- **Alarmes e Status:** badges coloridos com os flags ativos (verde = ok, amarelo = aviso, vermelho = erro)
- **Atualizar Firmware:** upload de `.bin` com barra de progresso
- **Logs:** terminal com logs do sistema em tempo real (atualização a cada 3s)

---

## Estrutura do projeto

```
UPS_ESP32_tinySrv/
├── main/
│   ├── main.c           — inicialização e sequência de boot
│   ├── ups.c / ups.h    — leitura USB-CDC, decodificação de registradores
│   ├── ups_mqtt.c/.h    — publicação MQTT (métricas + binary sensors)
│   ├── wifi_manager.c/.h — portal AP, NVS, reconexão
│   ├── web_server.c/.h  — servidor HTTP (/, /api/metrics, /api/status, /api/info, /api/logs, /ota)
│   ├── web_ui.h         — página HTML embarcada
│   ├── log_buffer.c/.h  — buffer circular de logs
│   ├── led_status.c/.h  — controle do WS2812
│   └── Kconfig.projbuild
├── partitions.csv       — tabela de partições (8 MB, dual OTA)
├── sdkconfig.example    — template de configuração
└── CMakeLists.txt
```

---

## Construído com

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/) — framework ESP32
- [esp-mqtt](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mqtt.html) — cliente MQTT
- [esp_http_server](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html) — servidor HTTP
- [Home Assistant](https://www.home-assistant.io/) — plataforma de automação

---

## Autor

- [@antunesls](https://github.com/antunesls)

## Agradecimentos

- [@RafaelEstevamReis/HA_Ragtech_UPS](https://github.com/RafaelEstevamReis/HA_Ragtech_UPS)
- [@ludoux/esp32-nut-server-usbhid](https://github.com/ludoux/esp32-nut-server-usbhid)
