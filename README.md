# 🏙️ Decentralized Smart City IoT Infrastructure

![Status](https://img.shields.io/badge/Status-Active-brightgreen)
![License](https://img.shields.io/badge/License-MIT-blue)
![Version](https://img.shields.io/badge/Version-1.0.0-orange)
![Platform](https://img.shields.io/badge/Platform-Raspberry%20Pi%20%7C%20ESP32%20%7C%20Arduino-lightgrey)

A highly secure, distributed IoT Smart City network built with edge-computing microcontrollers and a central Raspberry Pi 5 core. This project features military-grade AES-128-CBC encryption, HMAC-SHA256 authentication, and a real-time, WebSocket-driven futuristic web dashboard. 

Developed as a comprehensive, end-to-end demonstration of decentralized physical infrastructure networks (DePIN), focusing on fault tolerance, real-time data streaming, and hardware-level cryptography.

Built by Moksh.

---

## 🌟 Core Features

* **Decentralized Edge Nodes:** 4 distinct microcontrollers autonomously handling dedicated city tasks (Gate Security, Environmental/Perimeter Monitoring, Traffic Junctions, and Smart Parking).
* **Military-Grade Security:** Every MQTT payload is encrypted using AES-128-CBC, signed with an HMAC-SHA256 hash, and utilizes an incrementing tracking Nonce to definitively block replay attacks.
* **Real-Time Command Center:** A Python Flask backend processes decrypted telemetry and pushes it to a live, sci-fi-themed HUD via WebSockets in milliseconds, requiring zero page refreshes.
* **Hardware-Level Bypasses:** Custom physical implementations for sensors (e.g., analog bypass for sound sensors, digital bypass for LDRs) to ensure uninterrupted Wi-Fi radio transmission and prevent microcontroller boot-loops.
* **Dynamic Network Topology:** Live visual mapping of node health and intrusion alerts directly on the web interface.

---

## 🏗️ System Architecture

### 🧠 The Core Command Center
* **Hardware:** Raspberry Pi 5 (8GB RAM)
* **Role:** Central MQTT Broker (Mosquitto) and Cryptographic Decryption Server.
* **Backend:** Python environment utilizing `paho-mqtt` for data ingestion and `pycryptodome` for high-speed AES decryption.
* **Frontend:** Flask Web Server + SocketIO pushing live state to a fully responsive, dark-mode CSS Grid HTML/JS interface.

### 🔌 The Edge Nodes

#### 1. Node 01: Access Gate (ESP32)
* **Function:** Physical security and authorization checkpoint.
* **Operation:** Handles security access using an RFID RC522 scanner over dedicated hardware VSPI for zero-latency card reads. Actuates a high-precision PWM Servo motor for physical entry upon successful cryptographic token validation.

#### 2. Node 02: Perimeter Zone Super-Node (ESP32-C3)
* **Function:** Combined environmental and intrusion detection.
* **Operation:** A RISC-V powered node tracking environmental atmospheric data (DHT11 Temperature/Humidity, LDR Light Level) and perimeter security breaches (PIR Motion Sensors, IR Tripwires).

#### 3. Node 03: Automated Traffic Junction (Arduino Uno R4 WiFi)
* **Function:** Smart intersection management and emergency vehicle prioritization.
* **Operation:** Manages a dual-lane intersection using IR proximity sensors. It dynamically adjusts traffic light phases and utilizes an analog Sound Sensor to detect emergency vehicle sirens, immediately triggering an intersection override. Local status is displayed on a TM1637 4-Digit Display.

#### 4. Node 04: Smart Parking Deck (NodeMCU ESP8266)
* **Function:** Autonomous vehicle storage management.
* **Operation:** Monitors a 2-spot parking deck using highly calibrated IR sensors. It directly controls local occupancy indicator LEDs (Red/Green), reads ambient deck lighting to trigger safety floods, and outputs local status to an I2C OLED display for drivers.

---

## ⚙️ Hardware & Components Specification

Beyond the microcontrollers and standard sensors, this network relies on specific passive components to ensure signal integrity and power stability across the IoT edge network:

### Power Stability & Filtering
* **10µF 63V Electrolytic Capacitors**
  * *Implementation:* Placed directly in parallel across the `5V`/`3V3` and `GND` power rails on every single edge node breadboard.
  * *Purpose:* When ESP chips enable their internal Wi-Fi radios to transmit encrypted payloads, they draw massive instantaneous current spikes. These capacitors act as localized battery buffers (decoupling capacitors) to prevent sudden voltage drops that cause sensor freezing, brownout resets, or continuous boot-loops.

### Current Limiting & Protection
* **220Ω - 330Ω 1/4 Watt Carbon Film Resistors**
  * *Implementation:* Wired in series with the Ground (Negative/Cathode) legs of all Traffic Light and Parking Indicator LEDs.
  * *Purpose:* Drops the forward voltage to strictly protect both the LEDs from thermal runaway (burning out) and the microcontroller GPIO pins from drawing excessive current that exceeds the board's maximum sink ratings.

---

## 📍 Master Pin Mapping

### Node 01: Access Gate (ESP32)
| Component | Module Pin | ESP32 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **RFID RC522** | SDA / SCK / MOSI / MISO | D5 / D18 / D23 / D19 | Strict VSPI Hardware Pins |
| **RFID RC522** | RST / VCC | D22 / **3V3** | *Strict 3.3V power only* |
| **Servo Motor** | Signal / VCC | D13 / **VIN (5V)** | *Requires 5V for actuation* |

### Node 02: Perimeter Zone (ESP32-C3)
| Component | Module Pin | ESP32-C3 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **DHT11** | Data (OUT) | GPIO 1 | Digital Input |
| **LDR (Light)** | AO (Analog Out) | GPIO 0 (A0) | Safe ADC1 channel |
| **IR Tripwire** | OUT / DO | GPIO 3 | Safe Digital Input |
| **PIR Motion** | OUT / Data | GPIO 4 | Safe Digital Input |

### Node 03: Traffic Junction (Uno R4 WiFi)
| Component | Module Pin | Uno R4 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **Traffic LEDs** | Positive | D2 (Red), D3 (Yel), D4 (Grn)| Requires 220Ω Resistors |
| **Lane IRs** | OUT | D6 (Main), D9 (Cross) | Digital Input |
| **4-Digit Disp** | CLK / DIO | D7 / D8 | TM1637 I2C-like comms |
| **Sensors** | AO (LDR) / AO (Sound) | A0 / A1 | Analog to prevent false triggers |

### Node 04: Smart Parking (ESP8266)
| Component | Module Pin | ESP8266 Pin | Notes |
| :--- | :--- | :--- | :--- |
| **OLED Display** | SDA / SCL | D2 / D1 | I2C Bus |
| **Spot IRs** | OUT | D5 (Spot 1), D6 (Spot 2)| Digital Input |
| **Spot LEDs** | Positive | D7/D8 (Sp1), D3/D4 (Sp2)| Requires 220Ω Resistors |
| **LDR (Light)** | D0 (Digital Out) | D0 (GPIO16) | Hardware threshold bypass |

---

## 🔐 The Cryptographic Protocol

Data is **never** sent in plaintext over the Wi-Fi network. The custom C++ encryption engine performs the following multi-stage cryptographic operations before physical transmission:

1. **Payload Generation:** Telemetry is dynamically compiled into a structured plaintext string (e.g., `ENV:T:25:H:60:L:1024:IR:1:PIR:0`).
2. **PKCS#7 Padding:** The string is padded out to a strict multiple of 16 bytes to align with block cipher requirements.
3. **AES-128 Encryption:** The padded block is encrypted using Advanced Encryption Standard in Cipher Block Chaining (CBC) mode with a localized Initialization Vector (IV).
4. **Base64 Encoding:** The raw binary ciphertext is encoded in Base64 formatting to ensure transmission stability across the MQTT protocol.
5. **HMAC-SHA256 Signing:** An explicitly tracked, incrementing mathematical `Nonce` is prepended to the Base64 string. The entire concatenated block is then hashed using a secret key to create an unforgeable, tamper-proof signature.
6. **Transmission Format:** The final packet beamed over the air is structured as: `[Nonce]:[Encrypted Base64]:[HMAC Signature]`.

---

## 🚀 Installation & System Setup

### 1. Software Dependencies
Ensure your environment has the following prerequisites installed:
* Python 3.9+
* Arduino IDE 2.0+ with the following board manager cores: `esp32`, `esp8266`, `Arduino UNO R4 Boards`.

### 2. Clone the Repository
```bash
git clone [https://github.com/MokshSharma2006/SmartCityProject.git](https://github.com/MokshSharma2006/SmartCityProject.git)

cd SmartCityProject
```

### 3.Setup the Raspberry Pi 5 
~~~
sudo apt update

sudo apt install mosquitto mosquitto-clients -y
~~~

#### Configure Mosquitto to accept inbound Edge Node telemetry by editing:

~~~
nano /etc/mosquitto/mosquitto.conf
~~~

##### Configure these:

```plaintext
listener 1883 0.0.0.0
allow_anonymous true
```

#### Restart the background services
~~~
sudo systemctl restart mosquitto
~~~

### 4. Initialize the Python Virtual Enviornment

~~~
python3 -m venv env

source env/bin/activate

pip install paho-mqtt pycryptodome flask flask-socketio
~~~

### 5. Deploy Edge Node Firmware

##### Open the respective .ino files in the Arduino IDE.

##### Update the ssid, password, and mqtt_server (The IPv4 address of your Raspberry Pi) in the global variables of each script.

##### Upload the code to the ESP32, ESP32-C3, Arduino Uno R4, and ESP8266 boards.

### 6. Launch the Command Center

#### Execute the Flask server on the Raspberry Pi:
~~~
python3 dashboard.py
~~~

##### Open a web browser on any device connected to the local network and navigate to the HUD:
~~~
http://<YOUR_PI_IP>:5000
~~~

---

## 🛠️ Troubleshooting & Known Hardware Quirks

##### ESP32-C3 Serial Monitor shows ESP-ROM:esp32c3-api1-20210207 and Freezes: * Fix: The ESP32-C3 handles its USB interface via software. In the Arduino IDE, go to Tools -> USB CDC On Boot and change it to Enabled before compiling.

##### PIR Sensor is stuck on "MOTION DETECTED" permanently:

##### Fix: This is a physical hardware delay. On the back of the HC-SR501 PIR sensor, turn the left potentiometer (Time Delay) completely counter-clockwise to reduce the signal hold time to its minimum 3-second threshold.

##### RFID Scanner is returning random garbled characters:

##### Fix: The RC522 module is extremely sensitive to voltage. Ensure it is powered strictly by the 3V3 rail. Powering it with 5V will permanently damage the logic gates. Ensure wires are less than 10cm long to avoid SPI signal degradation.
