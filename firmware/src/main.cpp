#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <PubSubClient.h>

// -------------------------------------------------------------------------
// HARDWARE PIN DEFINITIONS
// -------------------------------------------------------------------------
#define BOOT_BUTTON_PIN 0   // Built-in BOOT button mapped to GPIO 0
#define GREEN_LED_PIN   12  // GPIO 12
#define RED_LED_PIN     14  // GPIO 14
#define BUZZER_PIN      27  // GPIO 27

#define DHTPIN          23  // DHT11 Data pin hooked to GPIO 23
#define DHTTYPE         DHT11

// -------------------------------------------------------------------------
// NETWORK & BROKER CONFIGURATION
// -------------------------------------------------------------------------
const char* ssid     = "YOUR_WIFI_SSID-";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "YOUR_COMPUTER_LOCAL_IP"; 

// Global System Driver Structures
WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);

String uniqueNodeId;
String telemetryTopic;
const char* alertTopic = "monitor/nodes/alerts";

unsigned long lastTelemetryTime = 0;
const unsigned long telemetryInterval = 4000; 

// --- AUTOMATION STATE FLAG ---
bool alertActive = false; // Tracks if this specific node is currently violating thresholds

// -------------------------------------------------------------------------
// STARTUP GATEKEEPER FUNCTION
// -------------------------------------------------------------------------
void waitForButtonPress() {
    // GPIO 0 uses an internal pull-up resistor (sits at HIGH until pressed)
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP); 
    
    Serial.println("\n==================================================");
    Serial.println("⏸️ SYSTEM PAUSED: Press the BOOT button to start...");
    Serial.println("==================================================");
    
    // Loop indefinitely as long as the button reads HIGH (not pressed)
    while (digitalRead(BOOT_BUTTON_PIN) == HIGH) {
        delay(50); // Small delay to prevent watchdog timer resets
    }
    
    Serial.println("\n▶️ Button press detected! Initializing systems...");
    delay(500); // Brief hardware debounce delay
}

// -------------------------------------------------------------------------
// MQTT INGRESS ALERT ENGINE (UPDATED FOR ASYNCHRONOUS PULSING)
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
        digitalWrite(RED_LED_PIN, HIGH);   // Threshold breached -> Turn Red On
        digitalWrite(GREEN_LED_PIN, LOW);  // Turn Green Off
        alertActive = true;                // Arm the pulsing engine in the background loop
        Serial.println("⚠️ WARNING: Local threshold breached! Alarm system ARMED.");
    } 
    else if (incomingMessage == triggerOffSignal) {
        digitalWrite(RED_LED_PIN, LOW);    // Turn Red Off
        digitalWrite(GREEN_LED_PIN, HIGH); // Turn Green On
        digitalWrite(BUZZER_PIN, LOW);     // Immediate silence safety override
        alertActive = false;               // Disarm the pulsing engine
        Serial.println("✅ Status Normalized. Alarm system DISARMED.");
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

    // Hold everything here until you physically click the BOOT button
    waitForButtonPress(); 

    // Initialize physical pin modes for outputs
    pinMode(GREEN_LED_PIN, OUTPUT);
    pinMode(RED_LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    // Initial predictable boot up state orientation
    digitalWrite(GREEN_LED_PIN, HIGH); 
    digitalWrite(RED_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);

    // 1. Initialize DHT11 sensor engine
    dht.begin();

    // 2. Fire up wireless radio stack
    setupWiFi();

    // 3. Fetch unique hardware signature
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    uniqueNodeId = "ESP32_" + mac;
    telemetryTopic = "monitor/nodes/" + uniqueNodeId + "/telemetry";
    
    Serial.println("==================================================");
    Serial.println("ESP32 DHT11 HARDWARE IDENTITY INITIALIZED");
    Serial.println("Node ID  : " + uniqueNodeId);
    Serial.println("Publishing to: " + telemetryTopic);
    Serial.println("==================================================");

    // 4. Attach broker configurations
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
}

// -------------------------------------------------------------------------
// ENGINE LOOP
// -------------------------------------------------------------------------
void loop() {
    if (!client.connected()) {
        reconnectBroker();
    }
    client.loop();

    unsigned long currentTimestamp = millis();

    // --- 🔍 NON-BLOCKING BUZZER PULSE GENERATOR ---
    if (alertActive) {
        // Obtains a continuous recurring window value from 0 to 1999ms
        unsigned long cycleTime = currentTimestamp % 2000; 
        
        if (cycleTime < 150) {
            digitalWrite(BUZZER_PIN, HIGH); // Quick clean 150ms chirp sound
        } else {
            digitalWrite(BUZZER_PIN, LOW);  // Drop low for the remaining 1850ms of the window
        }
    }

    // Telemetry generation loop handler frame segment block
    if (currentTimestamp - lastTelemetryTime >= telemetryInterval) {
        lastTelemetryTime = currentTimestamp;

        // Fetch snapshots directly from DHT11 registers
        float currentTemp = dht.readTemperature();
        float currentHum  = dht.readHumidity();

        if (isnan(currentTemp) || isnan(currentHum)) {
            Serial.println("⚠️ Error reading registers off DHT11 hardware layer.");
            return;
        }

        // Build flat dynamic payload formatting. DHT11 lacks pressure, so we pass 0.0
        String jsonPayload = "{\"node\":\"" + uniqueNodeId + 
                             "\",\"temperature\":" + String(currentTemp, 2) + 
                             ",\"humidity\":" + String(currentHum, 2) + 
                             ",\"pressure\": 0.0}";

        Serial.print("[Data Egress] Payload packet: ");
        Serial.println(jsonPayload);

        client.publish(telemetryTopic.c_str(), jsonPayload.c_str());
    }
}