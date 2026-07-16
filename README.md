# Distributed Environmental Monitoring System

An end-to-end, multi-tier IoT ecosystem that monitors ambient climate conditions (Temperature, Humidity, and Atmospheric Pressure) across localized rooms using distributed **ESP32** microcontrollers. Data is streamed asynchronously over **MQTT**, processed through a **FastAPI** gateway engine, cached and archived in a **MariaDB** database, and visualized via a native **Android Application** featuring real-time thresholds and active audible/visual hardware alerts.
An end-to-end, multi-tier IoT ecosystem that monitors ambient climate conditions (Temperature and Humidity) across localized rooms using distributed **ESP32** microcontrollers. Data is streamed asynchronously over **MQTT**, processed through a **FastAPI** gateway engine, cached in a **MariaDB** database, and visualized via a native **Android Application** featuring real-time thresholds and active audible/visual hardware alerts.

---

## 🏗️ System Architecture

1. **Hardware Layer (Edge Nodes):** ESP32 microcontrollers sampling environmental data from DHT11 sensors.
1. **Hardware Layer (Edge Nodes):** ESP32 microcontrollers sampling environmental data from DHT11 sensors.
1. **Transport Layer:** Asynchronous message distribution using an enterprise Mosquitto MQTT Broker over local Wi-Fi networks.
1. **Gateway Backend API:** FastAPI server managing concurrent threads, reading edge telemetry via wildcards, managing real-time table caches + time-series history logs, and exposing RESTful endpoints.
1. **Persistence Layer:** MariaDB database utilizing transactional triggers and relation matrices.
1. **Gateway Backend API:** FastAPI server managing concurrent threads, reading edge telemetry via wildcards, managing a real-time table cache, and exposing RESTful endpoints.
1. **Persistence Layer:** MariaDB database for caching and configuration.
1. **Mobile Application frontend:** Native Android application that polls backend endpoints to display live telemetry cards, threshold configurations, and alert overlays.

---

## 🛠️ Tech Stack & Dependencies

### Hardware / Firmware

- **Microcontroller:** ESP32-WROOM-32D
- **Sensor Breakout:** DHT11 Temperature and Humidity Sensor
- **Sensor Breakout:** DHT11 Temperature and Humidity Sensor
- **Peripherals:** 5V Active Buzzer, Standard Red & Green LEDs, 220Ω Resistors
- **Framework:** Arduino C++ (compiled via PlatformIO inside VS Code)

### Backend API & Broker

- **Language/Runtime:** Python 3.14+
- **Web Framework:** FastAPI + Uvicorn ASGI Server
- **MQTT Broker:** Eclipse Mosquitto (v2.0+)
- **Database Driver:** MySQL Connector Python
- **Database Server:** MariaDB / MySQL Server

### Mobile Client

- **IDE:** Android Studio
- **Languages:** Jetpack Compose / Kotlin / Java Architecture
- **Network Interface:** Retrofit2 / OkHttp HTTP Client Layer

---

## 🔌 Hardware Pin Mapping Layout

The edge node utilizes specific internal GPIO registers on the ESP32 chip. Use this schematic reference layout:

| Component Module     | Component Pin | ESP32 GPIO Assignment | Hardware Role Context                                    |
| :------------------- | :------------ | :-------------------- | :------------------------------------------------------- |
| **DHT11 Sensor**     | VCC           | **3V3**               | 3.3V Positive Power Supply Rail                          |
| **DHT11 Sensor**     | GND           | **GND**               | System Ground Reference Rail                             |
| **DHT11 Sensor**     | Data          | **D23 (GPIO 23)**     | One-Wire Serial Data Line                                |
| **DHT11 Sensor**     | VCC           | **3V3**               | 3.3V Positive Power Supply Rail                          |
| **DHT11 Sensor**     | GND           | **GND**               | System Ground Reference Rail                             |
| **DHT11 Sensor**     | Data          | **D23 (GPIO 23)**     | One-Wire Serial Data Line                                |
| **Green Status LED** | Long Leg (+)  | **D12 (GPIO 12)**     | Output Indicator: State Safe Status (Normal Conditions)  |
| **Red Warning LED**  | Long Leg (+)  | **D14 (GPIO 14)**     | Output Indicator: State Breach Warning (High Heat Alarm) |
| **Active Buzzer**    | Positive (+)  | **D27 (GPIO 27)**     | Output Alarm: Constant HIGH signal triggers audible tone |

