#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <PubSubClient.h>

// -------------------------------------------------------------------------
// ESP32 HARDWARE PIN DEFINITIONS
// -------------------------------------------------------------------------
#define GREEN_LED_PIN 12  // GPIO 12
#define RED_LED_PIN   14  // GPIO 14
#define BUZZER_PIN    27  // GPIO 27

// -------------------------------------------------------------------------
// NETWORK & BROKER CONFIGURATION
// -------------------------------------------------------------------------
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "YOUR_COMPUTER_LOCAL_IP";

// Global System Driver Structures
WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_BME280 bme;

String uniqueNodeId;
String telemetryTopic;
const char* alertTopic = "monitor/nodes/alerts";

unsigned long lastTelemetryTime = 0;
const unsigned long telemetryInterval = 4000; // 4-second transmit interval

// -------------------------------------------------------------------------
// MQTT INGRESS ALERT ENGINE (ACTIVE BUZZER + LED TRIGGERS)
// -------------------------------------------------------------------------
void callback(char* topic, byte* payload, unsigned int length) {
    String incomingMessage = "";
    for (unsigned int i = 0; i < length; i++) {
        incomingMessage += (char)payload[i];
    }
    
    Serial.println("[MQTT Alert] Message arrived: " + incomingMessage);

    String triggerOnSignal  = "ALERT_" + uniqueNodeId + "_ON";
    String triggerOffSignal = "ALERT_" + uniqueNodeId + "_OFF";
    
    triggerOnSignal.toUpperCase();
    triggerOffSignal.toUpperCase();

    if (incomingMessage == triggerOnSignal) {
        digitalWrite(RED_LED_PIN, HIGH);   // Turn on alert light
        digitalWrite(BUZZER_PIN, HIGH);    // Trigger continuous active buzzer tone
        digitalWrite(GREEN_LED_PIN, LOW);  // Turn off safe status light
        Serial.println("⚠️ WARNING: Threshold breached! Alarm sounding.");
    } 
    else if (incomingMessage == triggerOffSignal) {
        digitalWrite(RED_LED_PIN, LOW);    // Turn off alert light
        digitalWrite(BUZZER_PIN, LOW);     // Silence active buzzer
        digitalWrite(GREEN_LED_PIN, HIGH); // Turn on safe status light
        Serial.println("✅ Status Normalized. Alarm silenced.");
    }
}

// -------------------------------------------------------------------------
// SYSTEM CONNECTIVITY MANAGERS
// -------------------------------------------------------------------------
void setupWiFi() {
    delay(10);
    Serial.println("\n--- Initializing ESP32 Network Layer ---");
    Serial.print("Connecting to network: ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nWiFi infrastructure linked successfully.");
    Serial.print("Assigned IP endpoint address: ");
    Serial.println(WiFi.localIP());
}

void reconnectBroker() {
    while (!client.connected()) {
        Serial.print("[MQTT Core] Attempting broker connection...");
        if (client.connect(uniqueNodeId.c_str())) {
            Serial.println("CONNECTED.");
            client.subscribe(alertTopic);
            Serial.println("[MQTT Core] Subscribed to alerting channel.");
        } else {
            Serial.print("FAILED, rc=");
            Serial.print(client.state());
            Serial.println(" | Retrying connection in 5 seconds...");
            delay(5000);
        }
    }
}

// -------------------------------------------------------------------------
// RUNTIME SETUP CORE
// -------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);

    // Initialize physical pin modes for outputs
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    // Set safe starting baseline state
    digitalWrite(GREEN_LED_PIN, HIGH); 
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);

    // 1. Establish wireless link
    setupWiFi();

    // 2. Fetch unique factory MAC address
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    uniqueNodeId = "ESP32_" + mac;
    telemetryTopic = "monitor/nodes/" + uniqueNodeId + "/telemetry";
    
    Serial.println("==================================================");
    Serial.println("ESP32 HARDWARE Identity INITIALIZED");
    Serial.println("Node ID  : " + uniqueNodeId);
    Serial.println("Publishing to: " + telemetryTopic);
    Serial.println("==================================================");

    // 3. Start BME280 using your verified 0x76 address
    if (!bme.begin(0x76)) { 
        Serial.println("Could not find BME280 sensor breakout! Check connections.");
        while (1);
    }
    Serial.println("[Hardware Core] BME280 online at address 0x76.");

    // 4. Attach communication interface profiles
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
}

// -------------------------------------------------------------------------
// MAIN PROCESSING LOOP
// -------------------------------------------------------------------------
void loop() {
    if (!client.connected()) {
        reconnectBroker();
    }
    client.loop();

    unsigned long currentTimestamp = millis();
    if (currentTimestamp - lastTelemetryTime >= telemetryInterval) {
        lastTelemetryTime = currentTimestamp;

        // Fetch climate data snapshots directly from precision registers
        float currentTemp = bme.readTemperature();
        float currentHum  = bme.readHumidity();
        float currentPres = bme.readPressure() / 100.0F;

        if (isnan(currentTemp) || isnan(currentHum)) {
            Serial.println("⚠️ Error reading data from BME280 hardware layer.");
            return;
        }

        // Build serialization string matching cache database expectations
        String jsonPayload = "{\"node\":\"" + uniqueNodeId + 
                             "\",\"temperature\":" + String(currentTemp, 2) + 
                             ",\"humidity\":" + String(currentHum, 2) + 
                             ",\"pressure\":" + String(currentPres, 2) + "}";

        Serial.print("[Data Egress] Payload packet: ");
        Serial.println(jsonPayload);

        client.publish(telemetryTopic.c_str(), jsonPayload.c_str());
    }
}