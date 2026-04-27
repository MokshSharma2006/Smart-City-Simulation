import paho.mqtt.client as mqtt
from Crypto.Cipher import AES
import base64
import hmac
import hashlib
from flask import Flask, render_template
from flask_socketio import SocketIO

#Flask and WebSocket

app = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*")

@app.route('/')
def index():
    return render_template('index.html')

#Crypto

SECRET_KEY = b'1234567890123456'
IV =         b'abcdefghijklmnop'
AUTHORIZED_UID = "06CC6A06"

# Anti-Replay Attack & Telemetry Tracking
node_nonces = {
    "city/gate/status": 0,
    "city/zone1/status": 0,
    "city/traffic/status": 0,
    "city/parking/status": 0
}

# Global counter for Live Network Telemetry
network_message_count = 0

# Telemetry  

def emit_telemetry():
    global network_message_count
    while True:
        socketio.sleep(1) # Wait exactly 1 second
        # Broadcast the packets-per-second, then reset the counter
        socketio.emit('network_telemetry', {'msg_per_sec': network_message_count})
        network_message_count = 0

#MQTT

def on_connect(client, userdata, flags, reason_code, properties):
    print("✅ CENTRAL CONTROL ONLINE - Subscribed to city/+/status")
    client.subscribe("city/+/status") 

def on_message(client, userdata, msg):
    global node_nonces, network_message_count
    topic = msg.topic
    
    # Register the packet for the live telemetry chart
    network_message_count += 1
    
    try:
        raw_payload = msg.payload.decode('utf-8')
        parts = raw_payload.split(":")
        
        # Ensure payload has Nonce:Data:HMAC
        if len(parts) != 3: return
        incoming_nonce, encrypted_data, received_hmac = int(parts[0]), parts[1], parts[2]
        
        # 1. VERIFY HMAC (Tamper Check)
        data_to_verify = f"{incoming_nonce}:{encrypted_data}".encode('utf-8')
        calculated_hmac = hmac.new(SECRET_KEY, data_to_verify, hashlib.sha256).hexdigest()
        if not hmac.compare_digest(calculated_hmac, received_hmac): 
            print(f"⚠️ TAMPER ALERT on {topic}!")
            return
            
        # 2. VERIFY NONCE (Anti-Replay Attack Check)
        if topic not in node_nonces: node_nonces[topic] = 0 
        if incoming_nonce <= node_nonces[topic]: 
            print(f"⚠️ REPLAY ATTACK BLOCKED on {topic}!")
            return
        node_nonces[topic] = incoming_nonce
        
        # 3. DECRYPT AES-128-CBC
        encrypted_bytes = base64.b64decode(encrypted_data)
        cipher = AES.new(SECRET_KEY, AES.MODE_CBC, IV)
        decrypted_padded = cipher.decrypt(encrypted_bytes)
        plaintext = decrypted_padded[:-decrypted_padded[-1]].decode('utf-8')
        
        # Data Routing
        
        # --- 🚪 NODE 1: GATE CHECKPOINT (ESP32) ---
        if topic == "city/gate/status":
            # FIX: Expanded routing to cover ALL message types the ESP32 sends.
            # Previously STATUS:GATE_CLOSED, SENSOR:VEHICLE_*, STATUS:TRIPWIRE_CLEARED
            # all fell through unemitted — so the dashboard never updated for those events.
            
            if AUTHORIZED_UID in plaintext:
                # Authorized RFID scan
                socketio.emit('gate_data', {'msg': f"✅ ACCESS GRANTED: {plaintext}", 'alert': False})
            
            elif plaintext.startswith("SCANNED_UID:"):
                # Unauthorized RFID scan
                uid = plaintext.split(":", 1)[1]
                socketio.emit('gate_data', {'msg': f"⛔ ACCESS DENIED — Unknown UID: {uid}", 'alert': True})
            
            elif "ALERT" in plaintext or "BREACH" in plaintext or "MOTION_DETECTED" in plaintext:
                # Security alerts
                socketio.emit('gate_data', {'msg': f"🚨 {plaintext}", 'alert': True})
            
            elif plaintext == "STATUS:GATE_CLOSED":
                socketio.emit('gate_data', {'msg': "🔒 Gate secured — closed after timeout.", 'alert': False})
            
            elif plaintext == "STATUS:TRIPWIRE_CLEARED":
                socketio.emit('gate_data', {'msg': "✅ IR Tripwire cleared.", 'alert': False})
                socketio.emit('gate_alert_clear', {})  # Also tell dashboard to clear the map node alert
            
            elif plaintext.startswith("SENSOR:VEHICLE"):
                status = "arrived at gate — awaiting scan." if "WAITING" in plaintext else "departed. Gate clear."
                socketio.emit('gate_data', {'msg': f"🚗 Vehicle {status}", 'alert': False})
            
            else:
                # Catch-all for any other status messages
                socketio.emit('gate_data', {'msg': plaintext, 'alert': False})
                
        # --- 🌳 NODE 2: PERIMETER ZONE (ESP32-C3 SUPER NODE) ---
        elif topic == "city/zone1/status":
            if plaintext.startswith("ENV:"):
                p = plaintext.split(":") 
                if len(p) >= 11:
                    # FIX: IR is active-LOW on the ESP32-C3 (p[8]=="0" means beam broken = TRIGGERED)
                    # PIR is active-HIGH (p[10]=="1" means motion detected)
                    ir_status = "TRIGGERED" if p[8] == "0" else "CLEAR"
                    pir_status = "MOTION DETECTED" if p[10] == "1" else "STILL"
                    socketio.emit('zone_data', {
                        'temp': p[2], 'hum': p[4], 'light': p[6],
                        'ir': ir_status, 'pir': pir_status
                    })
                    # FIX: After emitting zone_data, the dashboard JS now handles auto-clear
                    # via a timeout. But also emit zone_alert_clear when both sensors are CLEAR
                    # so the map node resets immediately when the zone is safe again.
                    if ir_status == "CLEAR" and pir_status == "STILL":
                        socketio.emit('zone_alert_clear', {})
            else:
                socketio.emit('zone_alert', {'msg': plaintext})
                
        # --- 🚦 NODE 3: TRAFFIC JUNCTION (Arduino Uno R4) ---
        elif topic == "city/traffic/status":
            if plaintext.startswith("ENV:"):
                p = plaintext.split(":") 
                ir1 = "CLEAR" if p[4] == "1" else "WAITING"
                ir2 = "CLEAR" if p[6] == "1" else "WAITING"
                socketio.emit('traffic_data', {'light': p[2], 'lane1': ir1, 'lane2': ir2})
            else:
                socketio.emit('traffic_alert', {'msg': plaintext})
                
        # --- 🅿️ NODE 4: SMART PARKING (ESP8266) ---
        elif topic == "city/parking/status":
            if plaintext.startswith("PARKING:"):
                p = plaintext.split(":") 
                socketio.emit('parking_data', {'light': p[2], 'p1': p[4], 'p2': p[6], 'free': p[8]})

    except Exception as e:
        print(f"Decode Error on {topic}: {e}")

# System Initialization

mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message

if __name__ == '__main__':
    print("🏙️ BOOTING SMART CITY COMMAND CENTER...")
    try:
        mqtt_client.connect("localhost", 1883, 60)
        mqtt_client.loop_start() 
        
        # Start the background telemetry thread
        socketio.start_background_task(emit_telemetry)
        
        socketio.run(app, host='0.0.0.0', port=5000, debug=False)
    except KeyboardInterrupt:
        print("\n🛑 SHUTTING DOWN COMMAND CENTER...")
        mqtt_client.loop_stop()
