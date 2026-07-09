import json
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
import mysql.connector
import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish

app = FastAPI()

db_config = {
    "host": "localhost",
    "user": "YOUR_DATABASE_USERNAME",          
    "password": "YOUR_DATABASE_PASSWORD",  
    "database": "YOUR_DATABASE_NAME"
}

MQTT_BROKER = "localhost"
MQTT_SUB_TOPIC = "monitor/nodes/+/telemetry"
MQTT_ALERT_TOPIC = "monitor/nodes/alerts"

def initialize_database():
    try:
        temp_config = db_config.copy()
        db_name = temp_config.pop("database")
        
        conn = mysql.connector.connect(**temp_config)
        cursor = conn.cursor()
        cursor.execute(f"CREATE DATABASE IF NOT EXISTS {db_name}")
        cursor.execute(f"USE {db_name}")
        
        # FIXED: node_id is now the PRIMARY KEY. No duplicates can physically exist.
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS environmental_telemetry (
                node_id VARCHAR(50) PRIMARY KEY,
                temperature DECIMAL(5,2) NOT NULL,
                humidity DECIMAL(5,2) NOT NULL,
                pressure DECIMAL(6,2) NULL,
                timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
            )
        """)
        
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS room_registry (
                node_id VARCHAR(50) PRIMARY KEY,
                custom_name VARCHAR(100) NOT NULL,
                temperature_threshold DECIMAL(5,2) DEFAULT 30.0
            )
        """)
            
        conn.commit()
        cursor.close()
        conn.close()
        print("[Auto-Init Engine] Flat real-time database cache structures verified.")
    except Exception as e:
        print(f"[Auto-Init Failure] Initialization stalled: {e}")

initialize_database()

def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker with result code " + str(rc))
    client.subscribe("monitor/nodes/+/telemetry") # <-- Changed to '+' wildcard

def on_message(client, userdata, msg):
    print(f"\n📥 [NEW MQTT PACKET INGRESS] Topic matched: {msg.topic}")
    try:
        raw_payload = msg.payload.decode('utf-8')
        print(f"   Raw Content String: {raw_payload}")
        
        payload_data = json.loads(raw_payload)
        node_id = payload_data["node"]             
        temperature = payload_data["temperature"]
        humidity = payload_data["humidity"]
        pressure = payload_data["pressure"]

        # Spin up localized database interaction instance
        conn = mysql.connector.connect(**db_config)
        cursor = conn.cursor()  # FIXED: Changed from db_connection.cursor()
        
        sql = """
            INSERT INTO environmental_telemetry (node_id, temperature, humidity, pressure, timestamp)
            VALUES (%s, %s, %s, %s, NOW())
            ON DUPLICATE KEY UPDATE
                temperature = VALUES(temperature),
                humidity = VALUES(humidity),
                pressure = VALUES(pressure),
                timestamp = NOW()
        """
        cursor.execute(sql, (node_id, temperature, humidity, pressure))
        conn.commit()  # FIXED: Changed from db_connection.commit()
        cursor.close()
        conn.close()   # FIXED: Changed from db_connection.close()
        print(f"💾 SUCCESS: Database updated for primary key footprint -> {node_id}")

    except Exception as e:
        print(f"❌ FAILED PROCESS PIPELINE: Couldn't write to database. Error: {e}")

mqtt_client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
mqtt_client.on_message = on_message
mqtt_client.connect(MQTT_BROKER, 1883, 60)
mqtt_client.subscribe(MQTT_SUB_TOPIC)
mqtt_client.loop_start()

class RoomConfigureRequest(BaseModel):
    node_id: str
    custom_name: str
    temperature_threshold: float

@app.get("/api/telemetry/latest")
def get_all_rooms_telemetry():
    """Fetches the flat real-time state cache joined directly with individual room rules configuration parameters."""
    try:
        conn = mysql.connector.connect(**db_config)
        cursor = conn.cursor(dictionary=True)
        
        # FIXED: Beautiful, clean, ultra-fast single JOIN query with zero duplication risk
        query = """
            SELECT t.*, 
                   COALESCE(r.custom_name, t.node_id) AS display_name, 
                   COALESCE(r.temperature_threshold, 30.0) AS threshold
            FROM environmental_telemetry t
            LEFT JOIN room_registry r ON t.node_id = r.node_id;
        """
        cursor.execute(query)
        rows = cursor.fetchall()
        cursor.close()
        conn.close()
        
        return [{
            "raw_node_id": row["node_id"],
            "display_name": row["display_name"].replace("_", " ").upper(),
            "temperature": float(row["temperature"]),
            "humidity": float(row["humidity"]),
            "pressure": float(row["pressure"]) if row["pressure"] else 0.0,
            "threshold": float(row["threshold"])
        } for row in rows]
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/room/configure")
def configure_room(data: RoomConfigureRequest):
    try:
        conn = mysql.connector.connect(**db_config)
        cursor = conn.cursor()
        query = """
            INSERT INTO room_registry (node_id, custom_name, temperature_threshold) 
            VALUES (%s, %s, %s) 
            ON DUPLICATE KEY UPDATE custom_name = %s, temperature_threshold = %s
        """
        cursor.execute(query, (data.node_id, data.custom_name, data.temperature_threshold, data.custom_name, data.temperature_threshold))
        conn.commit()
        cursor.close()
        conn.close()
        return {"status": "Success"}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

class RenameRequest(BaseModel):
    node_id: str
    custom_name: str

@app.post("/api/room/rename")
def rename_room(data: RenameRequest):
    try:
        conn = mysql.connector.connect(**db_config)
        cursor = conn.cursor()
        query = """
            INSERT INTO room_registry (node_id, custom_name) 
            VALUES (%s, %s) 
            ON DUPLICATE KEY UPDATE custom_name = %s
        """
        cursor.execute(query, (data.node_id, data.custom_name, data.custom_name))
        conn.commit()
        cursor.close()
        conn.close()
        return {"status": "Success", "node_id": data.node_id, "assigned_alias": data.custom_name}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))