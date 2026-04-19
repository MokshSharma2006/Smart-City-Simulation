# 🏙️ Decentralized Smart City IoT Infrastructure

<div align="center">

![Status](https://img.shields.io/badge/Status-Active-brightgreen?style=for-the-badge)
![License](https://img.shields.io/badge/License-MIT-blue?style=for-the-badge)
![Version](https://img.shields.io/badge/Version-1.0.0-orange?style=for-the-badge)
![Platform](https://img.shields.io/badge/Platform-RPi%20%7C%20ESP32%20%7C%20Arduino-lightgrey?style=for-the-badge)

**A military-grade encrypted, decentralized IoT network for smart city infrastructure — built on edge-computing microcontrollers with a real-time WebSocket command center.**

[Features](#-core-features) · [Architecture](#-system-architecture) · [Hardware](#-hardware--components) · [Security](#-cryptographic-protocol) · [Setup](#-installation--setup) · [Troubleshooting](#-troubleshooting)

</div>

---

## Overview

This project is a comprehensive, end-to-end demonstration of a **Decentralized Physical Infrastructure Network (DePIN)** — a distributed IoT system where four autonomous edge nodes manage real city functions: gate access, environmental monitoring, traffic control, and smart parking.

All telemetry is secured with **AES-128-CBC encryption**, **HMAC-SHA256 authentication**, and replay-attack prevention via incrementing nonces — before being streamed live to a sci-fi HUD dashboard running on a central Raspberry Pi 5 command center.

> Built by **Moksh Sharma**

---

## ✨ Core Features

| Feature | Description |
|---|---|
| 🔗 **Decentralized Edge Nodes** | 4 autonomous microcontrollers handling dedicated city tasks independently |
| 🔐 **Military-Grade Security** | AES-128-CBC encryption + HMAC-SHA256 signing + nonce-based replay prevention |
| ⚡ **Real-Time Dashboard** | Flask + WebSocket HUD with zero page refreshes, pushing live state in milliseconds |
| 🛡️ **Hardware-Level Resilience** | Custom analog/digital bypasses preventing boot-loops and Wi-Fi transmission drops |
| 🗺️ **Dynamic Network Topology** | Live visual mapping of node health and intrusion alerts on the web interface |

---

## 🏗️ System Architecture

```
┌─────────────────────────────────────────────────────────┐
│               Raspberry Pi 5 — Command Center           │
│         Mosquitto MQTT Broker · Flask + SocketIO        │
│              AES Decryption · Live HUD Dashboard        │
└────────────┬────────────┬────────────┬──────────────────┘
             │  MQTT/Wi-Fi│            │
    ┌─────────┴──┐  ┌─────┴──────┐  ┌─┴──────────┐  ┌──────────────┐
    │  Node 01   │  │  Node 02   │  │  Node 03   │  │   Node 04    │
    │ Access Gate│  │ Perimeter  │  │  Traffic   │  │Smart Parking │
    │   ESP32    │  │  ESP32-C3  │  │ Uno R4 WiFi│  │  ESP8266     │
    └────────────┘  └────────────┘  └────────────┘  └──────────────┘
```

### 🧠 Command Center — Raspberry Pi 5 (8GB)

The central hub runs a **Mosquitto MQTT broker** to ingest encrypted telemetry from all edge nodes. A Python backend decrypts payloads using `pycryptodome` and pushes live state to the web dashboard via `flask-socketio`.

- **Backend:** Python · `paho-mqtt` · `pycryptodome` · `flask-socketio`
- **Frontend:** Fully responsive dark-mode CSS Grid HUD (HTML/JS)

---

### 🔌 Edge Nodes

#### Node 01 — Access Gate `ESP32`

Manages physical security at the city entry checkpoint.

- RFID RC522 scanner over dedicated **VSPI hardware SPI** for zero-latency card reads
- PWM Servo motor for physical gate actuation upon cryptographic token validation

#### Node 02 — Perimeter Zone Super-Node `ESP32-C3`

A RISC-V powered combined environmental and intrusion detection node.

- **Environmental:** DHT11 (Temperature/Humidity), LDR (Light Level)
- **Security:** PIR Motion Sensors, IR Tripwires

#### Node 03 — Automated Traffic Junction `Arduino Uno R4 WiFi`

Smart intersection management with emergency vehicle prioritization.

- Dual-lane IR proximity sensing with dynamic traffic light phase control
- Analog Sound Sensor detects emergency sirens → triggers intersection override
- Local status displayed on TM1637 4-Digit Display

#### Node 04 — Smart Parking Deck `NodeMCU ESP8266`

Autonomous vehicle storage management for a 2-spot parking deck.

- Calibrated IR sensors for per-spot occupancy detection
- Local Red/Green indicator LEDs + ambient LDR for safety flood lighting
- Driver-facing status output via I2C OLED display

---

## ⚙️ Hardware & Components

### Microcontrollers

| Node | Board | Primary Role |
|---|---|---|
| Node 01 | ESP32 | Access Gate & RFID Security |
| Node 02 | ESP32-C3 | Environmental & Perimeter Monitoring |
| Node 03 | Arduino Uno R4 WiFi | Traffic Junction Management |
| Node 04 | NodeMCU ESP8266 | Smart Parking Management |
| Core | Raspberry Pi 5 (8GB) | MQTT Broker & Command Center |

### Passive Components

**10µF 63V Electrolytic Capacitors — Power Decoupling**

Placed in parallel across the `5V`/`3V3` and `GND` rails on every edge node. When ESP chips fire their Wi-Fi radios to transmit encrypted payloads, they draw large instantaneous current spikes. These capacitors act as local charge reservoirs, preventing voltage dips that cause sensor freezing, brownout resets, or continuous boot-loops.

**220Ω – 330Ω ¼W Carbon Film Resistors — Current Limiting**

Wired in series with the cathode leg of all Traffic Light and Parking Indicator LEDs. They drop the forward voltage to protect both the LEDs from thermal runaway and the microcontroller GPIO pins from exceeding their maximum current sink ratings.

---

## 📍 Pin Mapping Reference

### Node 01 — Access Gate (ESP32)

| Component | Module Pin | ESP32 Pin | Notes |
|:---|:---|:---|:---|
| RFID RC522 | SDA / SCK / MOSI / MISO | D5 / D18 / D23 / D19 | Strict VSPI hardware pins |
| RFID RC522 | RST / VCC | D22 / 3V3 | **3.3V only — 5V will damage the module** |
| Servo Motor | Signal / VCC | D13 / VIN (5V) | Requires 5V for actuation |

### Node 02 — Perimeter Zone (ESP32-C3)

| Component | Module Pin | ESP32-C3 Pin | Notes |
|:---|:---|:---|:---|
| DHT11 | Data (OUT) | GPIO 1 | Digital input |
| LDR | AO (Analog Out) | GPIO 0 (A0) | Safe ADC1 channel |
| IR Tripwire | OUT / DO | GPIO 3 | Safe digital input |
| PIR Motion | OUT / Data | GPIO 4 | Safe digital input |

### Node 03 — Traffic Junction (Uno R4 WiFi)

| Component | Module Pin | Uno R4 Pin | Notes |
|:---|:---|:---|:---|
| Traffic LEDs | Positive | D2 (Red), D3 (Yellow), D4 (Green) | Requires 220Ω resistors |
| Lane IR Sensors | OUT | D6 (Main), D9 (Cross) | Digital input |
| TM1637 Display | CLK / DIO | D7 / D8 | I2C-like communication |
| LDR / Sound Sensor | AO | A0 / A1 | Analog to prevent false triggers |

### Node 04 — Smart Parking (ESP8266)

| Component | Module Pin | ESP8266 Pin | Notes |
|:---|:---|:---|:---|
| OLED Display | SDA / SCL | D2 / D1 | I2C bus |
| Spot IR Sensors | OUT | D5 (Spot 1), D6 (Spot 2) | Digital input |
| Spot LEDs | Positive | D7/D8 (Sp1), D3/D4 (Sp2) | Requires 220Ω resistors |
| LDR | D0 (Digital Out) | GPIO16 | Hardware threshold bypass |

---

## 🔐 Cryptographic Protocol

Data is **never transmitted in plaintext**. Every payload passes through a 6-stage cryptographic pipeline implemented in C++ on each edge node before transmission:

```
 Raw Telemetry String
        │
        ▼
 1. Payload Generation    →  Structured string: ENV:T:25:H:60:L:1024:IR:1:PIR:0
        │
        ▼
 2. PKCS#7 Padding        →  Padded to strict 16-byte block alignment
        │
        ▼
 3. AES-128-CBC Encrypt   →  Encrypted with a localized Initialization Vector (IV)
        │
        ▼
 4. Base64 Encoding       →  Binary ciphertext encoded for MQTT transport stability
        │
        ▼
 5. HMAC-SHA256 Signing   →  Incrementing Nonce prepended; entire block signed with secret key
        │
        ▼
 6. Transmission          →  [Nonce]:[Encrypted Base64]:[HMAC Signature]
```

The incrementing **Nonce** definitively blocks replay attacks — any replayed packet is rejected as its nonce value will already have been seen and logged by the broker.

---

## 🚀 Installation & Setup

### Prerequisites

- Python 3.9+
- Arduino IDE 2.0+ with board manager cores: `esp32`, `esp8266`, `Arduino UNO R4 Boards`

### 1. Clone the Repository

```bash
git clone https://github.com/MokshSharma2006/SmartCityProject.git
cd SmartCityProject
```

### 2. Configure the Raspberry Pi MQTT Broker

Install and configure Mosquitto:

```bash
sudo apt update
sudo apt install mosquitto mosquitto-clients -y
```

Edit the Mosquitto configuration to accept inbound edge node telemetry:

```bash
sudo nano /etc/mosquitto/mosquitto.conf
```

Add the following lines:

```
listener 1883 0.0.0.0
allow_anonymous true
```

Restart the service to apply changes:

```bash
sudo systemctl restart mosquitto
```

### 3. Set Up the Python Environment

```bash
python3 -m venv env
source env/bin/activate
pip install paho-mqtt pycryptodome flask flask-socketio
```

### 4. Flash Edge Node Firmware

1. Open the respective `.ino` file for each node in the Arduino IDE.
2. Update the following global variables in each script:
   - `ssid` — your Wi-Fi network name
   - `password` — your Wi-Fi password
   - `mqtt_server` — the local IPv4 address of your Raspberry Pi
3. Select the correct board and port, then upload to each microcontroller.

### 5. Launch the Command Center

Start the Flask server on the Raspberry Pi:

```bash
python3 dashboard.py
```

Then open a browser on any device on the local network and navigate to:

```
http://<YOUR_PI_IP>:5000
```

---

## 🛠️ Troubleshooting

**ESP32-C3 freezes at `ESP-ROM:esp32c3-api1-20210207` on boot**

The ESP32-C3 manages its USB interface via software. In the Arduino IDE, go to **Tools → USB CDC On Boot** and set it to **Enabled** before compiling and uploading.

---

**PIR sensor is permanently stuck on `MOTION DETECTED`**

This is a hardware delay artifact. On the back of the HC-SR501 module, rotate the **left potentiometer** (Time Delay) fully counter-clockwise to reduce the signal hold time to its minimum 3-second threshold.

---

**RFID RC522 returns garbled or random characters**

The RC522 is extremely voltage-sensitive. Ensure it is powered **exclusively** from the `3V3` rail — 5V will permanently damage the logic gates. Additionally, keep all SPI wiring under 10cm to prevent signal degradation.

---

## 📄 License

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.

---

<div align="center">
Built with ❤️ by <strong>Moksh Sharma</strong>
</div>
