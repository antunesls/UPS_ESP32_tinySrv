# Configuração do Projeto

Este projeto usa o sistema Kconfig do ESP-IDF para gerenciar as configurações.

## Configuração Rápida

### Método 1: Usando o menuconfig (Recomendado)

```bash
idf.py menuconfig
```

Navegue até: `UPS ESP32 TinySrv Configuration` e configure:

- **WiFi Configuration**
  - WiFi SSID: Nome da sua rede WiFi
  - WiFi Password: Senha da sua rede WiFi
  - Maximum retry: Número máximo de tentativas de conexão

- **MQTT Configuration**
  - Broker URL: URL do seu broker MQTT (ex: mqtt://192.168.1.100:1883)
  - Broker Port: Porta do broker (padrão: 1883)
  - Broker Username: Usuário (deixe vazio se não usar autenticação)
  - Broker Password: Senha (deixe vazio se não usar autenticação)

- **UPS Configuration**
  - UPS Polling Interval: Intervalo de leitura do UPS em ms (padrão: 5000)

### Método 2: Usando arquivo de configuração

1. Copie o arquivo de exemplo:
```bash
cp sdkconfig.example sdkconfig.defaults
```

2. Edite o arquivo `sdkconfig.defaults` com suas configurações:
```bash
nano sdkconfig.defaults
```

3. Reconfigure o projeto:
```bash
idf.py reconfigure
```

## Configurações Disponíveis

### WiFi
- `CONFIG_ESP_WIFI_SSID`: Nome da rede WiFi
- `CONFIG_ESP_WIFI_PASSWORD`: Senha da rede WiFi
- `CONFIG_ESP_MAXIMUM_RETRY`: Número máximo de tentativas de reconexão (padrão: 5)

### MQTT
- `CONFIG_BROKER_URL`: URL completa do broker MQTT
- `CONFIG_BROKER_PORT`: Porta do broker (padrão: 1883)
- `CONFIG_BROKER_USERNAME`: Nome de usuário (opcional)
- `CONFIG_BROKER_PASSWORD`: Senha (opcional)

### UPS
- `CONFIG_UPS_POLL_INTERVAL_MS`: Intervalo de polling em milissegundos (1000-60000, padrão: 5000)
- `CONFIG_UPS_VENDOR_ID`: Vendor ID USB do UPS
- `CONFIG_UPS_PRODUCT_ID`: Product ID USB do UPS

## Exemplo de Configuração

```ini
# WiFi
CONFIG_ESP_WIFI_SSID="MinhaRedeWiFi"
CONFIG_ESP_WIFI_PASSWORD="minha_senha_secreta"

# MQTT
CONFIG_BROKER_URL="mqtt://192.168.1.100:1883"

# UPS
CONFIG_UPS_POLL_INTERVAL_MS=5000
```

## Build e Flash

Após configurar, compile e grave o firmware:

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Notas de Segurança

⚠️ **IMPORTANTE**: Não commite o arquivo `sdkconfig` ou `sdkconfig.defaults` com suas credenciais reais!

Adicione ao `.gitignore`:
```
sdkconfig.defaults
```
