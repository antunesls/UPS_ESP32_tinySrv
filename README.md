<p align="center">
  <a href="" rel="noopener">
 <img width=200px height=200px src="images\logo_project.webp" alt="Project logo"></a>
</p>

<h3 align="center">UPS ESP32-S3 Server MQTT/WIFI (Ragtech)</h3>

<div align="center">

[![Status](https://img.shields.io/badge/status-active-success.svg)]()
[![GitHub Issues](https://img.shields.io/github/issues/kylelobo/The-Documentation-Compendium.svg)](https://github.com/antunesls/UPS_ESP32_tinySrv/issues)
[![GitHub Pull Requests](https://img.shields.io/github/issues-pr/kylelobo/The-Documentation-Compendium.svg)](https://github.com/antunesls/UPS_ESP32_tinySrv/pulls)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](/LICENSE)

</div>

---
<p align="center"> 
An ESP32-S3 reads UPS data from a Ragtech Easy Pro 1200VA via USB and sends it to an MQTT broker for integration with Home Assistant.
    <br> 
</p>


## 🧐 About <a name = "about"></a>

This project aims to develop a system using an ESP32-S3 microcontroller to monitor a Ragtech Easy Pro 1200VA UPS via USB. The ESP32-S3 will collect UPS data (such as input voltage, output voltage, battery level, load percentage, and operational status) and transmit it to an MQTT broker for integration with a Home Assistant server, enabling real-time monitoring and automation.

## 🏁 Getting Started <a name = "getting_started"></a>

Edit the file Kconfig.projbuild.example with your wifi parameters and the mqtt address.

Remove the file the extension .example before build the project in your ESP32-S3 device.

## ⛏️ Built Using <a name = "built_using"></a>

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/) - ESP32-S3 Framework
- [MQTT](https://mqtt.org/) - MQTT Broker
- [Home Assistant](https://www.home-assistant.io/) - Home Assistant


## ✍️ Authors <a name = "authors"></a>

- [@antunesls](https://github.com/antunesls) - Idea & Initial work

## 🎉 Acknowledgements <a name = "acknowledgement"></a>

A special thanks to the projects and resources that helped me accomplish this work.

- https://www.com-port-monitoring.com/pt/
- https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
- https://github.com/ludoux/esp32-nut-server-usbhid
- https://github.com/tanakamasayuki/EspUsbHost
- https://github.com/RafaelEstevamReis/HA_Ragtech_UPS