_Note: Always place a 220Ω resistor inline with the positive legs of the LEDs to prevent overcurrent damage._

---

## 📡 MQTT Messaging Schema

The system utilizes structured, dynamic topic trees to handle multiple microcontrollers seamlessly without editing the backend code:

- **Telemetry Ingress (Edge to Broker):** `monitor/nodes/ESP32_[MAC_ADDRESS]/telemetry`
    - _Payload Example:_ `{"node":"ESP32_1CC3ABD15874","temperature":31.25,"humidity":46.80,"pressure":1000.55}`
    - _Payload Example:_ `{"node":"ESP32_1CC3ABD15874","temperature":25.50,"humidity":65.00,"pressure":0.0}`
- **Alert Ingress/Egress:** `monitor/nodes/alerts`
    - _Payload Commands:_ `ALERT_ESP32_[MAC_ADDRESS]_ON` | `ALERT_ESP32_[MAC_ADDRESS]_OFF`

---

## 🗄️ Database Relational Design (`environmental_monitor`)

The architecture handles data splitting using a dual-write pipeline design: a real-time flat cache table for immediate UI responsiveness, and an append-only transaction history log table for timeline tracking.
The architecture uses a real-time flat cache table for immediate UI responsiveness and a table for storing node configurations.

### 1. `environmental_telemetry` (State Cache)

Stores only the **absolute latest packet** received from each node by utilizing `ON DUPLICATE KEY UPDATE` optimization rules.

- `node_id` (VARCHAR, PRIMARY KEY)
- `temperature` (DECIMAL)
- `humidity` (DECIMAL)
- `pressure` (DECIMAL)
- `pressure` (DECIMAL) - _Note: This will be `0.0` for DHT11 sensors, but is kept for future compatibility with other sensors._
- `timestamp` (TIMESTAMP, AUTO-UPDATE)

### 2. `environmental_history` (Chronological Archive)

### 2. `room_registry` (Configuration Meta Layer)

An append-only database that captures **every data heartbeat** transmitted across the lifetime of the project for charting and analytics.

- `history_id` (INT, AUTO_INCREMENT, PRIMARY KEY)
- `node_id` (VARCHAR)
- `temperature` (DECIMAL)
- `humidity` (DECIMAL)
- `pressure` (DECIMAL)
- `timestamp` (TIMESTAMP)

### 3. `room_registry` (Configuration Meta Layer)

Maps physical microcontrollers to human-readable names and user-defined alert thresholds.

- `node_id` (VARCHAR, PRIMARY KEY)
- `custom_name` (VARCHAR)
- `temperature_threshold` (DECIMAL, Default: `30.0`)

---

## 🚀 Quickstart Installation Guide

### 1. Database Setup

Log into your local MariaDB instance and execute the schema configurations:

```sql
CREATE DATABASE environmental_monitor;
```

### 2. Mosquitto Broker Security Configuration

To allow the ESP32 nodes to distribute message packets to the host machine, ensure your `/etc/mosquitto/mosquitto.conf` contains the following local network configurations:

```text
listener 1883 0.0.0.0
allow_anonymous true
```

Restart the broker service to apply changes:

```bash
sudo systemctl restart mosquitto
```

### 3. Spin up the FastAPI Gateway

Navigate to the backend environment subfolder, activate the local Python environment wrapper, and launch the server application:

```bash
cd backend
source env/bin/activate
pip install -r requirements.txt
uvicorn backend_gateway:app --host 0.0.0.0 --port 8000 --reload

```

### 4. Flash Firmware Nodes

Open the `firmware/` directory inside VS Code with PlatformIO active. Update the `src/main.cpp` configuration header with your specific local Wi-Fi parameters:

```cpp
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "YOUR_HOST_GATEWAY_IP";

```

Click **Upload** to compile and flash the binary payload onto the target ESP32 development board.

### 5. Build mobile interface Client

Launch Android Studio, open the `android_app/` folder workspace structure, sync gradle scripts, and deploy the application build straight onto an Android Device or virtual emulator profile.
